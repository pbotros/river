#include "gtest/gtest.h"
#include <boost/uuid/uuid.hpp>            // uuid class
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.
#include <boost/uuid/uuid.hpp>            // uuid class
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.

#include "../redis.h"

using namespace std;
using namespace river;

class RedisTest : public ::testing::Test {
protected:
    void SetUp() override {
        boost::uuids::uuid uuid = boost::uuids::random_generator()();
        stringstream ss;
        ss << uuid;
        stream_name = ss.str();
        redis = internal::Redis::Create(RedisConnection("127.0.0.1", 6379));
    }

    unique_ptr<internal::Redis> redis;
    string stream_name;
};

TEST_F(RedisTest, TestDoesNotExist) {
    auto ret = redis->GetMetadata(stream_name);
    ASSERT_FALSE(ret);

    ret = redis->GetUserMetadata(stream_name);
    ASSERT_FALSE(ret);
}

TEST_F(RedisTest, TestExists) {
    redis->SetUserMetadata(stream_name, unordered_map<string, string>({{"key", "value"}}));

    string err;
    auto ret = redis->GetUserMetadata(stream_name);
    ASSERT_TRUE(ret);
    ASSERT_STREQ(ret->at("key").c_str(), "value");
}


