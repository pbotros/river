#include "gtest/gtest.h"
#include <glog/logging.h>
#include <boost/uuid/uuid.hpp>            // uuid class
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "river.h"
#include "ingester_test_utils.h"
#include "ingester.h"

using namespace std;
using namespace river;

static const int NUM_ELEMENTS = 10240;
static const RedisConnection& connection = RedisConnection("127.0.0.1", 6379);

class StreamIngesterTest : public ::testing::Test {
protected:
    void SetUp() override {
        boost::uuids::uuid uuid = boost::uuids::random_generator()();
        stringstream ss;
        ss << uuid;
        stream_name = ss.str();

        tmp_directory = (boost::filesystem::temp_directory_path() / boost::filesystem::unique_path())
                .make_preferred().string();
        boost::filesystem::create_directory(tmp_directory);
        stream_directory = (boost::filesystem::path(tmp_directory) / stream_name).make_preferred().string();

        redis = internal::Redis::Create(connection);
        test_tombstone = false;
        terminated = false;
    }

    void TearDown() override {
        if (!tmp_directory.empty()) {
            boost::filesystem::remove_all(tmp_directory);
        }
    }

    shared_ptr<StreamIngester> new_ingester(int samples_per_row_group) {
        return make_shared<StreamIngester>(connection,
                                           tmp_directory,
                                           &terminated,
                                           stream_name,
                                           samples_per_row_group,
                                           1,
                                           1000,
                                           10000);
    }

    boost::property_tree::ptree read_metadata_json() {
        boost::property_tree::ptree root;
        boost::property_tree::read_json(
                (boost::filesystem::path(stream_directory) /
                 "metadata.json").make_preferred().string(), root);
        return root;
    }

    string stream_name;
    string tmp_directory;
    string stream_directory;
    bool test_tombstone;
    bool terminated;
    unique_ptr<internal::Redis> redis;

    template <class T, class U>
    void write_and_assert(FieldDefinition::Type type) {
        class Visitor : public arrow::ArrayVisitor {
            unsigned int expected = 0;
            arrow::Status Visit(const U &array) override {
                for (unsigned int i = 0; i < array.length(); i++) {
                    EXPECT_EQ(array.Value(i), (T) expected++);
                }
                return arrow::Status::OK();
            }
        };
        auto visitor = new Visitor();
        write_and_assert<T>(type, visitor);
    }

    template<class T>
    void write_and_assert(FieldDefinition::Type type, arrow::ArrayVisitor *visitor) {
        auto *data = new T[NUM_ELEMENTS];
        for (int i = 0; i < NUM_ELEMENTS; i++) {
            data[i] = (T) i;
        }

        vector<FieldDefinition> field_definitions = vector<FieldDefinition>{
                FieldDefinition("field1", type, sizeof(T))
        };

        StreamSchema schema(field_definitions);
        unique_ptr<StreamWriter> writer;
        if (test_tombstone) {
            writer = make_unique<StreamWriter>(connection, NUM_ELEMENTS / 2);
        } else {
            writer = make_unique<StreamWriter>(connection);
        }
        writer->Initialize(stream_name, schema);
        writer->SetMetadata(unordered_map<string, string>({{"foo", "bar"}}));
        writer->Write(data, NUM_ELEMENTS);
        writer->Stop();

        // Use a reduced row group size and lookback time to test
        auto ingester = new_ingester(NUM_ELEMENTS / 4);
        ingester->Ingest();
        ingester->Stop();
        auto result = boost::get<StreamIngestionResult>(*ingester->GetResult(stream_name));
        ASSERT_EQ(result, StreamIngestionResult::COMPLETED);

        std::shared_ptr<arrow::Table> table = internal::read_data_file(tmp_directory, stream_name);

        auto sample_index_column = table->GetColumnByName("sample_index");
        ASSERT_TRUE(sample_index_column);
        ASSERT_EQ(sample_index_column->length(), NUM_ELEMENTS);
        int64_t expected_sample_index = 0;
        for (const auto& chunk : sample_index_column->chunks()) {
            auto chunk_int = static_pointer_cast<arrow::Int64Array>(chunk->Slice(0));
            for (int64_t i = 0; i < chunk_int->length(); i++) {
                ASSERT_EQ(chunk_int->Value(i), expected_sample_index++);
            }
        }

        auto key_column = table->GetColumnByName("key");
        ASSERT_TRUE(key_column);
        ASSERT_EQ(key_column->length(), NUM_ELEMENTS);

        string previous_key;
        bool first_key = true;
        for (const auto& chunk : key_column->chunks()) {
            auto key_array = static_pointer_cast<arrow::StringArray>(chunk->Slice(0));
            for (int64_t i = 0; i < key_array->length(); i++) {
                string key = key_array->GetString(i);

                if (first_key) {
                    previous_key = key;
                    first_key = false;
                    continue;
                }

                unsigned long long actual_left, actual_right, previous_left, previous_right;
                decode_key(key, &actual_left, &actual_right);
                decode_key(previous_key, &previous_left, &previous_right);

                if (actual_left == previous_left) {
                    ASSERT_GT(actual_right, previous_right);
                } else {
                    ASSERT_GT(actual_left, previous_left);
                }
            }
        }

        auto column = table->GetColumnByName("field1");
        ASSERT_TRUE(column);

        ASSERT_EQ(column->length(), NUM_ELEMENTS);
        for (const auto& chunk : column->chunks()) {
            PARQUET_THROW_NOT_OK(chunk->Accept(visitor));
        }

        auto metadata = redis->GetMetadata(stream_name);
        ASSERT_FALSE(metadata);

        boost::property_tree::ptree root = read_metadata_json();
        ASSERT_STREQ(root.get<string>("stream_name").c_str(), stream_name.c_str());
        ASSERT_TRUE(root.get_optional<string>("local_minus_server_clock_us"));
        ASSERT_TRUE(root.get_optional<string>("initialized_at_us"));
        ASSERT_STREQ(root.get<string>("foo").c_str(), "bar");
        ASSERT_STREQ(root.get<string>("ingestion_status").c_str(), "COMPLETED");
    }

