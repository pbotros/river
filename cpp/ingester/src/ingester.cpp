//
// Created by Paul Botros on 10/28/19.
//

#include <thread>
#include <regex>
#include <chrono>
#include <boost/filesystem.hpp>
#include <arrow/io/file.h>
#include <arrow/util/logging.h>
#include <arrow/api.h>
#include <parquet/arrow/writer.h>
#include <parquet/properties.h>
#include <parquet/exception.h>
#include <spdlog/fmt/fmt.h>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <memory>
#include <boost/bind/bind.hpp>
#include <spdlog/spdlog.h>

#include <utility>

#include "ingester.h"

namespace river {

StreamIngester::StreamIngester(const RedisConnection &connection,
                               const string &output_directory,
                               bool *terminated,
                               std::vector<std::pair<std::regex, StreamIngestionSettings>> stream_settings_by_name_glob,
                               int stalled_timeout_ms,
                               int stale_period_ms)
        : _connection(connection),
          _output_directory(output_directory),
          _terminated(terminated),
          stream_settings_by_name_glob_(std::move(stream_settings_by_name_glob)),
          _stalled_timeout_ms(stalled_timeout_ms),
          _stale_period_ms(stale_period_ms) {
    // Create the output directory if necessary
    if (boost::filesystem::exists(output_directory)) {
        if (!boost::filesystem::is_directory(output_directory)) {
            throw StreamIngesterException(fmt::format("Non-directory filepath given: {}", output_directory));
        }
    } else {
        if (!boost::filesystem::create_directory(output_directory)) {
            throw StreamIngesterException(fmt::format("Failed to create directory {}", output_directory));
        }
    }

    this->_redis = internal::Redis::Create(connection);

    auto func = boost::bind(&StreamIngester::ingest_single, this, boost::placeholders::_1);
    _pool = make_unique<IngesterThreadPool<string, StreamIngestionResult>>(4, func);
}

void StreamIngester::Ingest() {
    auto stream_names = _redis->ListStreamNames();

    if (stream_names.empty()) {
        spdlog::info("No streams found to persist.");
        return;
    }

    for (const auto &stream_name : stream_names) {
        bool should_include = false;
        for (const auto &settings_pair : stream_settings_by_name_glob_) {
            if (std::regex_match(stream_name, settings_pair.first)) {
                should_include = true;
                break;
            }
        }

        if (!should_include) {
            spdlog::info("Stream {} did not match any settings. Skipping.", stream_name);
            continue;
        }

        bool did_enqueue = false;
        {
            lock_guard<mutex> lock(_streams_in_progress_mtx);
            if (_streams_in_progress.find(stream_name) == _streams_in_progress.end()) {
                spdlog::info("Stream {} enqueued.", stream_name);
                _streams_in_progress.insert(stream_name);
                did_enqueue = true;
            }
        }

        if (did_enqueue) {
            _pool->enqueue_stream(stream_name);
        }
    }
}

void StreamIngester::Stop() {
    _pool->stop();
}


boost::optional<boost::variant<exception, StreamIngestionResult>>
StreamIngester::GetResult(const string &stream_name) {
    boost::variant<exception, StreamIngestionResult> ret;
    bool present = _pool->visit_result(stream_name,
                                       [&](const exception &e) {
                                           ret = e;

                                           spdlog::error("Exception thrown! {}", e.what());
                                       },
                                       [&](const StreamIngestionResult &r) {
                                           ret = r;
                                       });
    if (!present) {
        return boost::none;
    } else {
        return ret;
    }
}

StreamIngestionResult StreamIngester::ingest_single(string stream_name) {
    if (*_terminated) {
        return StreamIngestionResult::IN_PROGRESS;
    }

    spdlog::info("Starting ingestion of stream {} [output directory {}]", stream_name, _output_directory);

    StreamIngestionSettings settings;
    for (const auto& pair : stream_settings_by_name_glob_) {
        if (std::regex_match(stream_name, pair.first)) {
            settings = pair.second;
            break;
        }
    }
    try {
        auto ingester = internal::SingleStreamIngester(_connection,
                                                       stream_name,
                                                       _output_directory,
                                                       _terminated,
                                                       _stalled_timeout_ms,
                                                       _stale_period_ms,
                                                       settings);
        auto ret = ingester.Ingest();
        {
            lock_guard<mutex> lock(_streams_in_progress_mtx);
            _streams_in_progress.erase(stream_name);
        }
        return ret;
    } catch (const exception& e) {
        {
            lock_guard<mutex> lock(_streams_in_progress_mtx);
            _streams_in_progress.erase(stream_name);
        }
        throw;
    }
}

namespace internal {
static shared_ptr<arrow::Schema> to_arrow(const std::vector<river::FieldDefinition> &field_definitions);
template<class ArrayT>
static shared_ptr<ArrayT> get_last(const shared_ptr<arrow::Table> &table, const string &column_name, int *chunk_idx);

SingleStreamIngester::SingleStreamIngester(const RedisConnection &connection,
                                           const string &stream_name,
                                           const string &output_directory,
                                           bool *terminated,
                                           int stalled_timeout_ms,
                                           int stale_period_ms,
                                           StreamIngestionSettings settings)
        : _connection(connection),
          _stalled_timeout_ms(stalled_timeout_ms),
          _stale_period_ms(stale_period_ms),
          stream_name_(stream_name),
          should_ingest(true),
          settings_(std::move(settings)),
          _terminated(terminated) {
    this->reader = std::make_unique<StreamReader>(connection);
    this->reader->Initialize(stream_name);
    auto local_schema = reader->schema();
    this->schema = make_unique<StreamSchema>(local_schema);

    parent_directory = boost::filesystem::path(output_directory) / boost::filesystem::path(stream_name);
    // Create the directory if necessary
    if (boost::filesystem::exists(parent_directory)) {
        if (!boost::filesystem::is_directory(parent_directory)) {
            throw StreamIngesterException(
                    fmt::format("Non-directory filepath given: {}", parent_directory.make_preferred().string()));
        }
    } else {
        if (!boost::filesystem::create_directory(parent_directory)) {
            throw StreamIngesterException(
                    fmt::format("Failed to create directory {}", parent_directory.make_preferred().string()));
        }
    }
}

template<class DataT, class BuilderT>
void write_to_numeric_array(const int64_t row_group_size,
                            const int within_sample_offset,
                            const int *sizes,
                            const char *read_buffer,
                            shared_ptr<arrow::Array> &array) {
    BuilderT builder;
    int64_t column_data_index = 0;
    for (int64_t j = 0; j < row_group_size; j++) {
        PARQUET_THROW_NOT_OK(builder.AppendValues(
                reinterpret_cast<const DataT *>(read_buffer + within_sample_offset + column_data_index),
                1));
        column_data_index += sizes[j];
    }
    PARQUET_THROW_NOT_OK(builder.Finish(&array));
}

StreamIngestionResult SingleStreamIngester::Ingest() {
    append_metadata(StreamIngestionResult::IN_PROGRESS);

    int sample_size = schema->sample_size();
    int64_t samples_per_row_group = std::max(int64_t{0}, (int64_t) (settings_.bytes_per_row_group / sample_size));
    int64_t samples_per_read = settings_.samples_per_read;

    vector<int64_t> data_indices(samples_per_row_group);
    vector<char> read_buffer(sample_size * samples_per_row_group);
    vector<int> sizes(samples_per_row_group);
    vector<string> keys(samples_per_row_group);

    // Determine the next index for the file by looking at the current directory.
    int file_data_index;
    string last_key;
    int64_t global_offset;
    read_existing_files(&file_data_index, &last_key, &global_offset);

    // Seek the reader to the last key that we persisted, and continue from there.
    int64_t seek_ret = reader->Seek(last_key);
    if (seek_ret < 0) {
        throw StreamIngesterException(fmt::format("#seek() returned -1? For key {}", last_key));
    }

    StreamIngestionResult ingestion_status = StreamIngestionResult::IN_PROGRESS;
    int log_idx = 0;
    while (should_ingest && !(*_terminated)) {
        if (log_idx % 10 == 0) {
            spdlog::info("New loop for stream {}", stream_name_);
        }
        log_idx++;

        int64_t row_group_size = 0;
        int log_idx2 = 0;
        string eof_key;
        while (should_ingest && !(*_terminated) && row_group_size < samples_per_row_group) {
            if (log_idx2 % 500 == 0) {
                spdlog::info("Fetching new samples. Size {} for stream {}", row_group_size, stream_name_);
            }
            log_idx2++;
            int64_t remaining_samples_in_row_group = samples_per_row_group - row_group_size;
            auto samples_to_read = remaining_samples_in_row_group > samples_per_read
                                   ? samples_per_read
                                   : remaining_samples_in_row_group;

            int *sizes_ptr = &sizes[row_group_size];
            string *keys_ptr = &keys[row_group_size];
            int64_t num_read = reader->ReadBytes(&read_buffer[row_group_size * sample_size],
                                                 samples_to_read,
                                                 &sizes_ptr,
                                                 &keys_ptr,
                                                 static_cast<int>(_stalled_timeout_ms));
            if (num_read == 0) {
                spdlog::info("Stream {} has stalled; no responses after {} ms [file index {}].",
                             stream_name_, _stalled_timeout_ms, file_data_index);
                should_ingest = false;
                break;
            } else if (num_read < 0) {
                should_ingest = false;
                ingestion_status = StreamIngestionResult::COMPLETED;
                eof_key = reader->eof_key();
                spdlog::info("EOF encountered in stream {}, num_read=0, global_offset={}", stream_name_, global_offset);
                break;
            }

            for (int64_t i = 0; i < num_read; i++) {
                data_indices[row_group_size + i] = global_offset + i;
            }

            row_group_size += num_read;
            global_offset += num_read;
        }

        if (row_group_size > 0) {
            const string &this_data_filepath = data_filepath(file_data_index);
            if (boost::filesystem::exists(this_data_filepath)) {
                spdlog::info("Filepath {} already exists. Refusing to overwrite any files.", this_data_filepath);
                throw StreamIngesterException(
                        fmt::format("Data file already exists; we will not overwrite. File={}", this_data_filepath));
            }

            spdlog::info("Creating batch of length {}. Total offset is now {}", row_group_size, global_offset);

            std::vector<std::shared_ptr<arrow::Array>> arrays;

            // 1. Write sample_index
            arrow::Int64Builder sample_idx_builder;
            PARQUET_THROW_NOT_OK(sample_idx_builder.AppendValues(&data_indices.front(), row_group_size));
            std::shared_ptr<arrow::Array> sample_idx_array;
            PARQUET_THROW_NOT_OK(sample_idx_builder.Finish(&sample_idx_array));
            arrays.push_back(sample_idx_array);

            spdlog::info("Successfully created sample_indices.");

            // 2. Write keys
            arrow::StringBuilder keys_builder;
            for (auto it = keys.begin(); it < keys.begin() + row_group_size; it++) {
                PARQUET_THROW_NOT_OK(keys_builder.Append(*it));
            }
            std::shared_ptr<arrow::Array> keys_array;
            PARQUET_THROW_NOT_OK(keys_builder.Finish(&keys_array));
            arrays.push_back(keys_array);
            spdlog::info("Successfully created keys.");

            // 3. Write timestamp_ms (derived from keys)
            arrow::Int64Builder timestamps_builder;
            for (auto it = keys.begin(); it < keys.begin() + row_group_size; it++) {
                auto timestamp_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(KeyTimestamp(it->c_str()));
                PARQUET_THROW_NOT_OK(timestamps_builder.Append(timestamp_ms.time_since_epoch().count()));
            }
            std::shared_ptr<arrow::Array> timestamps_array;
            PARQUET_THROW_NOT_OK(timestamps_builder.Finish(&timestamps_array));
            arrays.push_back(timestamps_array);
            spdlog::info("Successfully created timestamps.");

            // 4. All the data fields given in the schema, but filtered according to settings.
            int within_sample_offset = 0;
            auto field_definitions_filtered = settings_.Filter(schema->field_definitions);
            for (const auto &field: field_definitions_filtered) {
                std::shared_ptr<arrow::Array> column_array;
                switch (field.type) {
                    case FieldDefinition::DOUBLE: {
                        write_to_numeric_array<double, arrow::DoubleBuilder>(
                                row_group_size, within_sample_offset, &sizes.front(), &read_buffer.front(), column_array);
                        break;
                    }
                    case FieldDefinition::FLOAT: {
                        write_to_numeric_array<float, arrow::FloatBuilder>(
                                row_group_size, within_sample_offset, &sizes.front(), &read_buffer.front(), column_array);
                        break;
                    }
                    case FieldDefinition::INT16: {
                        write_to_numeric_array<int16_t, arrow::Int16Builder>(
                                row_group_size, within_sample_offset, &sizes.front(), &read_buffer.front(), column_array);
                        break;
                    }
                    case FieldDefinition::INT32: {
                        write_to_numeric_array<int32_t, arrow::Int32Builder>(
                                row_group_size, within_sample_offset, &sizes.front(), &read_buffer.front(), column_array);
                        break;
                    }
                    case FieldDefinition::INT64: {
                        write_to_numeric_array<int64_t, arrow::Int64Builder>(
                                row_group_size, within_sample_offset, &sizes.front(), &read_buffer.front(), column_array);
                        break;
                    }
                    case FieldDefinition::FIXED_WIDTH_BYTES: {
                        arrow::FixedSizeBinaryBuilder builder(arrow::fixed_size_binary(field.size));
                        int64_t column_data_index = 0;
                        for (int64_t j = 0; j < row_group_size; j++) {
                            PARQUET_THROW_NOT_OK(builder.AppendValues(
                                    reinterpret_cast<uint8_t *>(&read_buffer[within_sample_offset + column_data_index]),
                                    1));
                            column_data_index += sizes[j];
                        }
                        PARQUET_THROW_NOT_OK(builder.Finish(&column_array));
                        break;
                    }
                    case FieldDefinition::VARIABLE_WIDTH_BYTES: {
                        arrow::BinaryBuilder builder;
                        int64_t column_data_index = 0;
                        for (int64_t j = 0; j < row_group_size; j++) {
                            PARQUET_THROW_NOT_OK(builder.Append(
                                    reinterpret_cast<const uint8_t *>(&read_buffer[within_sample_offset + column_data_index]),
                                    sizes[j]));
                            column_data_index += sizes[j];
                        }
                        PARQUET_THROW_NOT_OK(builder.Finish(&column_array));
                        break;
                    }
                    default:
                        throw StreamIngesterException("Unhandled field type");
                }

                within_sample_offset += field.size;
                arrays.push_back(column_array);
                spdlog::info("Successfully created column array for field {}", field.name);
            }

            auto arrow_schema = to_arrow(field_definitions_filtered);
            shared_ptr<arrow::Table> table = arrow::Table::Make(arrow_schema, arrays);

            boost::filesystem::path temp = parent_directory / boost::filesystem::unique_path();
            spdlog::info("Writing file to temp filepath {}...", temp.make_preferred().string());
            write_parquet_file(temp.make_preferred().string(), *table);
            spdlog::info("Successfully wrote to file path. Renaming temporary file {} to final path: {}",
                         temp.make_preferred().string(), this_data_filepath);
            boost::filesystem::rename(temp, this_data_filepath);
            spdlog::info("Successfully moved temporary file {} to final path: {}",
                         temp.make_preferred().string(), this_data_filepath);
            file_data_index++;
        }

        if (!eof_key.empty()) {
            combine_all_files();
            append_metadata(ingestion_status);
            delete_up_to(eof_key);
        } else if (row_group_size > 0) {
            string last_key_persisted = string(keys[row_group_size - 1]);
            delete_up_to(last_key_persisted);
        }
    }

    reader->Stop();

    if (ingestion_status == StreamIngestionResult::IN_PROGRESS && !(*_terminated)) {
        add_eof_if_necessary();
    }
    return ingestion_status;
}

void SingleStreamIngester::delete_up_to(const string& last_key_persisted) {
    class SeekListener : public internal::StreamReaderListener {
    public:
        vector <pair<string, string>> stream_keys_to_delete_for_live;
        string last_stream_key;

        void OnStreamKeyChange(const string &old_stream_key, const string &new_stream_key) override {
            if (!old_stream_key.empty()) {
                // For a live stream (i.e. no EOF ingested yet), we only want to delete streams that are wholly behind
                // the given key.
                stream_keys_to_delete_for_live.emplace_back(old_stream_key, new_stream_key);
            }
            last_stream_key = new_stream_key;
        };

        ~SeekListener() = default;
    };

    unique_ptr<StreamReader> seek_reader = make_unique<StreamReader>(_connection);
    auto listener = make_unique<SeekListener>();
    seek_reader->AddListener(listener.get());
    seek_reader->Initialize(stream_name_);
    int64_t seek_ret = seek_reader->Seek(last_key_persisted);
    bool is_eof = seek_ret < 0;

    vector<pair<string, string>> stream_keys_to_delete = listener->stream_keys_to_delete_for_live;
    if (is_eof) {
        stream_keys_to_delete.emplace_back(listener->last_stream_key, "");
    }

    if (stream_keys_to_delete.empty()) {
        spdlog::info("Nothing to delete for stream {} up to key {}", stream_name_, last_key_persisted);
        return;
    }

    auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now() - KeyTimestamp(last_key_persisted.c_str()));
    if (elapsed_seconds.count() > settings_.minimum_age_seconds_before_deletion) {
        long long to_sleep = settings_.minimum_age_seconds_before_deletion - elapsed_seconds.count() + 1;
        if (to_sleep > 0) {
            spdlog::info(
                    "Sleeping for {} seconds until we can delete up to this key.", to_sleep);
            std::this_thread::sleep_for(chrono::seconds(to_sleep));
        }
    }

