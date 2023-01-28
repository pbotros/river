//
// Created by Paul Botros on 10/28/19.
//

#ifndef PARENT_INGESTER_H
#define PARENT_INGESTER_H

#include <set>
#include <regex>
#include <parquet/api/writer.h>
#include <arrow/io/file.h>
#include <cstdlib>
#include <parquet/file_writer.h>
#include <boost/filesystem.hpp>
#include <arrow/io/file.h>
#include <arrow/util/logging.h>
#include <arrow/io/file.h>
#include <parquet/api/reader.h>
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>
#include <parquet/arrow/writer.h>
#include <parquet/properties.h>
#include <parquet/exception.h>
#include <parquet/api/writer.h>
#include <fmt/format.h>
#include <utility>
#include <chrono>
#include "ingester_settings.h"

#include "ingester_threadpool.h"
#include "river.h"

namespace river {

typedef enum StreamIngestionResult {
    COMPLETED = 0,
    IN_PROGRESS = 1
} StreamIngestionResult;


class StreamIngester {
public:
    StreamIngester(
            const RedisConnection &connection,
            const string &output_directory,
            bool *terminated,
            std::vector<std::pair<std::regex, StreamIngestionSettings>> stream_settings_by_name_glob,
            int stalled_timeout_ms = 1000,
            int stale_period_ms = 300000);

    ~StreamIngester() {
        Stop();
    }

    void Ingest();
    boost::optional<boost::variant<exception, StreamIngestionResult>> GetResult(const string& stream_name);
    void Stop();

private:
    StreamIngestionResult ingest_single(string stream_name);
    unique_ptr<internal::Redis> _redis;
    unique_ptr<IngesterThreadPool<string, StreamIngestionResult>> _pool;
    const RedisConnection _connection;
    const string _output_directory;
    bool* _terminated;
    std::vector<std::pair<std::regex, StreamIngestionSettings>> stream_settings_by_name_glob_;
    const int _stalled_timeout_ms;
    const int _stale_period_ms;
    std::set<string> _streams_in_progress;
    mutex _streams_in_progress_mtx;
};

namespace internal {
class SingleStreamIngester {
public:
    SingleStreamIngester(const RedisConnection &connection,
                         const string &stream_name,
                         const string &output_directory,
                         bool *terminated,
                         int stalled_timeout_ms,
                         int stale_period_ms,
                         StreamIngestionSettings settings);

    StreamIngestionResult Ingest();
private:
    const RedisConnection _connection;
    const int _stalled_timeout_ms;
    const int _stale_period_ms;

    const StreamIngestionSettings settings_;

    const string &stream_name_;
    boost::filesystem::path parent_directory;

    unique_ptr<StreamSchema> schema;
    unique_ptr<StreamReader> reader;
    bool should_ingest;
    bool *_terminated;

    static void write_parquet_file(const string &filepath, const arrow::Table& table);

    string combined_data_filepath();
    string data_filepath(int index);

    string metadata_filepath();

    void add_eof_if_necessary();
    void append_metadata(StreamIngestionResult result);

    vector<boost::filesystem::path> list_existing_files();
    void read_existing_files(
            int *next_data_filepath_index, string *last_key, int64_t *global_index);
    void combine_all_files();

    void delete_up_to(const string& last_key_persisted);
};
}

class StreamIngesterException : public exception {
public:
    explicit StreamIngesterException(const std::string &message) {
        std::stringstream s;
        s << "[StreamIngester Exception] " << message;
        _message = s.str();
    }

    const char *what() const noexcept override {
        return _message.c_str();
    }

private:
    std::string _message;
};

}

#endif //PARENT_INGESTER_H
