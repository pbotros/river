//
// Created by Paul Botros on 10/27/19.
//

#ifndef PARENT_READER_H
#define PARENT_READER_H

#include <iostream>
#include <exception>
#include <hiredis.h>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <vector>
#include <memory>
#include <boost/optional.hpp>
#include "schema.h"
#include "redis.h"

using namespace std;

namespace river {
namespace internal {
    class StreamReaderListener;
}

class StreamReaderException : public exception {
public:
    explicit StreamReaderException(const std::string& message) {
        std::stringstream s;
        s << "[StreamReader Exception] " << message;
        _message = s.str();
    }

    const char *what() const noexcept override {
        return _message.c_str();
    }

private:
    std::string _message;
};

class StreamDoesNotExistException : public StreamReaderException {
    using StreamReaderException::StreamReaderException;
};

/**
 * The main entry point for River for reading an existing stream. This class is initialized with a stream name
 * corresponding to an existing stream, and allows for batch consumption of the stream. Reads requesting more data than
 * is present in the stream will block. Any attempt to read into a typed buffer will be checked against the stream's
 * schema to ensure compatibility.
 *
 * After constructing a StreamReader, you must call initialize with the name of the stream you wish to read.
 */
class StreamReader {
public:
    /**
     * Construct an instance of a StreamReader. One StreamReader should be used with at most one underlying stream.
     *
     * @param connection: parameters to connect to Redis
     * @param max_fetch_size: maximum number of elements to fetch from redis at a time (to prevent untenably large
     * batches if a large number of bytes are consumed).
     */
    explicit StreamReader(const RedisConnection& connection, const int max_fetch_size = 10000);

    /**
     * Initialize this reader to a particular stream. If timeout_ms is positive, this call will wait for up to
     * `timeout_ms` milliseconds for the stream to be created. When the timeout is exceeded or if no timeout was given
     * and the stream does not exist, a StreamReaderException will be raised.
     */
    void Initialize(const string &stream_name, int timeout_ms = -1);

    /**
     * Read from the stream from where was last consumed. This call blocks until the desired number of samples is
     * available in the underlying stream. The return value indicates how many samples were written to the buffer.
     * If EOF has been reached, then #good() will return false, and any attempts to #read() will return -1.
     *
     * @tparam DataT The data type of the buffer. The `sizeof()` of this type should match the stream's sample size as
     * governed by its schema.
     * @param buffer The buffer into which data will be written. If this call successfully returns, there will be
     * exactly `num_samples` samples written into the buffer. This buffer will be treated as a raw byte array and thus
     * will write structs according to their byte order.
     * @param num_samples Number of samples to read from the underlying stream.
     * @param sizes If given, `num_samples` entries will be written into this array containing the sizes of each
     * corresponding sample. Particularly useful for VARIABLE_WIDTH_BYTES fields. Pass nullptr to ignore.
     * @return the number of elements read. Should always be num_samples.
     */
    template<class DataT>
    int64_t Read(DataT *buffer,
                 int64_t num_samples,
                 int **sizes = nullptr,
                 string **keys = nullptr,
                 int timeout_ms = -1) {
        if (sizeof(buffer[0]) != sample_size_) {
            throw StreamReaderException("Buffer given was not the same size as what's stored in metadata.");
        }
        return ReadBytes(reinterpret_cast<char *>(buffer), num_samples, sizes, keys, timeout_ms);
    }

    /**
     * Read from the stream from where was last consumed. This call blocks until the desired number of samples is
     * available in the underlying stream. The return value indicates how many samples were written to the buffer.
     * If EOF has been reached, then #good() will return false, and any attempts to #read() will return -1.
     *
     * @param buffer The buffer into which data will be written. If this call successfully returns, there will be
     * exactly `num_samples` samples written into the buffer, each of which is `sample_size` as told by the schema. For
     * VARIABLE_WIDTH_BYTES fields, ensure this buffer is large enough to capture the maximum possible size of
     * `num_samples` samples.
     * @param num_samples Number of samples to read from the underlying stream.
     * @param sizes If given, `num_samples` entries will be written into this array containing the sizes of each
     * corresponding sample. Particularly useful for VARIABLE_WIDTH_BYTES fields. Pass nullptr to ignore.
     * @param keys If given, `num_samples` `char *` entries will be written into this array containing the
     * NULL-terminated unique string keys in the underlying database. Pass nullptr to ignore.
     * @param timeout_ms If positive, the maximum length of time this entire call can block while waiting for samples.
     * After the timeout, the stream can be partially read, and the return value is needed to determine samples read.
     * @return the number of elements read. If no timeout is given, this will always be num_samples. If there is a
     * timeout, this could be a partially read buffer and so can be less than num_samples.
     */
    int64_t ReadBytes(
            char *buffer,
            int64_t num_samples,
            int **sizes = nullptr,
            string **keys = nullptr,
            int timeout_ms = -1);

    /**
     * Returns the last element in the stream after the previously seen elements. Blocks until there's at least one
     * element available in the stream after the current cursor.
     * @param timeout_ms If positive, the maximum length of time this entire call can block while waiting for a sample.
     * After the timeout there can be 0 or 1 elements read, and so the return value is needed to determine samples read.
     * @return the size of the element read into the buffer. 0 if nothing was read (i.e. due to timeout).
     */
    template<class DataT>
    int64_t Tail(DataT *buffer,
                 int timeout_ms = -1,
                 char *key = nullptr,
                 int64_t *sample_index = nullptr) {
        if (sizeof(buffer[0]) != sample_size_) {
            throw StreamReaderException("Buffer given was not the same size as what's stored in metadata.");
        }
        return TailBytes(reinterpret_cast<char *>(buffer), timeout_ms, key, sample_index);
    }

