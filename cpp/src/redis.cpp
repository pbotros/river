//
// Created by Paul Botros on 10/28/19.
//

#include "redis.h"
#include <hiredis.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <regex>
#include <glog/logging.h>
#include <fmt/format.h>
#include <glog/logging.h>

#include "net/sockcompat.h"
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

using json = nlohmann::json;
using namespace std;

namespace river {
namespace internal {

unique_ptr<Redis> Redis::Create(const RedisConnection &connection) {
    struct timeval timeout = {connection.timeout_seconds(), 0};
    std::string redis_hostname = connection.redis_hostname();
    redisContext *new_context = redisConnectWithTimeout(
        redis_hostname.c_str(), connection.redis_port(), timeout);
    if (new_context == nullptr || new_context->err) {
        string msg = fmt::format("Connection error to host:port={}:{}, err={}",
                                 redis_hostname, connection.redis_port(),
                                 new_context != nullptr ? new_context->errstr : "NULL");
        redisFree(new_context);
        throw RedisException(msg);
    }

    auto redis_password = connection.redis_password();
    if (connection.redis_password().length() > 0) {
        auto *reply = (redisReply *) redisCommand(new_context, "AUTH %s", redis_password.c_str());
        if (reply == nullptr || reply->type == REDIS_REPLY_ERROR || new_context->err != 0) {
          string msg = fmt::format("Authorization failed to Redis. "
                                   "reply_type={}, "
                                   "reply_msg={}, "
                                   "context_err={}, "
                                   "context_errstr={}",
                                   reply->type,
                                   reply->str != nullptr ? reply->str : "null",
                                   new_context->err,
                                   new_context->errstr);
          redisFree(new_context);
          throw RedisException(msg);
        }

        LOG(INFO) << "AUTH response: " << reply->str << std::endl;
        freeReplyObject(reply);
    }

    auto *redis = new Redis(new_context);
    return unique_ptr<Redis>(redis);
}

Redis::UniqueRedisReplyPtr Redis::Xread(
        int64_t num_to_fetch,
        int timeout_ms,
        const string &stream_name,
        uint64_t key_part1,
        uint64_t key_part2) {
    auto *reply = (redisReply *) redisCommand(
            _context, "XREAD COUNT %d BLOCK %lld STREAMS %s %llu-%llu",
            num_to_fetch,
            timeout_ms,
            stream_name.c_str(),
            key_part1,
            key_part2);
    if (reply == nullptr) {
        throw RedisException(
                fmt::format("[XREAD] Null response received when fetching! err={}, errstr={}",
                            _context->err,
                            _context->errstr));
    }

    return UniqueRedisReplyPtr(reply);
}

Redis::UniqueRedisReplyPtr Redis::Xrange(
        int64_t num_to_fetch,
        const string &stream_name,
        uint64_t key_part1,
        uint64_t key_part2) {
    auto reply = (redisReply *) redisCommand(
            _context, "XRANGE %s %llu-%llu + COUNT %lld",
            stream_name.c_str(),
            key_part1,
            key_part2,
            num_to_fetch);
    if (reply == nullptr) {
        throw RedisException(
                fmt::format("Null response received when fetching! err={}, errstr={}",
                            _context->err,
                            _context->errstr));
    }

    return UniqueRedisReplyPtr(reply);
}

Redis::UniqueRedisReplyPtr Redis::Xrevrange(
        int64_t num_to_fetch,
        const string &stream_name,
        const string &key_left,
        uint64_t key_right_part1,
        uint64_t key_right_part2) {
    auto *reply = (redisReply *) redisCommand(
            _context,
            "XREVRANGE %s %s %llu-%llu COUNT %d",
            stream_name.c_str(),
            key_left.c_str(),
            key_right_part1,
            key_right_part2,
            num_to_fetch);
    if (reply == nullptr) {
        throw RedisException(
                fmt::format("Null response received when fetching! err={}, errstr={}",
                            _context->err,
                            _context->errstr));
    }
    if (reply->type != REDIS_REPLY_ARRAY) {
        freeReplyObject(reply);
        throw RedisException("Array response expected for XREVRANGE.");
    }

    return UniqueRedisReplyPtr(reply);
}

std::string Redis::GetMetadataKey(const string &stream_name) const {
    return fmt::format("{}-metadata", stream_name);
}

unique_ptr<unordered_map<string, string>> Redis::GetMetadata(const string &stream_name) {
    auto metadata_key = GetMetadataKey(stream_name);
    auto *reply = (redisReply *) redisCommand(_context, "HGETALL %s", metadata_key.c_str());
    if (reply == nullptr) {
        throw RedisException(
                fmt::format("Null response received when fetching metadata! err={}, errstr={}",
                            _context->err,
                            _context->errstr));
    }
    if (reply->type != REDIS_REPLY_ARRAY) {
        const string message = fmt::format(
                "Array response expected for HGETALL, but got {} [stream_name {}].", reply->type,
                stream_name);
        freeReplyObject(reply);
        throw RedisException(message);
    }

    if (reply->elements == 0) {
        freeReplyObject(reply);
        return unique_ptr<unordered_map<string, string>>();
    }

    unordered_map<string, string> ret;
    for (size_t field_idx = 0; field_idx < reply->elements; field_idx += 2) {
        ret.insert({string(reply->element[field_idx]->str, reply->element[field_idx]->len),
                    string(reply->element[field_idx + 1]->str, reply->element[field_idx + 1]->len)});
    }

    freeReplyObject(reply);
    return make_unique<unordered_map<string, string>>(ret);
}

std::vector<std::string> Redis::GetInstalledModules() {
    auto *reply = (redisReply *) redisCommand(_context, "MODULE LIST");
    if (reply == nullptr) {
        throw RedisException(
            fmt::format("Null response received when fetching! err={}, errstr={}",
                        _context->err,
                        _context->errstr));
    }
    UniqueRedisReplyPtr reply_ptr(reply);

    if (reply->type != REDIS_REPLY_ARRAY) {
        throw RedisException("Array response expected for MODULE LIST.");
    }

    if (reply->elements == 0) {
        return {};
    }

    std::vector<std::string> ret;
    for (int i = 0; i < reply->elements; i++) {
        auto module_info = reply->element[i];
        if (module_info->type != REDIS_REPLY_ARRAY) {
            throw RedisException("Expected nested arrays for MODULE LIST");
        }
        for (int field_idx = 0; field_idx < module_info->elements; field_idx += 2) {
            if (std::strcmp(module_info->element[field_idx]->str, "name") == 0) {
                ret.push_back(module_info->element[field_idx + 1]->str);
                break;
            }
        }
    }

    return ret;
}

unique_ptr<unordered_map<string, string>> Redis::GetUserMetadata(const string &stream_name) {
    auto maybe_metadata = this->GetMetadata(stream_name);
    if (!maybe_metadata) {
        return unique_ptr<unordered_map<string, string>>();
    }

    stringstream ss;
    ss << (*maybe_metadata)["user_metadata"];
    json pt = json::parse(ss);
    unordered_map<string, string> ret;
    for (auto &it : pt.items()) {
      ret.insert({it.key(), it.value().get<string>()});
    }
    return make_unique<unordered_map<string, string>>(ret);
}

int Redis::SetMetadataAndUserMetadata(const string &stream_name,
                                      const vector <std::pair<string, string>>& key_value_pairs,
                                      const unordered_map <string, string> &user_metadata) {
    json parent;
    for (auto &it : user_metadata) {
        parent[it.first] = it.second;
    }

    string out = parent.dump();
    vector<std::pair<string, string>> key_value_pairs_all(key_value_pairs);
    key_value_pairs_all.emplace_back("user_metadata", out);
    return this->SetMetadata(stream_name, key_value_pairs_all);
}

void Redis::SetUserMetadata(const string &stream_name, const unordered_map<string, string> &metadata) {
    json parent;
    for (auto &it : metadata) {
        parent[it.first] = it.second;
    }

    string out = parent.dump();
    this->SetMetadata(stream_name, {{"user_metadata", out}});
}

int Redis::SetMetadata(const string &stream_name, const vector<std::pair<string, string>>& key_value_pairs) {
    vector<string> parts;

    parts.push_back("HSET");
    parts.push_back(GetMetadataKey(stream_name));
    for (const auto &pair : key_value_pairs) {
        parts.push_back(pair.first);
        parts.push_back(pair.second);
    }

    vector<size_t> part_sizes;
    vector<const char *> parts_cstr;
    for (const auto &part : parts) {
        parts_cstr.push_back(const_cast<char *>(part.c_str()));
        part_sizes.push_back(part.size());
    }

    auto *reply = (redisReply *) redisCommandArgv(_context, parts_cstr.size(), &parts_cstr.front(),
                                                  &part_sizes.front());

    if (reply == nullptr) {
        throw RedisException("Error setting metadata. Got null.");
    }
    if (reply->type != REDIS_REPLY_INTEGER) {
        stringstream ss;
        ss << "Error setting metadata.";
        if (reply->str != nullptr) {
            ss << " Error: " << reply->str;
        } else {
            ss << " ??? " << reply->type;
        }
        freeReplyObject(reply);
        throw RedisException(ss.str());
    }
    int ret = reply->integer;
    freeReplyObject(reply);
    return ret;
}

int64_t Redis::TimeUs() {
    auto *reply = (redisReply *) redisCommand(_context, "TIME");

    if (reply == nullptr || reply->type != REDIS_REPLY_ARRAY || reply->elements != 2) {
        return -1;
    }
    auto ret = static_cast<int64_t>(strtoll(reply->element[0]->str, nullptr, 10) * 1000000ULL +
                                    strtoul(reply->element[1]->str, nullptr, 10));
    freeReplyObject(reply);
    return ret;
}

Redis::UniqueRedisReplyPtr
Redis::Xadd(const string &stream_name, initializer_list<pair<string, string>> key_value_pairs) {
    vector<string> parts;

    parts.push_back("XADD");
    parts.push_back(stream_name);
    parts.push_back("*");
    for (const auto &pair : key_value_pairs) {
        parts.push_back(pair.first);
        parts.push_back(pair.second);
    }

    vector<size_t> part_sizes;
    vector<const char *> parts_cstr;
    for (const auto &part : parts) {
        parts_cstr.push_back(const_cast<char *>(part.c_str()));
        part_sizes.push_back(part.size());
    }

    auto *reply = (redisReply *) redisCommandArgv(_context, parts_cstr.size(), &parts_cstr.front(),
                                                  &part_sizes.front());

    if (reply == nullptr) {
        throw RedisException(
                fmt::format("Null response received when doing XADD! err={}, errstr={}",
                            _context->err,
                            _context->errstr));
    }

    return river::internal::Redis::UniqueRedisReplyPtr(reply);
}

vector<string> Redis::ListStreamNames() {
    vector<string> ret;

    string cursor = "0";
    while (true) {
        UniqueRedisReplyPtr reply = UniqueRedisReplyPtr(
                (redisReply *) redisCommand(_context, "SCAN %s MATCH %s-metadata",
                                            cursor.c_str(),
                                            "*"));
        if (reply.get() == nullptr) {
            throw RedisException("SCAN returned null.");
        }
        if (reply->type != REDIS_REPLY_ARRAY) {
            throw RedisException("Fetching SCAN returned non-array.");
        }
        if (reply->elements == 0) {
            throw RedisException("Fetching SCAN returned zero elements; should have at least returned cursor.");
        }
        if (reply->element[0]->type != REDIS_REPLY_STRING) {
            throw RedisException("Fetching SCAN should have returned a string cursor.");
        }
        cursor = reply->element[0]->str;

        if (reply->element[1]->type != REDIS_REPLY_ARRAY) {
            throw RedisException("SCAN should have returned an array of items.");
        }
        for (size_t i = 0; i < reply->element[1]->elements; i++) {
            auto stream_name = regex_replace(string(reply->element[1]->element[i]->str),
                                             std::regex("-metadata$"), "");
            ret.push_back(stream_name);
        }

        if (cursor == "0") {
            break;
        }
    }
    return ret;
}

void Redis::Unlink(const string &stream_key) {
    auto *reply = (redisReply *) redisCommand(_context, "UNLINK %s", stream_key.c_str());
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER) {
        string msg = fmt::format(
                "Error deleting stream key {}. Reply: {}",
                stream_key,
                reply == nullptr ? "NULL" : to_string(reply->type));
        if (reply != nullptr) {
            freeReplyObject(reply);
        }
        throw RedisException(msg);
    }
    freeReplyObject(reply);
}

void Redis::DeleteMetadata(const string &stream_name) {
    auto metadata_key = GetMetadataKey(stream_name);
    auto *reply = (redisReply *) redisCommand(_context, "DEL %s", metadata_key.c_str());
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER) {
        string msg = fmt::format("Error deleting metadata for stream {}. Reply: {}", stream_name,
                                 reply == nullptr ? "NULL" : to_string(reply->type));
        if (reply != nullptr) {
            freeReplyObject(reply);
        }
        throw RedisException(msg);
    }
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
#ifdef _WIN32
#define strerror_r(errno,buf,len) strerror_s(buf,len,errno)
#endif /* _WIN32 */
        strerror_r(errno, c->errstr, sizeof(c->errstr));
    }
}