    unique_ptr<internal::Redis> redis = internal::Redis::Create(_connection);
    for (const pair<string, string> &to_del : stream_keys_to_delete) {
        const string &stream_key_to_del = to_del.first;
        const string &stream_key_following = to_del.second;

        if (!stream_key_following.empty()) {
            // Change the first_stream_key in the metadata to the one after this, so we always have an intact readable
            // stream.
            redis->SetMetadata(stream_name_, {{"first_stream_key", stream_key_following}});
            spdlog::info("First_stream_key changed to {}.", stream_key_following);
        }

        redis->Unlink(stream_key_to_del);
        spdlog::info("Stream key {} deleted.", stream_key_to_del);
    }

    if (is_eof) {
        redis->DeleteMetadata(stream_name_);
        spdlog::info("Stream metadata for {} deleted.", stream_name_);
    }
}

void SingleStreamIngester::add_eof_if_necessary() {
    class TailListener : public internal::StreamReaderListener {
    public:
        string last_stream_key;

        void OnStreamKeyChange(const string &, const string &new_stream_key) override {
            last_stream_key = new_stream_key;
        };

        ~TailListener() = default;
    };

    unique_ptr<StreamReader> tail_reader = make_unique<StreamReader>(_connection);
    auto listener = make_unique<TailListener>();
    tail_reader->AddListener(listener.get());
    tail_reader->Initialize(stream_name_);
    const int bufsize = tail_reader->schema().sample_size();
    vector<char> buf(bufsize);
    char key[512];
    int64_t sample_index;
    int64_t num_read = tail_reader->TailBytes(&buf.front(), 1000, key, &sample_index);
    if (num_read < 0) {
        // There's already an EOF, nothing to do here.
        return;
    }

    if (num_read == 0) {
        // We timed out OR we've consumed everything; can't tell.
        spdlog::info("No elements read; cannot differentiate between an empty stream and repeated timeouts, so not "
                     "doing anything with stream {}", stream_name_);
        return;
    }

    uint64_t last_sample_written_at_ms, b;
    internal::DecodeCursor(key, &last_sample_written_at_ms, &b);

    unique_ptr<internal::Redis> redis = internal::Redis::Create(_connection);
    int64_t time_us = redis->TimeUs();

    int64_t elapsed_us = time_us - last_sample_written_at_ms * 1000;
    if (elapsed_us > _stale_period_ms * 1000) {
        auto reply = redis->Xadd(listener->last_stream_key, {
            {"eof", "1",},
            {"sample_index", fmt::format_int(sample_index).str()}});
        spdlog::info("Forcibly added an EOF to stream {} at key {} [stream was {} seconds old]",
                     stream_name_, reply->str, elapsed_us / 1000000.0);
    }
}

vector<boost::filesystem::path> SingleStreamIngester::list_existing_files() {
    vector<boost::filesystem::path> paths;
    for (auto it = boost::filesystem::directory_iterator(parent_directory);
         it != boost::filesystem::directory_iterator();
         it++) {
        boost::filesystem::path path = it->path();
        string stem = path.stem().make_preferred().string();
        if (boost::starts_with(stem, "data_") && path.extension().compare(".parquet") == 0) {
            paths.push_back(path);
        }
    }
    return paths;
}

void SingleStreamIngester::combine_all_files() {
    vector<boost::filesystem::path> p = this->list_existing_files();

    if (p.empty()) {
        spdlog::info("No previous files found in directory {}. Nothing to do.",
                     parent_directory.make_preferred().string());
        return;
    }

    const string &this_data_filepath = combined_data_filepath();
    if (boost::filesystem::exists(this_data_filepath)) {
        spdlog::info("Combined filepath {} already exists. Refusing to overwrite any files.", this_data_filepath);
        throw StreamIngesterException(
                fmt::format("Combined file already exists; we will not overwrite. File={}", this_data_filepath));
    }

    string temp_filepath = (parent_directory / boost::filesystem::unique_path()).make_preferred().string();

    std::vector<std::shared_ptr<arrow::Array>> arrays;

#ifdef PARQUET_ASSIGN_OR_THROW
    PARQUET_ASSIGN_OR_THROW(
            shared_ptr<arrow::io::OutputStream> sink, arrow::io::FileOutputStream::Open(temp_filepath));
#else
    shared_ptr<arrow::io::OutputStream> sink;
    PARQUET_THROW_NOT_OK(arrow::io::FileOutputStream::Open(temp_filepath, &sink));
#endif
    unique_ptr<parquet::arrow::FileWriter> writer;

    parquet::WriterProperties::Builder builder;
    builder.compression(parquet::Compression::SNAPPY);
    shared_ptr<parquet::WriterProperties> props = builder.build();

    spdlog::info("Beginning combining of files to temp file {}", temp_filepath);

    // Sort ascending order
    sort(p.begin(), p.end(), less<>());
    for (auto &path : p) {
        // Read the file on disk into a table:
#ifdef PARQUET_ASSIGN_OR_THROW
        PARQUET_ASSIGN_OR_THROW(
                std::shared_ptr<arrow::io::ReadableFile> infile,
                arrow::io::ReadableFile::Open(path.make_preferred().string(),
                                              arrow::default_memory_pool()));
#else
        std::shared_ptr<arrow::io::ReadableFile> infile;
            PARQUET_THROW_NOT_OK(arrow::io::ReadableFile::Open(
            path.make_preferred().string(),
            arrow::default_memory_pool(),
            &infile));
#endif

        std::unique_ptr<parquet::arrow::FileReader> file_reader;
        PARQUET_THROW_NOT_OK(
                parquet::arrow::OpenFile(infile, arrow::default_memory_pool(), &file_reader));
        std::shared_ptr<arrow::Table> table;
        PARQUET_THROW_NOT_OK(file_reader->ReadTable(&table));

        if (!writer) {
            std::shared_ptr<::arrow::Schema> arrow_schema;
            PARQUET_THROW_NOT_OK(file_reader->GetSchema(&arrow_schema));
            // Open a writer
            PARQUET_THROW_NOT_OK(parquet::arrow::FileWriter::Open(
                    *arrow_schema,
                    arrow::default_memory_pool(),
                    sink,
                    props,
                    &writer));
        }


        // And then write this table to the existing file
        int64_t num_rows = table->num_rows();
        spdlog::info("Writing contents of {} to combined data filepath ({} rows)",
                     path.make_preferred().string(), num_rows);
        PARQUET_THROW_NOT_OK(writer->WriteTable(*table.get(), num_rows));
        spdlog::info("Done writing {} rows.", num_rows);
    }

    PARQUET_THROW_NOT_OK(writer->Close());

    spdlog::info("Renaming temporary file {} to final path: {}",
                             temp_filepath, this_data_filepath);
    boost::filesystem::rename(temp_filepath, this_data_filepath);
    spdlog::info("Successfully moved temporary file {} to final path: {}",
                             temp_filepath, this_data_filepath);

    // And then we can remove all old files
    for (auto &path : p) {
        boost::filesystem::remove(path);
        spdlog::info("Removed file {}", path.make_preferred().string());
    }
}

void SingleStreamIngester::read_existing_files(int *next_data_filepath_index, string *last_key, int64_t *global_index) {
    vector<boost::filesystem::path> p = this->list_existing_files();

    if (p.empty()) {
        *next_data_filepath_index = 0;
        *last_key = "0-0";
        *global_index = 0;
        spdlog::info("No previous files found in directory {}. Starting from the start.",
                     parent_directory.make_preferred().string());
        return;
    }

    // Sort descending order
    sort(p.begin(), p.end(), greater<>());
    boost::filesystem::path last_path = p.at(0);
    string stem = last_path.stem().make_preferred().string();
    stem = regex_replace(stem, regex("^data_"), "");
    int64_t last_index = strtoll(stem.c_str(), nullptr, 10);
    *next_data_filepath_index = last_index + 1;

    // Read the last file on disk, and find the greatest "sample_index" it is at.
#ifdef PARQUET_ASSIGN_OR_THROW
    PARQUET_ASSIGN_OR_THROW(
            std::shared_ptr<arrow::io::ReadableFile> infile,
            arrow::io::ReadableFile::Open(last_path.make_preferred().string(),
                                          arrow::default_memory_pool()));
#else
    std::shared_ptr<arrow::io::ReadableFile> infile;
    PARQUET_THROW_NOT_OK(arrow::io::ReadableFile::Open(
            last_path.make_preferred().string(),
            arrow::default_memory_pool(),
            &infile));
#endif

    std::unique_ptr<parquet::arrow::FileReader> file_reader;
    PARQUET_THROW_NOT_OK(
            parquet::arrow::OpenFile(infile, arrow::default_memory_pool(), &file_reader));
    std::shared_ptr<arrow::Table> table;
    PARQUET_THROW_NOT_OK(file_reader->ReadTable(&table));

    int key_idx;
    auto key_array = get_last<arrow::StringArray>(table, "key", &key_idx);

    int sample_index_idx;
    auto sample_index_array = get_last<arrow::Int64Array>(table, "sample_index", &sample_index_idx);

    if (!key_array || !sample_index_array) {
        spdlog::info("No data found in the loaded table from file {}.", last_path.make_preferred().string());
        *last_key = "0-0";
        *global_index = 0;
        return;
    }

    *last_key = key_array->GetString(key_idx);
    *global_index = sample_index_array->Value(sample_index_idx) + 1;

    spdlog::info("Starting from existing files. last_key={}, global_index={}, found in filename {}. "
                 "new data file index {}",
                 *last_key,
                 *global_index,
                 last_path.make_preferred().string(),
                 *next_data_filepath_index);
}

template<class ArrayT>
inline shared_ptr<ArrayT> get_last(const shared_ptr<arrow::Table> &table, const string &column_name, int *idx) {
    auto key_column = table->GetColumnByName(column_name);
    if (key_column->length() == 0 || key_column->num_chunks() == 0) {
        *idx = 0;
        return nullptr;
    }

    auto last_chunk = key_column->chunk(key_column->num_chunks() - 1);
    auto array = std::static_pointer_cast<ArrayT>(last_chunk);
    *idx = array->length() - 1;
    return array;
}

shared_ptr<arrow::Schema> to_arrow(const std::vector<river::FieldDefinition> &field_definitions) {
    vector<std::shared_ptr<arrow::Field>> fields;
    fields.push_back(arrow::field("sample_index", arrow::int64(), false));
    fields.push_back(arrow::field("key", arrow::utf8(), false));
    fields.push_back(arrow::field("timestamp_ms", arrow::int64(), false));

    for (auto &field : field_definitions) {
        shared_ptr<arrow::DataType> type;
        switch (field.type) {
            case FieldDefinition::DOUBLE:
                type = arrow::float64();
                break;
            case FieldDefinition::FLOAT:
                type = arrow::float32();
                break;
            case FieldDefinition::INT16:
                type = arrow::int16();
                break;
            case FieldDefinition::INT32:
                type = arrow::int32();
                break;
            case FieldDefinition::INT64:
                type = arrow::int64();
                break;
            case FieldDefinition::FIXED_WIDTH_BYTES:
                type = arrow::fixed_size_binary(field.size);
                break;
            case FieldDefinition::VARIABLE_WIDTH_BYTES:
                type = arrow::binary();
                break;
            default:
                throw StreamIngesterException("Unhandled data type! ");
        }
        fields.push_back(arrow::field(field.name, type, false));
    }

    return arrow::schema(fields);
}

void SingleStreamIngester::append_metadata(StreamIngestionResult result) {
    boost::property_tree::ptree root;
    const string &filename = metadata_filepath();
    if (boost::filesystem::exists(filename)) {
        boost::property_tree::read_json(filename, root);
    }

    for (const auto &it : reader->Metadata()) {
        root.put(it.first, it.second);
    }
    root.put("stream_name", stream_name_);
    root.put("local_minus_server_clock_us", std::to_string(reader->local_minus_server_clock_us()));
    root.put("initialized_at_us", std::to_string(reader->initialized_at_us()));

    string result_str;
    switch (result) {
        case IN_PROGRESS:
            result_str = "IN_PROGRESS";
            break;
        case COMPLETED:
            result_str = "COMPLETED";
            break;
    }
    root.put("ingestion_status", result_str);

    std::ofstream ofs(filename, std::ofstream::out);
    boost::property_tree::write_json(ofs, root);
}

string SingleStreamIngester::metadata_filepath() {
    return (parent_directory / boost::filesystem::path("metadata.json")).make_preferred().string();
}

string SingleStreamIngester::combined_data_filepath() {
    return (parent_directory / boost::filesystem::path("data.parquet")).make_preferred().string();
}

string SingleStreamIngester::data_filepath(int index) {
    return (parent_directory / boost::filesystem::path(fmt::format("data_{:0>10}.parquet", index)))
            .make_preferred().string();
}

void SingleStreamIngester::write_parquet_file(const string &filepath, const arrow::Table& table) {
    if (boost::filesystem::exists(filepath)) {
        throw StreamIngesterException(
                fmt::format("Data file already exists; we will not overwrite. File={}", filepath));
    }

#ifdef PARQUET_ASSIGN_OR_THROW
    PARQUET_ASSIGN_OR_THROW(
            shared_ptr<arrow::io::OutputStream> sink, arrow::io::FileOutputStream::Open(filepath));
#else
    shared_ptr<arrow::io::OutputStream> sink;
    PARQUET_THROW_NOT_OK(arrow::io::FileOutputStream::Open(filepath, &sink));
#endif

    parquet::WriterProperties::Builder builder;
    builder.compression(parquet::Compression::SNAPPY);
    shared_ptr<parquet::WriterProperties> props = builder.build();

    PARQUET_THROW_NOT_OK(
        parquet::arrow::WriteTable(table, arrow::default_memory_pool(), sink, 1024 * 1024 * 4, props));
    spdlog::info("Successfully wrote table to file. filepath = {}", filepath);
}
}
}
