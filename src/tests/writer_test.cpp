#include "gtest/gtest.h"
#include "../river.h"
#include <boost/uuid/uuid.hpp>            // uuid class
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.
#include <cstring>
#include <unordered_map>

using namespace std;
using namespace river;

static const int NUM_ELEMENTS = 256;

class StreamWriterTest : public ::testing::Test {
protected:
    void SetUp() override {
        this->writer = NewWriter<double>(FieldDefinition::DOUBLE);

        struct timeval timeout = {1, 500000};
        redis = redisConnectWithTimeout("127.0.0.1", 6379, timeout);
        set_metadata_on_init = false;
    }

    template<class T>
    shared_ptr<StreamWriter> NewWriter(const unordered_map<string, string>& metadata, FieldDefinition::Type type) {
        boost::uuids::uuid uuid = boost::uuids::random_generator()();
        stringstream ss;
        ss << uuid;
        stream_name = ss.str();

        auto writer = make_shared<StreamWriter>(RedisConnection("127.0.0.1", 6379));

        vector<FieldDefinition> field_definitions = vector<FieldDefinition>{
                FieldDefinition("field1", type, sizeof(T))
        };

        StreamSchema schema(field_definitions);
        if (set_metadata_on_init) {
            writer->Initialize(stream_name, schema, metadata);
        } else {
            writer->Initialize(stream_name, schema);
            writer->SetMetadata(metadata);
        }
        this->schema = new StreamSchema(schema);
        return writer;
    }

    template<class T>
    shared_ptr<StreamWriter> NewWriter(FieldDefinition::Type type) {
        return NewWriter<T>(unordered_map<string, string>(), type);
    }

    void TearDown() override {
        writer->Stop();
    }

    shared_ptr<StreamWriter> writer;
    string stream_name;
    StreamSchema *schema;
    redisContext *redis;
    bool set_metadata_on_init;
};

static void
assert_expected(redisContext *redis, const string &stream_name, void (*checker)(int, const char *, size_t)) {
    auto *reply = (redisReply *) redisCommand(redis, "HGET %s-metadata schema", stream_name.c_str());
    ASSERT_NE(strlen(reply->str), 0);
    // Just check that it doesn't blow up parsing
    internal::deserialize_schema(reply->str);

    reply = (redisReply *) redisCommand(redis, "HGET %s-metadata first_stream_key", stream_name.c_str());
    const char *first_stream_key = reply->str;
    reply = (redisReply *) redisCommand(redis, "XRANGE %s - +", first_stream_key);

    ASSERT_TRUE(reply != nullptr);
    ASSERT_EQ(reply->type, REDIS_REPLY_ARRAY);
    ASSERT_EQ(reply->elements, NUM_ELEMENTS);
    for (size_t i = 0; i < reply->elements; i++) {
        redisReply *element = reply->element[i];

        bool found_val = false;
        for (unsigned int j = 0; j < element->element[1]->elements; j++) {
            if (strcmp(element->element[1]->element[j]->str, "val") == 0) {
                redisReply *val = element->element[1]->element[j + 1];
                checker(i, val->str, val->len);
                found_val = true;
            }
        }

        ASSERT_TRUE(found_val);
    }

    freeReplyObject(reply);
}

TEST_F(StreamWriterTest, TestDouble) {
    double data[NUM_ELEMENTS];
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        data[i] = i;
    }
    writer->Write(data, NUM_ELEMENTS);

    assert_expected(redis, stream_name, [](int index, const char *val_value, size_t length) {
        ASSERT_EQ(length, sizeof(double));
        ASSERT_DOUBLE_EQ(*(reinterpret_cast<const double *>(val_value)), (double) index);
    });
}
TEST_F(StreamWriterTest, TestFloat) {
    float data[NUM_ELEMENTS];
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        data[i] = i;
    }
    NewWriter<float>(FieldDefinition::FLOAT)->Write(data, NUM_ELEMENTS);

    assert_expected(redis, stream_name, [](int index, const char *val_value, size_t length) {
        ASSERT_EQ(length, sizeof(float));
        ASSERT_FLOAT_EQ(*(reinterpret_cast<const float *>(val_value)), (float) index);
    });
}

TEST_F(StreamWriterTest, TestLong) {
    long data[NUM_ELEMENTS];
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        data[i] = i;
    }
    NewWriter<long>(FieldDefinition::INT32)->Write(data, NUM_ELEMENTS);

    assert_expected(redis, stream_name, [](int index, const char *val_value, size_t length) {
        ASSERT_EQ(length, sizeof(long));
        ASSERT_EQ(*(reinterpret_cast<const long *>(val_value)), (long) index);
    });
}

