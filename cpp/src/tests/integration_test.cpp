#include "gtest/gtest.h"
#include "../river.h"
#include "../tools/uuid.h"
#include <thread>
#include <random>

using namespace std;
using namespace river;

static const RedisConnection &connection = RedisConnection("127.0.0.1", 6379);
static const int MEAN_JITTER_MS = 20;
static const int STD_JITTER_MS = 10;

class IntegrationTest;

void write_data(shared_ptr<StreamWriter> writer, std::vector<double> write_data, int num_write_data, int64_t *num_written, int num_iterations_to_write) {
    std::random_device rd;
    std::mt19937 mt(rd());
    std::normal_distribution<float> dist(MEAN_JITTER_MS, STD_JITTER_MS);

    *num_written = 0;
    for (unsigned int i = 0; i < num_iterations_to_write; i++) {
        writer->Write(write_data.data(), num_write_data);
        *num_written += num_write_data;
        // Add a random sleep for some jitter for some "fuzz testing"
        std::this_thread::sleep_for(std::chrono::milliseconds(max(0, (int) dist(mt))));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(max(0, (int) dist(mt))));
    writer->Stop();
}

void read_data(shared_ptr<StreamReader> reader, std::vector<double> expected_data, int size_expected_data, int64_t *num_read) {
    *num_read = 0;
    unsigned int num_read_data = 4000;
    vector<double> read_data(num_read_data);
    while (true) {
        auto num_read_this_time = reader->Read(&read_data.front(), num_read_data);
        if (num_read_this_time < 0) {
          break;
        }
        *num_read += num_read_this_time;

        for (unsigned int i = 0; i < num_read_this_time; i++) {
            ASSERT_EQ(expected_data[(reader->total_samples_read() - num_read_this_time + i) % size_expected_data],
                      read_data[i]);
        }
    }
}

void tail_data(shared_ptr<StreamReader> reader) {
    // NB: tail() is sorta undefined when there's an EOF inserted on the stream: there's no guarantee that tail() has
    // seen the element right before the EOF. So, there's not too much we can assert on here.
    double element;
    int64_t total_num_skipped = 0;
    while (reader) {
        int64_t num_skipped = reader->Tail(&element);
        if (num_skipped < 0) {
            return;
        }
        ASSERT_GE(element, 0);
        total_num_skipped += num_skipped;
        ASSERT_EQ(total_num_skipped, reader->total_samples_read());
    }
}


class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        reader = make_shared<StreamReader>(connection);
        reader_tail = make_shared<StreamReader>(connection);
        writer = make_shared<StreamWriter>(connection, 3000);

        stream_name = uuid::generate_uuid_v4();
        num_elements = 1024;
        num_iterations_to_write = 150;
        compute_local_versus_global_clock = false;
    }

    void TearDown() override {
        writer->Stop();
        reader->Stop();
        reader_tail->Stop();
    }

    void run() {
        vector<FieldDefinition> field_definitions = vector<FieldDefinition>{
                FieldDefinition("field1", FieldDefinition::DOUBLE, sizeof(double))
        };

        StreamSchema schema(field_definitions);
        writer->Initialize(stream_name, schema, unordered_map<string, string>(), compute_local_versus_global_clock);
        reader->Initialize(stream_name);
        reader_tail->Initialize(stream_name);

        std::vector<double> data;
        for (unsigned int i = 0; i < num_elements; i++) {
            data.push_back((double) i);
        }

        int64_t num_written = 0;
        int64_t num_read = 0;

        std::thread writer_thread(write_data, writer, data, num_elements, &num_written, num_iterations_to_write);
        std::thread reader_thread(read_data, reader, data, num_elements, &num_read);
        std::thread tail_thread(tail_data, reader_tail);

        writer_thread.join();
        reader_thread.join();
        tail_thread.join();

        ASSERT_EQ(num_read, num_written);
        ASSERT_EQ(num_read, reader->total_samples_read());
        ASSERT_EQ(num_written, writer->total_samples_written());
    }

    shared_ptr<StreamReader> reader;
    shared_ptr<StreamReader> reader_tail;
    shared_ptr<StreamWriter> writer;
    string stream_name;
    string tmp_directory;
    int num_elements;
    int num_iterations_to_write;
    bool compute_local_versus_global_clock;
};

TEST_F(IntegrationTest, TestFull) {
    run();
}

TEST_F(IntegrationTest, TestSmall) {
    num_elements = 1;
    num_iterations_to_write = 1;
    run();
}

TEST_F(IntegrationTest, TestComputeLocalVersusServerGlobal) {
    num_elements = 1;
    num_iterations_to_write = 1;
    compute_local_versus_global_clock = true;
    run();
    // Unfortunately the actual local_minus_server_clock_us can be zero so there's not much to check here other than
    // asserting it can be called and doesn't blow up.
    reader->local_minus_server_clock_us();
}