    static void decode_key(const string& key, unsigned long long *left, unsigned long long *right) {
        unsigned long delimiter_index = key.rfind('-');
        string new_left_str = key.substr(0, delimiter_index);
        string new_right_str = key.substr(delimiter_index + 1);
        *left = strtoull(new_left_str.c_str(), nullptr, 10);
        *right = strtoull(new_right_str.c_str(), nullptr, 10);
    }
};

TEST_F(StreamIngesterTest, TestDouble) {
    write_and_assert<double, arrow::DoubleArray>(FieldDefinition::DOUBLE);
}

TEST_F(StreamIngesterTest, TestDoubleWithTombstone) {
    test_tombstone = true;
    write_and_assert<double, arrow::DoubleArray>(FieldDefinition::DOUBLE);
}

TEST_F(StreamIngesterTest, TestFloat) {
    write_and_assert<float, arrow::FloatArray>(FieldDefinition::FLOAT);
}

TEST_F(StreamIngesterTest, TestInt) {
    write_and_assert<int, arrow::Int32Array>(FieldDefinition::INT32);
}

TEST_F(StreamIngesterTest, TestLongLong) {
    write_and_assert<long long, arrow::Int64Array>(FieldDefinition::INT64);
}

TEST_F(StreamIngesterTest, TestBinary) {
    class Visitor : public arrow::ArrayVisitor {
        unsigned int expected = 0;
        arrow::Status Visit(const arrow::FixedSizeBinaryArray &array) override {
            for (unsigned int i = 0; i < array.length(); i++) {
                EXPECT_EQ(*array.Value(i), (unsigned char) expected++);
            }
            return arrow::Status::OK();
        }
    };
    auto visitor = new Visitor();
    write_and_assert<unsigned char>(FieldDefinition::FIXED_WIDTH_BYTES, visitor);
}

TEST_F(StreamIngesterTest, TestVariableBinary) {
    const char *data[] = {
            "This",
            "is",
            "a",
            "test",
            ""
    };

    vector<FieldDefinition> field_definitions = vector<FieldDefinition>{
            FieldDefinition("field1", FieldDefinition::VARIABLE_WIDTH_BYTES, 100)
    };

    StreamSchema schema(field_definitions);
    auto writer = make_unique<StreamWriter>(connection);
    writer->Initialize(stream_name, schema);
    for (auto &word : data) {
        int size = static_cast<int>(strlen(word));
        writer->WriteBytes(word, 1, &size);
    }
    writer->Stop();

    auto ingester = new_ingester(10);
    ingester->Ingest();
    ingester->Stop();

    auto table = internal::read_data_file(tmp_directory, stream_name);
    auto column = table->GetColumnByName("field1");
    ASSERT_NE(column, nullptr);

    for (const auto& chunk : column->chunks()) {
        auto array = std::static_pointer_cast<arrow::BinaryArray>(chunk);
        for (int data_idx = 0; data_idx < chunk->length(); data_idx++) {
            int out_length;
            auto out_buf = array->GetValue(data_idx, &out_length);
            ASSERT_EQ(out_length, strlen(data[data_idx]));
            vector<char> out_str(out_length + 1);
            memcpy(&out_str.front(), out_buf, out_length);
            out_str[out_length] = '\0';
            string out_ss = string(&out_str.front());
            ASSERT_STREQ(out_ss.c_str(), data[data_idx]);
        }
    }
}

