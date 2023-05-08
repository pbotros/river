#include "reader.h"
#include "redis.h"
#include <thread>
#include <algorithm>
#include <fmt/format.h>
#include <glog/logging.h>


namespace river {

using namespace std;

StreamReader::StreamReader(const RedisConnection &connection, const int max_fetch_size)
        : max_fetch_size_(max_fetch_size), cursor_(RedisCursor()), current_sample_idx_(-1), num_samples_read_(0) {
    this->redis_ = internal::Redis::Create(connection);

    this->cursor_.left = 0;
    this->cursor_.right = 0;

    this->is_initialized_ = false;
    this->is_stopped_ = false;
    this->is_eof_ = false;
    this->eof_key_ = "";
    this->sample_size_ = -1;

    if (max_fetch_size_ <= 0) {
        throw StreamReaderException("Invalid max fetch size given, needs to be positive.");
    }
}

void StreamReader::Initialize(const std::string &stream_name, int timeout_ms) {
    if (this->is_stopped_) {
        throw StreamReaderException("Reader is already stopped; cannot initialize a stopped stream.");
    }
    if (this->is_initialized_) {
        return;
    }

    unique_ptr<unordered_map<std::string, std::string>> maybe_metadata = RetryablyFetchMetadata(stream_name, timeout_ms);
    if (!maybe_metadata) {
        throw StreamDoesNotExistException(fmt::format("Stream {} does not exist.", stream_name));
    }

    unordered_map<std::string, std::string> metadata = *maybe_metadata;

    this->current_stream_key_ = metadata["first_stream_key"];
    if (this->current_stream_key_.empty()) {
        throw StreamReaderException("first_stream_key an empty std::string!");
    }
    const StreamSchema &tmp = StreamSchema::FromJson(metadata["schema"]);
    if (metadata.find("local_minus_server_clock_us") != metadata.end()) {
        this->local_minus_server_clock_us_ = strtoll(metadata["local_minus_server_clock_us"].c_str(), nullptr, 10);
    } else {
        this->local_minus_server_clock_us_ = 0;
    }
    this->initialized_at_us_ = strtoull(metadata["initialized_at_us"].c_str(), nullptr, 10);

    this->schema_ = make_shared<StreamSchema>(tmp);
    this->has_variable_width_field_ = schema_->has_variable_width_field();
    this->sample_size_ = schema_->sample_size();
    this->stream_name_ = stream_name;
    this->is_initialized_ = true;

    FireStreamKeyChange("", current_stream_key_);
}

int64_t StreamReader::ReadBytes(
        char *buffer,
        int64_t num_samples,
        int **sizes,
        std::string **keys,
        int timeout_ms) {
    if (this->has_variable_width_field_ && sizes == nullptr) {
        LOG(INFO) << "Schema has a variable width field, so sizes must be given." << endl;
        return -1;
    }

    auto good_err_msg = ErrorMsgIfNotGood();
    if (!good_err_msg.empty()) {
      LOG(INFO) << good_err_msg << endl;
      return -1;
    }

    int64_t samples_fetched = 0;
    int64_t buffer_index = 0;
    bool should_xread = false;

    int64_t end_us;
    if (timeout_ms <= 0) {
        end_us = int64_t{INT64_MAX};
    } else {
        int64_t start_us = chrono::duration_cast<std::chrono::microseconds>(
                chrono::steady_clock::now().time_since_epoch()).count();
        end_us = start_us + 1000 * timeout_ms;
    }

    // NB: Redis XREAD blocking resolution is ~0.1 seconds per their documentation. Thus we should only rely
    // on XREAD blocking if there's ample time left - in this case >= 500ms in the timeout.
    const int redis_resolution_ms = 200;
    while (samples_fetched < num_samples) {
        int64_t remaining_us = end_us - chrono::duration_cast<std::chrono::microseconds>(
                chrono::steady_clock::now().time_since_epoch()).count();
        if (remaining_us < 0) {
            break;
        }

        int64_t samples_remaining = num_samples - samples_fetched;
        int64_t num_to_fetch = samples_remaining > max_fetch_size_ ? max_fetch_size_ : samples_remaining;

        // NB: depending whether we XREAD or XRANGE, the data in reply is shaped differently, hence the need to split
        // out a separate "data_reply" pointer.
        internal::Redis::UniqueRedisReplyPtr reply;
        redisReply *data_reply;

        int num_elements_fetched;
        if (should_xread) {
            int64_t to_block = max(int64_t{1LL}, min(remaining_us / 1000 - redis_resolution_ms, int64_t{1000LL}));
            reply = redis_->Xread(
                num_to_fetch,
                static_cast<int>(to_block),
                current_stream_key_, // streams
                cursor_.right == 0 && cursor_.left != 0 ? cursor_.left - 1 : cursor_.left, // cursor
                cursor_.right == 0 ? uint64_t{UINT64_MAX} : cursor_.right - 1);
            if (reply->type == REDIS_REPLY_NIL) {
                should_xread = true;
                num_elements_fetched = 0;
                data_reply = nullptr;
            } else if (reply->type != REDIS_REPLY_ARRAY || reply->elements != 1 || reply->element[0]->elements != 2) {
                throw StreamReaderException("Unexpected response from redis on XREAD.");
            } else {
                data_reply = reply->element[0]->element[1];
                num_elements_fetched = data_reply->elements;
            }
        } else {
            should_xread = false;
            reply = redis_->Xrange(
                num_to_fetch,
                current_stream_key_, // streams
                cursor_.left,
                cursor_.right);
            if (reply->type != REDIS_REPLY_ARRAY) {
                throw StreamReaderException(fmt::format("Unexpected response received when fetching! Got reply type {}",
                                                        reply->type));
            }

            num_elements_fetched = reply->elements;
            data_reply = reply.get();
        }

        remaining_us = end_us - chrono::duration_cast<std::chrono::microseconds>(
                chrono::steady_clock::now().time_since_epoch()).count();
        if (num_elements_fetched == 0) {
            if (remaining_us > redis_resolution_ms * 1000) {
                should_xread = true;
            } else if (remaining_us > 0) {
                // Sleep for a small amount of time to prevent a tight loop
                std::this_thread::sleep_for(std::chrono::microseconds(50));
                should_xread = false;
            } else {
                break;
            }
            continue;
        }

        // Format of the reply is:
        // [ (key, (field1, value1, field2, value2, ...)), ... ]
        for (size_t i = 0; i < data_reply->elements; i++) {
            redisReply *element = data_reply->element[i]->element[1];
            int len;
            const char *value = FindField(element, "val", &len);
            if (value != nullptr) {
                if (sizes != nullptr) {
                    (*sizes)[samples_fetched] = len;
                }
                if (keys != nullptr) {
                    (*keys)[samples_fetched] = data_reply->element[i]->element[0]->str;
                }
                if (!this->has_variable_width_field_) {
                    memcpy(&buffer[buffer_index], value, sample_size_);
                    buffer_index += sample_size_;
                } else {
                    memcpy(&buffer[buffer_index], value, len);
                    buffer_index += len;
                }
                samples_fetched++;
                num_samples_read_++;
            }
        }

        redisReply *last_element = data_reply->element[data_reply->elements - 1];

        IncrementCursorFrom(last_element->element[0]->str);

        // Look for tombstone / eof in the last element
        const char *eof_str = FindField(last_element->element[1], "eof");
        if (eof_str != nullptr) {
            const char *sample_index_str = FindField(last_element->element[1], "sample_index");
            if (sample_index_str == nullptr) {
                throw StreamReaderException("EOF entry found without a sample_index key.");
            }

            int64_t last_sample_index = strtoll(sample_index_str, nullptr, 10);

            // NB: For some reason on M1 Macs, LOG(INFO) with a number (int64_t here) causes a SIGBUS / EXC_BAD_ACCESS
            // error to be thrown. Putting it through a stringstream seems to work. Glog 0.6.0 and C++14/17.
            std::stringstream ss;
            ss << "EOF received! Ending stream with " << samples_fetched << " elements at sample "
               << last_sample_index << endl;
            LOG(INFO) << ss.str();
            FireStreamKeyChange(current_stream_key_, "");
            is_eof_ = true;
            eof_key_ = std::string(last_element->element[0]->str);
            // Ensure we can't get caught in a "stalling" loop where we've actually returned EOF but return no data.
            if (samples_fetched == 0) {
              return -1;
            } else {
              return samples_fetched;
            }
        }

        const char *tombstone_str = FindField(last_element->element[1], "tombstone");
        if (tombstone_str != nullptr) {
            const char *next_stream_str = FindField(last_element->element[1], "next_stream_key");
            if (next_stream_str == nullptr) {
                throw StreamReaderException("Tombstone entry found without a next_stream_key key.");
            }
            const char *sample_index_str = FindField(last_element->element[1], "sample_index");
            if (sample_index_str == nullptr) {
                throw StreamReaderException("Tombstone entry found without a sample_index_str key.");
            }
            LOG(INFO) << "Tombstone received! Changing streams from " << current_stream_key_ << " to "
                      << next_stream_str
                      << endl;
            const std::string &s = std::string(next_stream_str);
            FireStreamKeyChange(current_stream_key_, s);
            current_stream_key_ = s;
            cursor_.left = 0;
            cursor_.right = 0;
            continue;
        }

        // If it's neither tombstone or EOF, then it's a data element; use its "i" field for sample index.
        current_sample_idx_ = GetSampleIndexOrThrow(last_element->element[1]);
    }

  return samples_fetched;
}

int64_t StreamReader::TailBytes(char *buffer, int timeout_ms, char *key, int64_t *sample_index) {
    auto good_err_msg = ErrorMsgIfNotGood();
    if (!good_err_msg.empty()) {
        LOG(INFO) << good_err_msg << endl;
        return -1;
    }

    int64_t end_us;
    if (timeout_ms <= 0) {
        end_us = INT64_MAX;
    } else {
        int64_t start_us = chrono::duration_cast<std::chrono::microseconds>(
                chrono::steady_clock::now().time_since_epoch()).count();
        end_us = start_us + 1000 * timeout_ms;
    }

    // NB: Redis XREAD blocking resolution is ~0.1 seconds per their documentation. Thus we should only rely
    // on XREAD blocking if there's ample time left - in this case >= 500ms in the timeout.
    const int redis_resolution_ms = 200;

    bool should_xread = false;
    while (true) {
        int64_t remaining_us = end_us - chrono::duration_cast<std::chrono::microseconds>(
                chrono::steady_clock::now().time_since_epoch()).count();
        if (remaining_us < 0) {
            break;
        }

        internal::Redis::UniqueRedisReplyPtr reply;
        redisReply *data_reply;
        bool did_read;

        if (!should_xread) {
            reply = redis_->Xrevrange(
                1,
                current_stream_key_,
                "+",
                cursor_.left,
                cursor_.right
            );

            if (reply->elements > 1) {
                int num_elements = reply->elements;
                throw StreamReaderException(fmt::format("Expected 0 or 1 elements but got {}", num_elements));
            }

            did_read = (reply->elements != 0);
            data_reply = reply.get();
        } else {
            reply = redis_->Xread(
                1,
                1000,
                current_stream_key_,
                cursor_.right == 0 && cursor_.left != 0 ? cursor_.left - 1 : cursor_.left,
                cursor_.right == 0 ? uint64_t{UINT64_MAX} : cursor_.right - 1);
            if (reply->type == REDIS_REPLY_NIL) {
                did_read = false;
                data_reply = nullptr;
            } else if (reply->type != REDIS_REPLY_ARRAY || reply->elements != 1 || reply->element[0]->elements != 2) {
                int type = reply->type;
                size_t num_elements = reply->elements;
                throw StreamReaderException(
                        fmt::format("Unexpected response from redis on XREAD. type={}, #elements={}", type,
                                    num_elements));
            } else {
                data_reply = reply->element[0]->element[1];
                if (data_reply->elements != 1) {
                    int num_elements = reply->elements;
                    throw StreamReaderException(fmt::format("Expected exactly 1 elements but got {}", num_elements));
                }
                did_read = true;
            }
        }

        remaining_us = end_us - chrono::duration_cast<std::chrono::microseconds>(
                chrono::steady_clock::now().time_since_epoch()).count();
        if (!did_read) {
            if (remaining_us > redis_resolution_ms * 1000) {
                should_xread = true;
            } else if (remaining_us > 0) {
                // Sleep for a small amount of time to prevent a tight loop
                std::this_thread::sleep_for(std::chrono::microseconds(50));
                should_xread = false;
            } else {
                break;
            }
            continue;
        }

        // singleton(key, (field1, value1, field2, value2, ...))
        auto *this_key = data_reply->element[0]->element[0]->str;
        auto *values = data_reply->element[0]->element[1];

        // Look for tombstone / eof in the last element
        const char *tombstone_str = FindField(values, "tombstone");
        const char *eof_str = FindField(values, "eof");

        if (tombstone_str == nullptr && eof_str == nullptr) {
            IncrementCursorFrom(this_key);
            int len;
            const char *val_str = FindField(values, "val", &len);

            if (key != nullptr) {
                strcpy(key, this_key);
            }
            int64_t old_sample_index = current_sample_idx_;
            current_sample_idx_ = GetSampleIndexOrThrow(values);

            if (sample_index != nullptr) {
                *sample_index = current_sample_idx_;
            }

            if (!this->has_variable_width_field_) {
                memcpy(buffer, val_str, sample_size_);
            } else {
                memcpy(buffer, val_str, len);
            }
            int64_t num_skipped = current_sample_idx_ - old_sample_index;
            num_samples_read_ += num_skipped;
            return num_skipped;
        }

        if (eof_str != nullptr) {
            return -1;
        }

        // Tombstone
        const char *next_stream_str = FindField(values, "next_stream_key");
        if (next_stream_str == nullptr) {
            throw StreamReaderException("Tombstone entry found without a next_stream_key key.");
        }
        const char *sample_index_str = FindField(values, "sample_index");
        if (sample_index_str == nullptr) {
            throw StreamReaderException("Tombstone entry found without a sample_index_str key.");
        }
        LOG(INFO) << "Tombstone received! Changing streams from " << current_stream_key_ << " to " << next_stream_str
                  << endl;
        const std::string &s = std::string(next_stream_str);
        FireStreamKeyChange(current_stream_key_, s);
        current_stream_key_ = s;
        cursor_.left = 0ULL;
        cursor_.right = 0ULL;
    }

    return 0;
}

std::string StreamReader::ErrorMsgIfNotGood() {
    if (Good()) {
        return "";
    }

    if (!is_initialized_) {
        return "Stream is not good: Initialize() has not been called.";
    }
    if (is_stopped_) {
        return "Stream is not good: stop() has been called.";
    }
    if (is_eof_) {
        return "Stream is not good: EOF has been reached.";
    }
    return "Stream is not good: unknown.";
}

int64_t StreamReader::Seek(const std::string &key) {
    auto err = ErrorMsgIfNotGood();
    if (!err.empty()) {
        LOG(INFO) << err << endl;
        return -1;
    }

    while (true) {
        auto reply = redis_->Xrevrange(
            1,
            current_stream_key_,
            key,
            cursor_.left,
            cursor_.right);
        if (reply->elements > 1) {
            throw StreamReaderException("Expected exactly 0 or 1 elements in seek().");
        }

        if (reply->elements == 0) {
            // No elements found *before* the target key in this stream. This could be due to the fact that the key is
            // actually "in the past" -- i.e. already consumed -- or that the stream itself is empty. In either case, the
            // right action is to not change the current cursor.
            LOG(INFO) << "No elements found before this key. Not changing cursor." << endl;
            return 0;
        }

        redisReply *data_reply = reply->element[0];
        const char *last_key = data_reply->element[0]->str;
        const char *eof = FindField(data_reply->element[1], "eof");
        if (eof != nullptr) {
            // EOF was found *before* the target key, meaning the target key is past the end of this stream, i.e. it's a
            // bogus key.
            const std::string &msg = fmt::format("Key {} exceeded EOF of the stream (EOF key {}).", key, last_key);
            LOG(INFO) << msg << endl;
            return -1;
        }

        const char *tombstone = FindField(data_reply->element[1], "tombstone");
        if (tombstone == nullptr) {
            // If it's not a tombstone, then we've found the greatest key that's immediately less than or equal to this
            // key. We can then set the cursor to a incremented copy of the given key.
            IncrementCursorFrom(last_key);
            int64_t old_sample_index = current_sample_idx_;
            current_sample_idx_ = GetSampleIndexOrThrow(data_reply->element[1]);
            int64_t ret = current_sample_idx_ - old_sample_index;
            LOG(INFO) << fmt::format("Seeked successfully; skipped {} elements. New cursor {}-{}",
                                     ret,
                                     cursor_.left,
                                     cursor_.right) << endl;
            num_samples_read_ += ret;
            return ret;
        }

        // Tombstone found before this key, meaning we need to follow the chain to the next stream and repeat the process.
        const char *next_stream_str = FindField(data_reply->element[1], "next_stream_key");
        if (next_stream_str == nullptr) {
            throw StreamReaderException("Tombstone entry found without a next_stream_key key.");
        }
        const char *sample_index_str = FindField(data_reply->element[1], "sample_index");
        if (sample_index_str == nullptr) {
            throw StreamReaderException("Tombstone entry found without a sample_index_str key.");
        }
        int64_t last_sample_index = strtoll(sample_index_str, nullptr, 10);
        const std::string &s = std::string(next_stream_str);

        LOG(INFO) << fmt::format("Tombstone received during seek. Changing streams from {} to {} [sample_index {}]",
                                 current_stream_key_, next_stream_str, last_sample_index) << endl;
        FireStreamKeyChange(current_stream_key_, s);
        current_stream_key_ = s;
        cursor_.left = 0ULL;
        cursor_.right = 0ULL;
    }
}

/**
 * Polls redis until the metadata key exists. Returns nullptr if the timeout is exceeded (or if only one attempt is
 * requested.
 */
unique_ptr<unordered_map<std::string, std::string>> StreamReader::RetryablyFetchMetadata(const std::string &stream_name,
                                                                                    int timeout_ms) {
    int64_t start_ms = chrono::duration_cast<std::chrono::milliseconds>(
            chrono::steady_clock::now().time_since_epoch()).count();
    int64_t end_ms = timeout_ms > 0 ? start_ms + timeout_ms : (start_ms - 1);
    do {
        auto maybe_metadata = redis_->GetMetadata(stream_name);
        if (!maybe_metadata) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        return maybe_metadata;
    } while (chrono::duration_cast<std::chrono::milliseconds>(
            chrono::steady_clock::now().time_since_epoch()).count() < end_ms);
    return unique_ptr<unordered_map<std::string, std::string>>();
}

unordered_map<std::string, std::string> StreamReader::Metadata() {
    auto ret = redis_->GetUserMetadata(stream_name_);
    if (!ret) {
        throw StreamReaderException(fmt::format(
            "Metadata could not be found for stream {}; has it been initialized?", stream_name_));
    }
    return *ret;
}

void StreamReader::Stop() {
    is_stopped_ = true;
    if (redis_) {
      redis_.reset();
    }
}

void StreamReader::AddListener(internal::StreamReaderListener *listener) {
    listeners_.push_back(listener);
}

void StreamReader::FireStreamKeyChange(const std::string &old_stream_key, const std::string &new_stream_key) {
    for (auto &listener : listeners_) {
        listener->OnStreamKeyChange(old_stream_key, new_stream_key);
    }
}

const StreamSchema &StreamReader::schema() {
    if (!schema_) {
        throw StreamReaderException("Schema has not been initialized. Did you call initialize()?");
    }
    return *this->schema_;
}

int64_t StreamReader::local_minus_server_clock_us() {
    return local_minus_server_clock_us_;
}

}