    /**
     * Returns the last element in the stream after the previously seen elements. Blocks until there's at least one
     * element available in the stream after the current cursor.
     * @param timeout_ms If positive, the maximum length of time this entire call can block while waiting for a sample.
     * After the timeout there can be 0 or 1 elements read, and so the return value is needed to determine samples read.
     * @return the size of the element read into the buffer. 0 if nothing was read (i.e. due to timeout).
     */
    int64_t TailBytes(char *buffer,
                      int timeout_ms = -1,
                      char *key = nullptr,
                      int64_t *sample_index = nullptr);

    /**
     * Seeks the internal cursor to the given key. Any elements returned by read/tail will be *after* this element.
     *
     * If the key that's given is in the past -- i.e., this StreamReader has already consumed past this key -- then
     * the cursor will not be moved, and no exception will be thrown.
     */
    int64_t Seek(const string &key);

    /**
     * Whether this stream has been initialized.
     */
    bool is_initialized() {
        return is_initialized_;
    }

    /**
     * Whether this stream is "good" for reading (similar to std::ifstream's #good()). Synonymous with casting to bool.
     */
    bool Good() const {
        return is_initialized_ && !is_eof_ && !is_stopped_;
    }

    /**
     * Synonym for #good().
     */
    explicit operator bool() const {
        return Good();
    }

    /**
     * If the stream has reached EOF, this will be the key that contained the EOF signal.
     * @return
     */
    boost::optional<string> eof_key() {
        return eof_key_.empty() ? boost::optional<string>() : boost::optional<string>(eof_key_);
    }

    /**
     * Time in microseconds since epoch of when this stream was initialized, with respect to the *server* time.
     */
    int64_t initialized_at_us() {
        return initialized_at_us_;
    }

    /**
     * Number of samples that have been read since initialization of this stream.
     */
    int64_t total_samples_read() {
        return num_samples_read_;
    }

    /**
     * Add a listener to this reader. Can be called at any point, even before initialization of the stream. See
     * StreamReaderListener for more details.
     */
    void AddListener(internal::StreamReaderListener *listener);

    /**
     * The schema of this stream; only valid after #initialize() has been called. This schema contains useful
     * information on the fields, in particular the name, order, and size of the fields within a single sample of this
     * stream.
     */
    const StreamSchema& schema();

    const string& stream_name() {
        return stream_name_;
    }

    /**
     * User metadata attached to this stream.
     */
    unordered_map<string, string> Metadata();

    /**
     * Get the difference between the "local" clock with respect to the *WRITER* of the stream and the server clock,
     * i.e. the redis server. Difference returned in microseconds.
     */
    int64_t local_minus_server_clock_us();

    /**
     * Stops this reader from being used in the future. Redis connections are freed; read() will no longer work; good()
     * will return false.
     */
    void Stop();

private:
    unique_ptr<internal::Redis> redis_;

    const int max_fetch_size_;

    string stream_name_;
    string current_stream_key_;
    shared_ptr<StreamSchema> schema_;
    int64_t initialized_at_us_{};
    int64_t local_minus_server_clock_us_{};
    bool has_variable_width_field_{};

    vector<internal::StreamReaderListener *> listeners_;

    int sample_size_;

    typedef struct RedisCursor {
        uint64_t left;
        uint64_t right;
    } RedisCursor;
    RedisCursor cursor_;
    int64_t num_samples_read_;

  void FireStreamKeyChange(const string &old_stream_key, const string &new_stream_key);

    boost::optional<unordered_map<string, string>> RetryablyFetchMetadata(const string &stream_name, int timeout_ms);
    boost::optional<string> ErrorMsgIfNotGood();

    bool is_stopped_;
    bool is_initialized_;
    bool is_eof_;
    string eof_key_;

    inline void IncrementCursorFrom(const char *key) {
        // Increment the LSB part of the cursor for the next fetch
        internal::DecodeCursor(key, &cursor_.left, &cursor_.right);
        cursor_.right++;
    }

    static inline const char *FindField(const redisReply *element, const char *field_name, int *len = nullptr) {
        for (unsigned int j = 0; j < element->elements; j += 2) {
            if (strcmp(element->element[j]->str, field_name) == 0) {
                if (len != nullptr) {
                    *len = element->element[j + 1]->len;
                }
                return element->element[j + 1]->str;
            }
        }
        return nullptr;
    }
};

namespace internal {
    /**
     * Listener for internals that occur with the stream. Contains details related to the underlying Redis structure of
     * the stream.
     */
    class StreamReaderListener {
    public:
      /**
       * Called whenever the underlying stream key in redis is changed (i.e. from a tombstoning or EOF).
       *
       * @param old_stream_key: the previous stream key. Can be empty if we are just starting to read the stream.
       * @param new_stream_key: the stream key to which we changed. Can be empty if we hit an EOF.
       */
      virtual void OnStreamKeyChange(const string &old_stream_key, const string &new_stream_key) = 0;

        virtual ~StreamReaderListener() = default;
    };
}
}

#endif //PARENT_READER_H
