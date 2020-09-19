#ifndef RIVER_WRITER_H
#define RIVER_WRITER_H

#include <cstdio>
#include <iostream>
#include <mutex>
#include <cstring>
#include <hiredis.h>
#include <unordered_map>
#include <memory>
#include "schema.h"
#include "redis.h"

namespace river {

class StreamWriterException : public std::exception {
 public:
    explicit StreamWriterException(const std::string& message) {
        std::stringstream s;
        s << "[StreamWriter Exception] " << message;
        _message = s.str();
    }

    const char *what() const noexcept override {
        return _message.c_str();
    }

private:
    std::string _message;
};

class StreamExistsException : public StreamWriterException {
    using StreamWriterException::StreamWriterException;
};

/**
 * The main entry point for River for writing a new stream. Streams are defined by a schema and a stream name, both of
 * which are given in the `initialize()` call. All samples written to this stream must belong to the same schema. Once
 * there are no more elements in this stream, call `stop()`; this will signal to any other readers that the stream has
 * ended.
 */
class StreamWriter {
public:
    /**
     * Construct an instance of StreamWriter. One StreamWriter belongs to at most one stream.
     *
     * @param connection Parameters to connect to Redis.
     * @param batch_size Number of samples in a batch that will be written/read from redis. Increasing this
     * number makes batches bigger and thus reduces the number of writes/reads to redis, but then also increases the
     * average latency of the stream.
     * @param keys_per_redis_stream: the number of keys in each underlying redis stream. Default value reasoning is:
     * 2^24 = 17M keys per stream => ~350MB of memory on 64-bit redis with 8-byte fields
     */
    explicit StreamWriter(const RedisConnection &connection,
                          int64_t keys_per_redis_stream = int64_t{1LL << 24},
                          int batch_size = 1536);

    /**
     * Initialize this stream for writing. The given stream name must be unique within the Redis used. This
     * initialization puts necessary information (e.g. schemas and timestamps) into redis. Optionally, it can accept
     * an unordered_map of user metadata to put in to Redis atomically.
     */
    void Initialize(const std::string &stream_name,
                    const StreamSchema &schema,
                    const std::unordered_map<std::string, std::string> &user_metadata =
                    std::unordered_map<std::string, std::string>());

    /**
     * Writes data to the stream. The given data buffer of type DataT will be recast to a raw (e.g. char *) array and
     * written to redis according to each sample size. If the schema has only fixed-width fields, then the data buffer
     * will be advanced according to the fixed-width size given in #initialize(); otherwise (i.e. if it has variable-
     * width fields), the sizes buffer is necessary to determine the size of each sample.
     */
    template <class DataT>
    void Write(DataT *data, int64_t num_samples, const int *sizes = nullptr) {
        if (!this->has_variable_width_field_ && sizeof(data[0]) != sample_size_) {
            throw StreamWriterException("Sample size that was given is not equal to the data!");
        }
        WriteBytes(reinterpret_cast<const char *>(data), num_samples, sizes);
    }

    /**
     * Writes raw bytes to the stream. For fixed-width fields, each sample will be assumed to be of the size defined in
     * the schema from initialize(); otherwise, for variable-width fields, the sizes array is necessary.
     */
    void WriteBytes(const char *data, int64_t num_samples, const int *sizes = nullptr);

    /**
     * A copy of the stream's schema that was provided on initialize().
     */
    const StreamSchema& schema();

    /**
     * The stream name belonging to this stream. Empty if it has not been initialized.
     */
    const std::string& stream_name();

    /**
     * Number of samples written to this stream since initialization.
     */
    int64_t total_samples_written();

    /**
     * Time in microseconds since epoch of when this stream was initialized, with respect to the *server* time.
     */
    int64_t initialized_at_us();

    /**
     * User metadata attached to this stream.
     */
    std::unordered_map<std::string, std::string> Metadata();

    /**
     * Sets the user metadata attached to this stream.
     */
    void SetMetadata(const std::unordered_map<std::string, std::string>& metadata);

    /**
     * Stops this stream permanently. This method must be called once the stream is finished in order to notify readers
     * that the stream has terminated.
     */
    void Stop();

private:
    int64_t ComputeLocalMinusServerClocks();

    std::unique_ptr<internal::Redis> redis_;

    const int redis_batch_size_;
    const int64_t keys_per_redis_stream_;

    std::shared_ptr<StreamSchema> schema_;
    std::string stream_name_;
    int sample_size_;
    bool has_variable_width_field_;

    int64_t total_samples_written_;
    bool is_stopped_;
    bool is_initialized_;
    int64_t initialized_at_us_;
    int last_stream_key_idx_;
};

}

#endif //RIVER_WRITER_H

