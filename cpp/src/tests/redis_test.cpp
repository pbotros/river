#include "gtest/gtest.h"
#include "../tools/uuid.h"
#include "../redis.h"

using namespace std;
using namespace river;

class RedisTest : public ::testing::Test {
protected:
    void SetUp() override {
        stream_name = uuid::generate_uuid_v4();
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


