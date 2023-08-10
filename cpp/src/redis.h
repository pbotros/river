//
// Created by Paul Botros on 10/28/19.
//

#ifndef PARENT_REDIS_H
#define PARENT_REDIS_H

#include <sstream>
#include <cstdlib>
#include <chrono>
#include <utility>
#include <memory>
#include <vector>
#include <unordered_map>
#include <hiredis.h>
#include <hiredis/sds.h>
#include <sockcompat.h>
#include <cassert>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

namespace river {

class RedisConnection {
public:
    const std::string redis_hostname_;
    const int redis_port_;
    const std::string redis_password_;
    const int timeout_seconds_;

    RedisConnection(
            std::string redis_hostname,
            int redis_port,
            std::string redis_password = "",
            int timeout_seconds = 30)
            : redis_hostname_(std::move(redis_hostname)),
              redis_port_(redis_port),
              redis_password_(std::move(redis_password)),
              timeout_seconds_(timeout_seconds) {}
};

namespace internal {


inline void DecodeCursor(const char *key, uint64_t *left, uint64_t *right) {
    // Increment the LSB part of the cursor for the next fetch
    std::string last_key = std::string(key);
    unsigned long delimiter_index = last_key.rfind('-');
    std::string new_left_str = last_key.substr(0, delimiter_index);
    std::string new_right_str = last_key.substr(delimiter_index + 1);
    *left = strtoull(new_left_str.c_str(), nullptr, 10);
    *right = strtoull(new_right_str.c_str(), nullptr, 10);
}

inline std::chrono::system_clock::time_point KeyTimestamp(const char *key) {
    uint64_t left, right;
    DecodeCursor(key, &left, &right);
    return std::chrono::system_clock::time_point(std::chrono::milliseconds(static_cast<int64_t>(left)));
}

 class RedisException : public std::exception {
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

    typedef std::unique_ptr<redisReply, RedisReplyDeleter> UniqueRedisReplyPtr;

    UniqueRedisReplyPtr Xread(
            int64_t num_to_fetch,
            int timeout_ms,
            const std::string &stream_name,
            uint64_t key_part1,
            uint64_t key_part2);

    UniqueRedisReplyPtr Xrange(
            int64_t num_to_fetch,
            const std::string &stream_name,
            uint64_t key_part1,
            uint64_t key_part2);

    UniqueRedisReplyPtr Xrevrange(
            int64_t num_to_fetch,
            const std::string &stream_name,
            const std::string &key_left,
            uint64_t key_right_part1,
            uint64_t key_right_part2);


    UniqueRedisReplyPtr Xadd(const std::string &stream_name, std::initializer_list<std::pair<std::string, std::string>> key_value_pairs);

    std::unique_ptr<std::unordered_map<std::string, std::string>> GetMetadata(const std::string &stream_name);

    std::unique_ptr<std::unordered_map<std::string, std::string>> GetUserMetadata(const std::string &stream_name);

    std::vector<std::string> GetInstalledModules();

    // Atomically set internal and user metadata at the same time to prevent race conditions.
    int SetMetadataAndUserMetadata(const std::string &stream_name,
                                   const std::vector<std::pair<std::string, std::string>>& key_value_pairs,
                                   const std::unordered_map<std::string, std::string> &user_metadata);

    void SetUserMetadata(const std::string &stream_name, const std::unordered_map<std::string, std::string> &metadata);

    int SetMetadata(const std::string &stream_name, const std::vector<std::pair<std::string, std::string>>& key_value_pairs);

    void DeleteMetadata(const std::string &stream_name);

    inline void SendCommandArgv(int argc, const char **argv, const size_t *argvlen) {
        redisAppendCommandArgv(_context, argc, argv, argvlen);
    }

    inline std::string FormatCommandArgv(int argc, const char **argv, const size_t *argvlen) {
        sds cmd;
        size_t cmd_strlen = redisFormatSdsCommandArgv(&cmd, argc, argv, argvlen);
        return {cmd, cmd_strlen};
    }

    void __redisSetError(redisContext *c, int type, const char *str) {
        // Lifted directly from Redis.
        size_t len;

        c->err = type;
        if (str != NULL) {
            len = strlen(str);
            len = len < (sizeof(c->errstr)-1) ? len : (sizeof(c->errstr)-1);
            memcpy(c->errstr,str,len);
            c->errstr[len] = '\0';
        } else {
            /* Only REDIS_ERR_IO may lack a description! */
            assert(type == REDIS_ERR_IO);
            strerror_r(errno, c->errstr, sizeof(c->errstr));
        }
    }

    inline int SendCommandPreformatted(std::vector<std::pair<const char *, size_t>> preformatted_commands) {
        int nwritten_total = 0;
        for (int i = 0; i < preformatted_commands.size(); i++) {
            int nwritten = send(_context->fd, preformatted_commands[i].first, preformatted_commands[i].second, 0);
            if (nwritten < 0) {
                if ((errno == EWOULDBLOCK && !(_context->flags & REDIS_BLOCK)) || (errno == EINTR)) {
                    /* Try again later */
                } else {
                    __redisSetError(_context, REDIS_ERR_IO, NULL);
                    return -1;
                }
            }
            nwritten_total += nwritten;
        }
        return nwritten_total;
    }

    inline UniqueRedisReplyPtr GetReply() {
        redisReply *reply = nullptr;
        int response = redisGetReply(_context, (void **) &reply);
        if (response != REDIS_OK) {
            std::stringstream ss;
            if (reply != nullptr) {
                ss << "Error from redis when fetching reply: type=" << reply->type;
                if (reply->len > 0) {
                    ss << ". Error message: " << std::string(reply->str, reply->len);
                }
                freeReplyObject(reply);
            } else {
                ss << "Error from redis when fetching reply: <null>";
            }
            throw RedisException(ss.str());
        }
        return UniqueRedisReplyPtr(reply);
    }

    std::vector<std::string> ListStreamNames();

    void Unlink(const std::string &stream_key);

    int64_t TimeUs();

    static std::unique_ptr<Redis> Create(const RedisConnection &connection);

private:
    explicit Redis(redisContext *context) {
        this->_context = context;
    }

    redisContext *_context;
};

}
}

#endif //PARENT_REDIS_H