TEST_F(StreamIngesterTest, TestMultipleFields) {
    vector<FieldDefinition> field_definitions = vector<FieldDefinition>{
            FieldDefinition("field1", FieldDefinition::INT32, 4),
            FieldDefinition("field2", FieldDefinition::INT32, 4),
            FieldDefinition("field3", FieldDefinition::INT64, 8)
    };
    typedef struct {
        int32_t val1;
        int32_t val2;
        int64_t val3;
    } TestData;
    TestData test_data;
    test_data.val1 = 1;
    test_data.val2 = 2;
    test_data.val3 = 3;

    StreamSchema schema(field_definitions);
    auto writer = make_unique<StreamWriter>(connection);
    writer->Initialize(stream_name, schema);
    writer->Write(&test_data, 1);
    writer->Stop();

    auto ingester = new_ingester(10);
    ingester->Ingest();
    ingester->Stop();

    auto table = internal::read_data_file(tmp_directory, stream_name);
    auto col1 = static_pointer_cast<arrow::Int32Array>(table->GetColumnByName("field1")->chunk(0));
    auto col2 = static_pointer_cast<arrow::Int32Array>(table->GetColumnByName("field2")->chunk(0));
    auto col3 = static_pointer_cast<arrow::Int64Array>(table->GetColumnByName("field3")->chunk(0));

    ASSERT_EQ(col1->length(), 1);
    ASSERT_EQ(col2->length(), 1);
    ASSERT_EQ(col3->length(), 1);

    ASSERT_EQ(col1->Value(0), 1);
    ASSERT_EQ(col2->Value(0), 2);
    ASSERT_EQ(col3->Value(0), 3);
}

TEST_F(StreamIngesterTest, TestPartial) {
    vector<FieldDefinition> field_definitions = vector<FieldDefinition>{
            FieldDefinition("field1", FieldDefinition::DOUBLE, 8)
    };

    StreamSchema schema(field_definitions);
    auto writer = make_unique<StreamWriter>(connection);
    writer->Initialize(stream_name, schema);
    int num_elements_to_write = 10;
    for (int i = 0; i < num_elements_to_write; i++) {
        double d = i;
        writer->Write(&d, 1);
    }
    writer->SetMetadata({{"foo1", "bar1"}});

    auto ingester = new_ingester(20);
    // This should time out after it ingests the 10 samples, and successfully persist the 10.
    ingester->Ingest();
    ingester->Stop();
    auto result = boost::get<StreamIngestionResult>(*ingester->GetResult(stream_name));
    ASSERT_EQ(result, StreamIngestionResult::IN_PROGRESS);

    boost::property_tree::ptree root = read_metadata_json();
    ASSERT_STREQ(root.get<string>("ingestion_status").c_str(), "IN_PROGRESS");

    auto table = internal::read_data_file(tmp_directory, stream_name);
    ASSERT_FALSE(table);

    // Write another 10 elements and EOF
    for (int i = num_elements_to_write; i < 2 * num_elements_to_write; i++) {
        double d = i;
        writer->Write(&d, 1);
    }
    writer->SetMetadata({{"foo2", "bar2"}});
    writer->Stop();

    ingester = new_ingester(20);
    ingester->Ingest();
    ingester->Stop();

    table = internal::read_data_file(tmp_directory, stream_name);
    auto column = table->GetColumnByName("field1");
    ASSERT_TRUE(column);
    ASSERT_EQ(column->length(), 2 * num_elements_to_write);

    auto data_idx = 0;
    for (const auto& chunk : column->chunks()) {
        auto array = std::static_pointer_cast<arrow::DoubleArray>(chunk);
        for (int i = 0; i < chunk->length(); i++) {
            double val = array->Value(i);
            ASSERT_DOUBLE_EQ(val, data_idx);
            data_idx++;
        }
    }

    root = read_metadata_json();
    ASSERT_STREQ(root.get<string>("ingestion_status").c_str(), "COMPLETED");
    ASSERT_STREQ(root.get<string>("foo1").c_str(), "bar1");
    ASSERT_STREQ(root.get<string>("foo2").c_str(), "bar2");
}

