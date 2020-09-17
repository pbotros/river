#include "redis.h"

#include <iostream>
#include <cstring>
#include <hiredis.h>
#include <fmt/format.h>
#include <chrono>
#include <glog/logging.h>
#include "writer.h"

using namespace std;

namespace river {

StreamWriter::StreamWriter(const RedisConnection &connection, const int64_t keys_per_redis_stream, const int batch_size)
        : redis_batch_size_(batch_size), keys_per_redis_stream_(keys_per_redis_stream) {
    this->redis_ = internal::Redis::Create(connection);

    this->is_stopped_ = false;
    this->is_initialized_ = false;
    this->total_samples_written_ = 0LL;
    this->last_stream_key_idx_ = 0;

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
                              const unordered_map<string, string> &user_metadata) {
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

    // Calculate the delta between clocks of the client and Redis server, and store offset
    auto local_minus_server_clock = ComputeLocalMinusServerClocks();
    initialized_at_us_ = chrono::duration_cast<std::chrono::microseconds>(
            chrono::system_clock::now().time_since_epoch()).count() - local_minus_server_clock;

    string first_stream_key = fmt::format("{}-0", stream_name);
    string local_minus_server_clock_f = fmt::format_int(local_minus_server_clock).str();
    string initialized_at_us_f = fmt::format_int(initialized_at_us_).str();

    initializer_list<pair<string, string>> fields = {
        {"first_stream_key", first_stream_key},
        {"schema", serialized_schema},
        {"local_minus_server_clock_us", local_minus_server_clock_f},
        {"initialized_at_us", initialized_at_us_f},
        {"user_metadata", "{}"}};
    auto num_fields_added = static_cast<size_t>(redis_->SetMetadata(stream_name, fields));
    if (fields.size() != num_fields_added) {
        throw StreamWriterException(
                fmt::format("Stream exists already! stream {}. Expected {} fields to be written but {} were written.",
                            stream_name, fields.size(), num_fields_added));
    }
    redis_->SetUserMetadata(stream_name, user_metadata);

    auto metadata = redis_->GetMetadata(stream_name);
    if (!metadata) {
        throw StreamWriterException(fmt::format("HGETALL failed. stream_name={}", stream_name));
    }

    LOG(INFO) << "Stream metadata:" << endl;
    for (const auto& pair : metadata.get()) {
        LOG(INFO) << "=> " << pair.first << ": " << pair.second << endl;
    }

    this->stream_name_ = stream_name;
    this->total_samples_written_ = 0ULL;
    this->is_initialized_ = true;
    this->schema_ = make_shared<StreamSchema>(schema);
    this->sample_size_ = this->schema_->sample_size();
    this->has_variable_width_field_ = this->schema_->has_variable_width_field();
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

            std::stringstream ss;
            ss << "Adding tombstone entry for stream " << stream_name_ << ", key idx "
               << std::to_string(last_stream_key_idx_) << " at samples "
               << std::to_string(total_samples_written_)
               << " | Response : " << std::to_string(reply->type);
            LOG(INFO) << ss.str() << std::endl;

            last_stream_key_idx_ = stream_key_idx;
        }

        // We preallocate / reuse the command buffer as much as possible, as much of the bottleneck is in the
        // formatting of the command.
        const int append_argc = 7;
        const char **append_argv = new const char *[append_argc];
        auto *append_arglens = new size_t [append_argc];

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

            redis_->SendCommandArgv(append_argc, append_argv, append_arglens);
        }

        delete[] append_argv;
        delete[] append_arglens;

        for (int64_t i = 0; i < samples_to_write_in_batch; i++) {
            auto reply = redis_->GetReply();
            if (reply->type != REDIS_REPLY_STRING || reply->len == 0) {
                throw StreamWriterException(
                        fmt::format("Reply was not of the right type (was {}) and/or had invalid length ({})",
                                    reply->type, reply->len));
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
    LOG(INFO) << "Relative time (local - server) = " << delta << " us" << endl;
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

    LOG(INFO) << "Adding eof entry for stream " << stream_name_ << ", idx " << std::to_string(last_stream_key_idx_)
              << " at samples " << std::to_string(total_samples_written_) << endl;

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
    return ret.get();
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
