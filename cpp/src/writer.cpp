#include "redis.h"

#include <iostream>
#include <cstring>
#include <spdlog/fmt/fmt.h>
#include <chrono>
#include "writer.h"
#include "redis_writer_commands.h"
#include "compression/compressor.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace std;

namespace river {

StreamWriter::StreamWriter(const StreamWriterParams& params)
        : redis_batch_size_(params.batch_size), keys_per_redis_stream_(params.keys_per_redis_stream) {
    this->redis_ = internal::Redis::Create(params.connection);

    this->is_stopped_ = false;
    this->is_initialized_ = false;
    this->total_samples_written_ = 0LL;
    this->last_stream_key_idx_ = 0;
    this->compression_ = params.compression;

    this->schema_ = nullptr;
    this->sample_size_ = -1;

    if (redis_batch_size_ <= 0) {
        throw StreamWriterException("Invalid batch size given, needs to be positive.");
    }
    if (keys_per_redis_stream_ <= 0) {
        throw StreamWriterException("Invalid keys per redis stream given, needs to be positive.");
    }
}

void StreamWriter::Initialize(const string &stream_name,
                              const StreamSchema &schema,
                              const unordered_map<string, string> &user_metadata,
                              bool compute_local_minus_global_clock) {
    if (this->is_stopped_) {
        throw StreamWriterException("Writer is already stopped; cannot Initialize a stopped stream.");
    }

    if (is_initialized_) {
        return;
    }

    if (stream_name.empty() || stream_name.size() >= 256) {
        throw StreamWriterException("Stream name is invalid. Must be given and < 256 in length.");
    }

    auto maybe_metadata = redis_->GetMetadata(stream_name);
    if (maybe_metadata) {
        stringstream ss;
        ss << "Stream metadata key exists already; does a stream with this name already exist? Stream: " << stream_name;
        throw StreamExistsException(ss.str());
    }

    if (schema.has_variable_width_field() && schema.field_definitions.size() != 1) {
        throw StreamWriterException("If one field is variable width, then that can be the only field.");
    }

    string serialized_schema = schema.ToJson();

    string first_stream_key = fmt::format("{}-0", stream_name);
    vector<pair<string, string>> fields = {
        {"first_stream_key", first_stream_key},
        {"schema", serialized_schema},
    };

    // If enabled, calculate the delta between clocks of the client and Redis server, and store offset
    if (compute_local_minus_global_clock) {
        auto local_minus_server_clock = ComputeLocalMinusServerClocks();
        initialized_at_us_ = chrono::duration_cast<std::chrono::microseconds>(
                chrono::system_clock::now().time_since_epoch()).count() - local_minus_server_clock;
        string local_minus_server_clock_f = fmt::format_int(local_minus_server_clock).str();
        fields.emplace_back("local_minus_server_clock_us", local_minus_server_clock_f);
    } else {
        initialized_at_us_ = chrono::duration_cast<std::chrono::microseconds>(
                chrono::system_clock::now().time_since_epoch()).count();
    }

    string initialized_at_us_f = fmt::format_int(initialized_at_us_).str();
    fields.emplace_back("initialized_at_us", initialized_at_us_f);

    this->compressor_ = CreateCompressor(compression_);
    if (compressor_) {
        json compressor_params;
        compressor_params["name"] = compression_.name();
        compressor_params["params"] = compression_.params();
        fields.emplace_back("compression_params_json", compressor_params.dump());
    }

    auto num_fields_added = static_cast<size_t>(
            redis_->SetMetadataAndUserMetadata(stream_name, fields, user_metadata));
    // Ensure to add 1 for user_metadata
    if (fields.size() + 1 != num_fields_added) {
        throw StreamWriterException(
                fmt::format("Stream exists already! stream {}. Expected {} fields to be written but {} were written.",
                            stream_name, fields.size(), num_fields_added));
    }

    auto metadata = redis_->GetMetadata(stream_name);
    if (!metadata) {
        throw StreamWriterException(fmt::format("HGETALL failed. stream_name={}", stream_name));
    }

    spdlog::info("Stream metadata");
    for (const auto& pair : *metadata) {
        // Truncate for very long metadatas/schemas:
        std::stringstream ss;
        ss << "=> " << pair.first << ": " << pair.second;
        std::string ss_str = ss.str();
        if (ss_str.length() >= 120) {
            spdlog::info(ss_str.substr(0, 120 - 3) + "...");
        } else {
            spdlog::info(ss_str);
        }
    }

    this->stream_name_ = stream_name;
    this->total_samples_written_ = 0ULL;
    this->is_initialized_ = true;
    this->schema_ = make_shared<StreamSchema>(schema);
    this->sample_size_ = this->schema_->sample_size();
    this->has_variable_width_field_ = this->schema_->has_variable_width_field();

    // TODO: handle this by encoding sizes in compressed
    if (this->has_variable_width_field_ && this->compression_.type() != StreamCompression::Type::UNCOMPRESSED) {
        throw StreamWriterException("Having variable width fields with compression is not supported right now");
    }

    auto installed_modules = redis_->GetInstalledModules();
    if (std::find(installed_modules.begin(), installed_modules.end(), "river") != installed_modules.end()) {
        spdlog::info("Found river module installed. Utilizing it for performance.");
        this->has_module_installed_ = true;
    } else {
        this->has_module_installed_ = false;
    }

    if (!this->has_module_installed_ && this->compression_.type() != StreamCompression::Type::UNCOMPRESSED) {
        throw StreamWriterException("Module must be installed to support compression.");
    }
}

void StreamWriter::WriteBytes(const char *data, int64_t num_samples, const int *sizes) {
    if (num_samples <= 0) {
        return;
    }

    if (!is_initialized_) {
        throw StreamWriterException("Stream is not yet initialized. Call #Initialize() first.");
    }

    if (is_stopped_) {
        throw StreamWriterException("Stream has already been stopped. Do not reuse these objects.");
    }

    if (this->has_variable_width_field_ && sizes == nullptr) {
        throw StreamWriterException("Stream has variable width fields; the size of each sample must be given!");
    }
    bool has_compression = (bool) compressor_;

    int64_t data_index = 0;
    int64_t samples_written = 0;
    while (samples_written < num_samples) {
        auto samples_remaining = num_samples - samples_written;
        int64_t samples_to_write_in_batch =
            samples_remaining > redis_batch_size_ ? redis_batch_size_ : samples_remaining;

        int stream_key_idx = static_cast<int>(total_samples_written_ / keys_per_redis_stream_);

        if (stream_key_idx != last_stream_key_idx_) {
            auto reply = redis_->Xadd(
                fmt::format("{}-{}", stream_name_, last_stream_key_idx_),
                {{"tombstone", "1"},
                 {"next_stream_key", fmt::format("{}-{}", stream_name_, stream_key_idx)},
                 {"sample_index", fmt::format_int(
                     total_samples_written_ == 0 ? total_samples_written_ : total_samples_written_ - 1).str()}});

            spdlog::info(
                "Adding tombstone entry for stream {}, key idx {} at samples {} | Response : {}",
                stream_name_, last_stream_key_idx_, total_samples_written_, std::to_string(reply->type));

            last_stream_key_idx_ = stream_key_idx;
        }

        if (has_module_installed_) {
            // We preallocate / reuse the command buffer as much as possible, as much of the bottleneck is in the
            // formatting of the command and copying of data. Instead, we do a "zero-copy" (ish) methodology where we
            // manually manage formatting and sending of the command, such that we don't ever copy the <data> until we need
            // to send it via network. A strong assumption in this methodology is that all XADD commands sent via Redis are
            // fairly uniform, and just need the last argument (the "data") switched out.
            // In addition, to reduce network bandwidth, we have a set of functions in a Redis server module (under the
            // library name "river") that is tailored towards our batch use of XADD. In particular, it minimizes the
            // redundant characters sent over the network.
            int append_argc = (this->has_variable_width_field_ || has_compression) ? 5 : 6;
            std::vector<const char *> append_argv(append_argc);
            std::vector<size_t> append_arglens(append_argc);

            const string &stream_key_formatted = fmt::format("{}-{}", stream_name_, stream_key_idx);
            append_argv[1] = stream_key_formatted.c_str();
            append_arglens[1] = strlen(append_argv[1]);

            auto formatted_global_index = fmt::format_int(total_samples_written_);
            append_argv[2] = formatted_global_index.c_str();
            append_arglens[2] = strlen(append_argv[2]);

            const char *data_to_write;
            int64_t data_to_write_num_bytes;

            // Placeholder vectors in case memory needs to be retained until sending
            std::vector<char> data_holder;

            auto formatted_num_samples = fmt::format_int(samples_to_write_in_batch).str();
            if (has_compression) {
                data_holder = compressor_->compress(&data[data_index], samples_to_write_in_batch * sample_size_);
                data_to_write = data_holder.data();
                data_to_write_num_bytes = (int64_t) data_holder.size();

                append_argv[0] = "RIVER.batch_xadd_compressed";
                append_arglens[0] = strlen(append_argv[0]);

                append_argv[3] = formatted_num_samples.c_str();
                append_arglens[3] = formatted_num_samples.size();

                // Overwritten later down, doesn't matter now.
                append_argv[4] = "\0";
                append_arglens[4] = 1;
            } else if (this->has_variable_width_field_) {
                append_argv[0] = "RIVER.batch_xadd_variable";
                append_arglens[0] = strlen(append_argv[0]);

                // TODO: we end up making a copy of sizes here instead of avoiding a copy like the data; we can
                // improve this by making sizes zero-copy as well
                append_argv[3] = (const char *) (&sizes[samples_written]);
                append_arglens[3] = sizeof(int) * samples_to_write_in_batch;

                // Overwritten later down, doesn't matter now.
                append_argv[4] = "\0";
                append_arglens[4] = 1;

                data_to_write = &data[data_index];
                data_to_write_num_bytes = 0;
                for (int i = 0; i < samples_to_write_in_batch; i++) {
                    data_to_write_num_bytes += sizes[samples_written + i];
                }
            } else {
                append_argv[0] = "RIVER.batch_xadd";
                append_arglens[0] = strlen(append_argv[0]);

                append_argv[3] = formatted_num_samples.c_str();
                append_arglens[3] = strlen(append_argv[3]);

                auto formatted_sample_size_bytes = fmt::format_int(sample_size_);
                append_argv[4] = formatted_sample_size_bytes.c_str();
                append_arglens[4] = strlen(append_argv[4]);

                // Overwritten later down, doesn't matter now.
                append_argv[5] = "\0";
                append_arglens[5] = 1;

                data_to_write = &data[data_index];
                data_to_write_num_bytes = sample_size_ * samples_to_write_in_batch;
            }

            std::string formatted_command_str =
                redis_->FormatCommandArgv(append_argc, append_argv.data(), append_arglens.data());
            RedisWriterCommand xadd_redis_command_(formatted_command_str);

            auto commands_to_send = xadd_redis_command_.ReplaceLastBulkStringAndAssemble(
                data_to_write, data_to_write_num_bytes);

            auto bytes_written = redis_->SendCommandPreformatted(commands_to_send);
            if (bytes_written < 0) {
                throw StreamWriterException(
                    fmt::format("Failed to write apprporiate number of bytes! wrote bytes={}", bytes_written));
            }

            data_index += data_to_write_num_bytes;

            auto reply = redis_->GetReply();
            if (reply->type != REDIS_REPLY_STATUS || reply->len == 0) {
                if (reply->type == REDIS_REPLY_ERROR && reply->len > 0) {
                    throw StreamWriterException(
                        fmt::format("batch_xadd response was ERROR: {} ", reply->str));
                } else {
                    throw StreamWriterException(
                        fmt::format("Reply was not of the right type (was {}) and/or had invalid length ({})",
                                    reply->type, reply->len));
                }
            }
        } else {
            // We preallocate / reuse the command buffer as much as possible, as much of the bottleneck is in the
            // formatting of the command.
            const int append_argc = 7;
            std::vector<const char *> append_argv(append_argc);
            std::vector<size_t> append_arglens(append_argc);

            append_argv[0] = "XADD";
            append_arglens[0] = strlen(append_argv[0]);

            const string &stream_key_formatted = fmt::format("{}-{}", stream_name_, stream_key_idx);
            append_argv[1] = stream_key_formatted.c_str();
            append_arglens[1] = strlen(append_argv[1]);

            append_argv[2] = "*";
            append_arglens[2] = 1;

            append_argv[3] = "val";
            append_arglens[3] = strlen(append_argv[3]);

            // Set per sample below
            append_argv[4] = nullptr;
            append_arglens[4] = sample_size_;

            // Set per sample below
            append_argv[5] = "i";
            append_arglens[5] = strlen(append_argv[5]);

            // Set per sample below
            append_argv[6] = nullptr;
            append_arglens[6] = 0;

            for (int64_t i = 0; i < samples_to_write_in_batch; i++) {
                int64_t global_index = total_samples_written_ + i;
                auto formatted_global_index = fmt::format_int(global_index);
                append_argv[6] = formatted_global_index.c_str();
                append_arglens[6] = formatted_global_index.size();

                append_argv[4] = &data[data_index];
                if (!this->has_variable_width_field_) {
                    data_index += sample_size_;
                } else {
                    int this_sample_size = sizes[samples_written + i];
                    append_arglens[4] = this_sample_size;
                    data_index += this_sample_size;
                }

                redis_->SendCommandArgv(append_argc, append_argv.data(), append_arglens.data());
            }

            for (int64_t i = 0; i < samples_to_write_in_batch; i++) {
                auto reply = redis_->GetReply();
                if (reply->type != REDIS_REPLY_STRING || reply->len == 0) {
                    throw StreamWriterException(
                        fmt::format("Reply was not of the right type (was {}) and/or had invalid length ({})",
                                    reply->type, reply->len));
                }
            }
        }

        samples_written += samples_to_write_in_batch;
        total_samples_written_ += samples_to_write_in_batch;
    }
}

int64_t StreamWriter::initialized_at_us() {
    return initialized_at_us_;
}

int64_t StreamWriter::ComputeLocalMinusServerClocks() {
    int64_t sum_deltas = 0;
    int num_round_trips = 100;
    for (int i = 0; i < num_round_trips; i++) {
        int64_t before = chrono::duration_cast<std::chrono::microseconds>(
                chrono::system_clock::now().time_since_epoch()).count();
        int64_t redis_time = redis_->TimeUs();
        int64_t after = chrono::duration_cast<std::chrono::microseconds>(
                chrono::system_clock::now().time_since_epoch()).count();
        int64_t local_time = (after + before) / 2;
        sum_deltas += local_time - redis_time;
    }

    auto delta = sum_deltas / num_round_trips;
    spdlog::info("Relative time (local - server) = {} us", delta);
    return delta;
}

void StreamWriter::Stop() {
    if (is_stopped_ || !is_initialized_) {
        return;
    }

    string stream_key = fmt::format("{}-{}", stream_name_, last_stream_key_idx_);
    redis_->Xadd(stream_key,
                 {{"eof", "1"},
                  {"sample_index",
                   fmt::format_int(total_samples_written_ == 0 ? 0 : total_samples_written_ - 1).str()}});

    spdlog::info("Adding eof entry for stream {}, idx {} at samples {}",
                 stream_name_, last_stream_key_idx_, total_samples_written_);

    is_stopped_ = true;
}

const string& StreamWriter::stream_name() {
    return stream_name_;
}

unordered_map<string, string> StreamWriter::Metadata() {
    auto ret = redis_->GetUserMetadata(stream_name_);
    if (!ret) {
        throw StreamWriterException(fmt::format(
            "Metadata could not be found for stream {}; has it been initialized?", stream_name_));
    }
    return *ret;
}

void StreamWriter::SetMetadata(const unordered_map<string, string>& metadata) {
    if (stream_name_.empty()) {
        throw StreamWriterException("Must call Initialize() first!");
    }

    redis_->SetUserMetadata(stream_name_, metadata);
}

int64_t StreamWriter::total_samples_written() {
    return total_samples_written_;
}

const StreamSchema& StreamWriter::schema() {
    if (!schema_) {
        throw StreamWriterException("Schema has not been initialized. Did you call Initialize()?");
    }
    return *this->schema_;
}

}
