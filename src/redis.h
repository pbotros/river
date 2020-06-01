//
// Created by Paul Botros on 10/28/19.
//

#ifndef PARENT_REDIS_H
#define PARENT_REDIS_H

#include <hiredis.h>
#include <sstream>
#include <cstdlib>
#include <chrono>
#include <utility>
#include <memory>
#include <vector>
#include <unordered_map>
#include <boost/optional.hpp>

#if defined(_WIN32) || defined(_WIN64)
#include <boost/asio.hpp>
#include <windows.h>
#endif

using namespace std;

namespace river {

class RedisConnection {
public:
    string redis_hostname_;
    int redis_port_;
    string redis_password_;

    RedisConnection(string redis_hostname, int redis_port, string redis_password = "")
            : redis_hostname_(std::move(redis_hostname)),
              redis_port_(redis_port),
              redis_password_(std::move(redis_password)) {}
};

namespace internal {


inline void DecodeCursor(const char *key, uint64_t *left, uint64_t *right) {
    // Increment the LSB part of the cursor for the next fetch
    string last_key = string(key);
    unsigned long delimiter_index = last_key.rfind('-');
    string new_left_str = last_key.substr(0, delimiter_index);
    string new_right_str = last_key.substr(delimiter_index + 1);
    *left = strtoull(new_left_str.c_str(), nullptr, 10);
    *right = strtoull(new_right_str.c_str(), nullptr, 10);
}

inline std::chrono::system_clock::time_point KeyTimestamp(const char *key) {
    unsigned long long left, right;
    DecodeCursor(key, &left, &right);
    return std::chrono::system_clock::time_point(std::chrono::milliseconds(left));
}

class RedisException : public exception {
public:
    explicit RedisException(const std::string &message) {
        std::stringstream s;
        s << "[RedisException] " << message;
        _message = s.str();
    }

    const char *what() const noexcept override {
        return _message.c_str();
    }

private:
    std::string _message;
};

class Redis {
public:
    explicit Redis() {
        _context = nullptr;
    }

    ~Redis() {
        redisFree(_context);
        _context = nullptr;
    }

    struct RedisReplyDeleter {
        void operator()(redisReply *reply) {
            freeReplyObject(reply);
        }
    };

    typedef unique_ptr<redisReply, RedisReplyDeleter> UniqueRedisReplyPtr;

    UniqueRedisReplyPtr Xread(
            int64_t num_to_fetch,
            int timeout_ms,
            const string &stream_name,
            uint64_t key_part1,
            uint64_t key_part2);

    UniqueRedisReplyPtr Xrange(
            int64_t num_to_fetch,
            const string &stream_name,
            uint64_t key_part1,
            uint64_t key_part2);

    UniqueRedisReplyPtr Xrevrange(
            int64_t num_to_fetch,
            const string &stream_name,
            const string &key_left,
            uint64_t key_right_part1,
            uint64_t key_right_part2);


    UniqueRedisReplyPtr Xadd(const string &stream_name, initializer_list<pair<string, string>> key_value_pairs);

    boost::optional<unordered_map<string, string>> GetMetadata(const string &stream_name);

    boost::optional<unordered_map<string, string>> GetUserMetadata(const string &stream_name);

    void SetUserMetadata(const string &stream_name, const unordered_map<string, string> &metadata);

    int SetMetadata(const string &stream_name, initializer_list<pair<string, string>> key_value_pairs);

    void DeleteMetadata(const string &stream_name);

    inline void SendCommandArgv(int argc, const char **argv, const size_t *argvlen) {
        redisAppendCommandArgv(_context, argc, argv, argvlen);
    }

    inline UniqueRedisReplyPtr GetReply() {
        redisReply *reply;
        int response = redisGetReply(_context, (void **) &reply);
        if (response != REDIS_OK) {
            stringstream ss;
            ss << "Error from redis when fetching reply: " << reply->str;
            freeReplyObject(reply);
            throw RedisException(ss.str());
        }
        return UniqueRedisReplyPtr(reply);
    }

    vector<string> ListStreamNames(const string &stream_filter);

    void Unlink(const string &stream_key);

    int64_t TimeUs();

    static unique_ptr<Redis> Create(const RedisConnection &connection);

private:
    explicit Redis(redisContext *context) {
        this->_context = context;
    }

    redisContext *_context;
};

}
}

#endif //PARENT_REDIS_H
