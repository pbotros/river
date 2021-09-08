#include "gtest/gtest.h"
#include <boost/uuid/uuid.hpp>            // uuid class
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <thread>
#include <random>
#include "ingester_test_utils.h"
#include "ingester.h"
#include "river.h"

using namespace std;
using namespace river;

static const RedisConnection &connection = RedisConnection("127.0.0.1", 6379);
static const int MEAN_JITTER_MS = 20;
static const int STD_JITTER_MS = 10;

class IntegrationTest;
void ingest_data(IntegrationTest *test, const string &stream_name);

void write_data(shared_ptr<StreamWriter> writer, double* write_data, int num_write_data, int64_t *num_written) {
    std::random_device rd;
    std::mt19937 mt(rd());
    std::normal_distribution<float> dist(MEAN_JITTER_MS, STD_JITTER_MS);

    *num_written = 0;
    for (unsigned int i = 0; i < 150; i++) {
        writer->Write(write_data, num_write_data);
        *num_written += num_write_data;
        // Add a random sleep for some jitter for some "fuzz testing"
        std::this_thread::sleep_for(std::chrono::milliseconds(max(0, (int) dist(mt))));
    }

      std::this_thread::sleep_for(std::chrono::milliseconds(max(0, (int) dist(mt))));
      writer->Stop();
}

void read_data(shared_ptr<StreamReader> reader, double *expected_data, int64_t size_expected_data, int64_t *num_read) {
    *num_read = 0;
    unsigned int num_read_data = 4000;
    vector<double> read_data(num_read_data);
    while (true) {
        auto num_read_this_time = reader->Read(&read_data.front(), num_read_data);
        if (num_read_this_time < 0) {
            break;
        }
        *num_read += num_read_this_time;

        for (int64_t i = 0; i < num_read_this_time; i++) {
            ASSERT_EQ(expected_data[(reader->total_samples_read() - num_read_this_time + i) % size_expected_data],
                      read_data[i]);
        }
    }
}

void tail_data(shared_ptr<StreamReader> reader) {
    // NB: tail() is sorta undefined when there's an EOF inserted on the stream: there's no guarantee that tail() has
    // seen the element right before the EOF. So, there's not too much we can assert on here.
    double element;
    while (reader) {
        int num_skipped = reader->Tail(&element);
        if (num_skipped < 0) {
            LOG(INFO) << "Tail terminating." << endl;
            return;
        }
        ASSERT_GE(element, 0);
    }
}


class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        reader = make_shared<StreamReader>(connection);
        reader_tail = make_shared<StreamReader>(connection);
        writer = make_shared<StreamWriter>(connection, 3000);

        tmp_directory = (boost::filesystem::temp_directory_path() / boost::filesystem::unique_path())
                .make_preferred().string();
        boost::filesystem::create_directory(tmp_directory);

        boost::uuids::uuid uuid = boost::uuids::random_generator()();
        stringstream ss;
        ss << uuid;
        stream_name = ss.str();
        bool terminated = false;

        // Set our stalling timeout as 1 STD above the mean, i.e. ~16% of requests should timeout. In reality, this
        // "stalling" timeout should be much higher, in order to capture streams that haven't been properly terminated.
        ingester_factory = [&] {
            return make_unique<StreamIngester>(
                    connection,
                    tmp_directory,
                    &terminated,
                    stream_name,
                    2000,
                    3,
                    (int) MEAN_JITTER_MS + STD_JITTER_MS,
                    1000);
        };
    }

    void TearDown() override {
        if (!tmp_directory.empty()) {
            boost::filesystem::remove_all(tmp_directory);
        }
    }

    void run() {
        vector<FieldDefinition> field_definitions = vector<FieldDefinition>{
                FieldDefinition("field1", FieldDefinition::DOUBLE, sizeof(double))
        };

        StreamSchema schema(field_definitions);
        writer->Initialize(stream_name, schema);
        reader->Initialize(stream_name);
        reader_tail->Initialize(stream_name);

        static const int NUM_ELEMENTS = 1024;
        vector<double> data(NUM_ELEMENTS);
        for (int i = 0; i < NUM_ELEMENTS; i++) {
            data[i] = (double) i;
        }

        int64_t num_written = 0;
        int64_t num_read = 0;

        std::thread writer_thread(write_data, writer, &data.front(), NUM_ELEMENTS, &num_written);
        std::thread reader_thread(read_data, reader, &data.front(), NUM_ELEMENTS, &num_read);
        std::thread tail_thread(tail_data, reader_tail);
        std::thread ingest_thread(ingest_data, this, stream_name);

        writer_thread.join();
        reader_thread.join();
        tail_thread.join();
        ingest_thread.join();

        ASSERT_EQ(num_read, num_written);
        ASSERT_EQ(num_read, reader->total_samples_read());
        ASSERT_EQ(num_written, writer->total_samples_written());

        std::shared_ptr<arrow::Table> table = internal::read_data_file(tmp_directory, stream_name);
        const shared_ptr<arrow::ChunkedArray> &field1_col = table->GetColumnByName("field1");
        ASSERT_EQ(field1_col->length(), reader->total_samples_read());
        vector<double> field1_values;
        for (const auto& chunk : field1_col->chunks()) {
            auto array = std::static_pointer_cast<arrow::DoubleArray>(chunk->View(arrow::float64()).ValueOrDie());
            for (int i = 0; i < array->length(); i++) {
                field1_values.push_back(array->Value(i));
            }
        }

        for (unsigned int ingested_idx = 0; ingested_idx < num_written; ingested_idx++) {
            ASSERT_DOUBLE_EQ(field1_values[ingested_idx], data[ingested_idx % NUM_ELEMENTS]);
        }

        boost::property_tree::ptree root;
        boost::property_tree::read_json(
                (boost::filesystem::path(tmp_directory) / stream_name / "metadata.json").make_preferred().string(),
                root);
        ASSERT_STREQ(root.get<string>("ingestion_status").c_str(), "COMPLETED");

        // Metadata key in Redis no longer exists
        unique_ptr<internal::Redis> redis = internal::Redis::Create(connection);
        ASSERT_FALSE(redis->GetMetadata(stream_name));
    }

    shared_ptr<StreamReader> reader;
    shared_ptr<StreamReader> reader_tail;
    shared_ptr<StreamWriter> writer;
    string stream_name;
    string tmp_directory;
public:
    boost::function<unique_ptr<StreamIngester>()> ingester_factory;
};

void ingest_data(IntegrationTest *test, const string &stream_name) {
    while (true) {
        auto ingester = test->ingester_factory();
        ingester->Ingest();
        ingester->Stop();
        auto maybe_result = ingester->GetResult(stream_name);
        if (maybe_result &&
            (*maybe_result).which() == 1 &&
            boost::get<StreamIngestionResult>(*maybe_result) == StreamIngestionResult::COMPLETED) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

TEST_F(IntegrationTest, TestFull) {
    run();
}