int Redis::SendCommandPreformatted(std::vector<std::pair<const char *, size_t>> preformatted_commands) {
    ZoneScoped;
    int nwritten_total = 0;

    for (const auto &[data, data_len] : preformatted_commands) {
        int n_bytes_written = 0;
        while (n_bytes_written < data_len) {
            int nwritten = redis_sockcompat::send_redis_sockcompat(
                _context->fd,
                &data[n_bytes_written],
                data_len - n_bytes_written, 0);
            if (nwritten < 0) {
                if ((errno == EWOULDBLOCK && !(_context->flags & REDIS_BLOCK)) || (errno == EINTR)) {
                    /* Try again later */
                    continue;
                } else {
                    __redisSetError(_context, REDIS_ERR_IO, NULL);
                    return -1;
                }
            }
            n_bytes_written += nwritten;
        }
        nwritten_total += data_len;
    }
    return nwritten_total;
}

RedisPool::RedisPool(int num_connections, const RedisConnection &connection)
    : connection_locks_(num_connections) {
    for (int i = 0; i < num_connections; ++i) {
        redises_.emplace_back(Redis::Create(connection));
    }
}

RedisPoolInstance RedisPool::Checkout() {
    std::scoped_lock<std::mutex> pool_guard(pool_lock_);

    for (int i = 0; i < connection_locks_.size(); ++i) {
        auto &lock = connection_locks_[i];
        if (lock.try_lock()) {
            lock.unlock();
            return {
                redises_[i].get(),
                connection_locks_[i]};
        }
    }

    throw RedisException("Could not acquire a connection and would otherwise hang; do you need a larger pool size?");
}
}
}