TEST_F(StreamWriterTest, TestInteger) {
    int data[NUM_ELEMENTS];
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        data[i] = i;
    }
    NewWriter<int>(FieldDefinition::INT32)->Write(data, NUM_ELEMENTS);

    assert_expected(redis, stream_name, [](int index, const char *val_value, size_t length) {
        ASSERT_EQ(length, sizeof(int));
        ASSERT_EQ(*(reinterpret_cast<const int *>(val_value)), (int) index);
    });
}

TEST_F(StreamWriterTest, TestLongLong) {
    long long data[NUM_ELEMENTS];
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        data[i] = i;
    }
    NewWriter<long long>(FieldDefinition::INT64)->Write(data, NUM_ELEMENTS);

    assert_expected(redis, stream_name, [](int index, const char *val_value, size_t length) {
        ASSERT_EQ(length, sizeof(long long));
        ASSERT_EQ(*(reinterpret_cast<const long long *>(val_value)), (long long) index);
    });
}

TEST_F(StreamWriterTest, TestBinary) {
    char data[NUM_ELEMENTS];
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        data[i] = i;
    }
    NewWriter<char>(FieldDefinition::FIXED_WIDTH_BYTES)->Write(data, NUM_ELEMENTS);

    assert_expected(redis, stream_name, [](int index, const char *val_value, size_t length) {
        ASSERT_EQ(length, sizeof(char));
        ASSERT_EQ(*(reinterpret_cast<const char *>(val_value)), (char) index);
    });
}

TEST_F(StreamWriterTest, TestVariableBinaryBlowsUpWithoutSizes) {
    char data[NUM_ELEMENTS];
    shared_ptr<StreamWriter> w = NewWriter<char>(FieldDefinition::VARIABLE_WIDTH_BYTES);
    ASSERT_THROW(w->Write(data, NUM_ELEMENTS, nullptr), StreamWriterException);
}

TEST_F(StreamWriterTest, TestVariableBinary) {
    // Create an array of [<1 byte>, <2 bytes>, ..., <NUM_ELEMENT - 1 bytes>, <NUM_ELEMENT bytes>]
    // where each segment is [0, ..., <num bytes - 1>]
    char data[NUM_ELEMENTS * (NUM_ELEMENTS + 1) / 2];
    int sizes[NUM_ELEMENTS];
    unsigned int idx = 0;
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        sizes[i] = i + 1;
        for (int j = 0; j <= i; j++) {
            data[idx++] = (char) j;
        }
    }

    NewWriter<char>(FieldDefinition::VARIABLE_WIDTH_BYTES)->Write(data, NUM_ELEMENTS, sizes);

    assert_expected(redis, stream_name, [](int index, const char *val_value, size_t length) {
        ASSERT_EQ(length, index + 1);
        for (int j = 0; j <= index; j++) {
            ASSERT_EQ(val_value[j], (char) j);
        }
    });
}

TEST_F(StreamWriterTest, TestGetSetMetadata) {
    ASSERT_TRUE(writer->Metadata().empty());
    writer->Stop();

    unordered_map<string, string> test_metadata({{"key", "val"}});
    writer = NewWriter<double>(test_metadata, FieldDefinition::DOUBLE);

    ASSERT_EQ(writer->Metadata(), test_metadata);

    writer->SetMetadata(unordered_map<string, string>());
    ASSERT_TRUE(writer->Metadata().empty());
}

TEST_F(StreamWriterTest, TestSetMetadataOnInit) {
    set_metadata_on_init = true;
    unordered_map<string, string> test_metadata({{"key", "val"}});
    writer = NewWriter<double>(test_metadata, FieldDefinition::DOUBLE);
    ASSERT_EQ(writer->Metadata(), test_metadata);
}

TEST_F(StreamWriterTest, TestInitializeAfterStop) {
    writer->Stop();
    ASSERT_THROW(writer->Initialize(stream_name, *schema), StreamWriterException);
}

TEST_F(StreamWriterTest, TestInitializeAlreadyExists) {
    auto schema = writer->schema();
    writer->Stop();

    auto writer = new StreamWriter(RedisConnection("127.0.0.1", 6379));
    ASSERT_THROW(writer->Initialize(stream_name, schema), StreamExistsException);
}
