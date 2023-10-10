#include "gtest/gtest.h"
#include "../river.h"
#include "../tools/uuid.h"
#include "compression/compressor_types.h"
#include <thread>
#include <random>
#include <glog/logging.h>

using namespace std;
using namespace river;

static const RedisConnection &connection = RedisConnection("127.0.0.1", 6379);
static const int MEAN_JITTER_MS = 20;
static const int STD_JITTER_MS = 10;

class IntegrationTest;

// From https://stackoverflow.com/questions/4792449/c0x-has-no-semaphores-how-to-synchronize-threads
class Semaphore {
    std::mutex mutex_;
    std::condition_variable condition_;
    unsigned long count_ = 0; // Initialized as locked.

public:
    void release() {
        std::lock_guard<decltype(mutex_)> lock(mutex_);
        ++count_;
        condition_.notify_one();
    }

    void acquire() {
        std::unique_lock<decltype(mutex_)> lock(mutex_);
        while(!count_) // Handle spurious wake-ups.
            condition_.wait(lock);
        --count_;
    }

    bool try_acquire() {
        std::lock_guard<decltype(mutex_)> lock(mutex_);
        if(count_) {
            --count_;
            return true;
        }
        return false;
    }
};

void write_data(shared_ptr<StreamWriter> writer, std::vector<double> write_data, int64_t *num_written, int num_iterations_to_write) {
    int num_write_data = write_data.size();
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

void write_data_multi(
    shared_ptr<StreamWriter> writer,
    std::vector<double> write_data,
    int64_t *num_written,
    int num_iterations_to_write,
    int pool_size) {

    const int num_write_data = write_data.size();
    std::random_device rd;
    std::mt19937 mt(rd());
    std::normal_distribution<float> dist(MEAN_JITTER_MS, STD_JITTER_MS);

    std::mutex preparation_lock;

    // Tracks the number of threads currently sending any data
    Semaphore sending_semaphore;
    for (int i = 0; i < pool_size; ++i) {
        sending_semaphore.release();
    }

    const int64_t num_samples = num_iterations_to_write * num_write_data;
    *num_written = 0;

    std::function<void()> send_data([
                                        &preparation_lock,
                                        num_samples,
                                        &num_written,
                                        &write_data,
                                        &dist, &mt,
                                        num_write_data,
                                        pool_size,
                                        &sending_semaphore,
                                        &writer]() mutable {
        while (true) {
            std::optional<PreparedData> prepared_data;
            {
                std::scoped_lock<std::mutex> guard(preparation_lock);

                if (*num_written >= num_samples) {
                    break;
                }

                prepared_data = writer->PrepareBytes(
                    reinterpret_cast<const char *>(write_data.data()), num_write_data);
                std::this_thread::sleep_for(std::chrono::milliseconds(max(0, (int) dist(mt))));
                if (!prepared_data) {
                    // Failed to prepare; must mean we need to halt all sends...
                    // Acquire all of them, thus guaranteeing nobody is sending right now
                    for (int i = 0; i < pool_size; ++i) {
                        sending_semaphore.acquire();
                    }
                    writer->WriteBytes(
                        reinterpret_cast<const char *>(write_data.data()), num_write_data);
                    *num_written += num_write_data;

                    // Restore the pool to how it was now
                    for (int i = 0; i < pool_size; ++i) {
                        sending_semaphore.release();
                    }
                }
            }

            if (!prepared_data) {
                // Already handled this above; nothing else to send.
                continue;
            }

            sending_semaphore.acquire();
            // Add a random sleep for some jitter for some "fuzz testing"
            std::this_thread::sleep_for(std::chrono::milliseconds(max(0, (int) dist(mt))));
            int sent = writer->SendPrepared(prepared_data.value());
            ASSERT_EQ(sent, num_write_data);
            *num_written += num_write_data;
            sending_semaphore.release();
        }
    });

    std::vector<std::thread> threads;
    for (int i = 0; i < pool_size; ++i) {
        threads.emplace_back(send_data);
    }
    for (auto &t : threads) {
        t.join();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(max(0, (int) dist(mt))));
    writer->Stop();
}

void read_data(shared_ptr<StreamReader> reader, std::vector<double> expected_data, int64_t *num_read) {
    int size_expected_data = expected_data.size();

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

void read_and_tail_data(shared_ptr<StreamReader> reader, std::vector<double> expected_data) {
    int size_expected_data = expected_data.size();

    double element;
    int64_t total_num_skipped = 0;
    int64_t total_num_read = 0;
    while (reader) {
        int64_t num_skipped = reader->Tail(&element);
        if (num_skipped < 0) {
            return;
        }
        total_num_skipped += num_skipped;
        ASSERT_EQ(total_num_skipped + total_num_read, reader->total_samples_read());
        ASSERT_EQ(expected_data[(reader->total_samples_read() - 1) % size_expected_data],
                  element);

        int64_t num_read_this_time = reader->Read(&element, 1);
        if (num_read_this_time < 0) {
            break;
        }
        ASSERT_EQ(num_read_this_time, 1);
        total_num_read++;
        ASSERT_EQ(total_num_skipped + total_num_read, reader->total_samples_read());
        ASSERT_EQ(expected_data[(reader->total_samples_read() - 1) % size_expected_data],
                  element);
    }
}


class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
        writer->Stop();
        reader->Stop();
        reader_tail->Stop();
        reader_read_and_tail->Stop();
    }

    void run() {
        reader = make_shared<StreamReader>(connection);
        reader_tail = make_shared<StreamReader>(connection);
        reader_read_and_tail = make_shared<StreamReader>(connection);
        writer = make_shared<StreamWriter>(StreamWriterParamsBuilder()
                                               .connection(connection)
                                               .keys_per_redis_stream(3000)
                                               .compression(StreamCompression(compression_type, compression_params))
                                               .pool_size(writing_pool_size)
                                               .build());

        stream_name = uuid::generate_uuid_v4();
        num_elements = 1024;
        num_iterations_to_write = 150;
        compute_local_versus_global_clock = false;

        vector<FieldDefinition> field_definitions = vector<FieldDefinition>{
                FieldDefinition("field1", FieldDefinition::DOUBLE, sizeof(double))
        };

        StreamSchema schema(field_definitions);
        writer->Initialize(stream_name, schema, unordered_map<string, string>(), compute_local_versus_global_clock);
        reader->Initialize(stream_name);
        reader_tail->Initialize(stream_name);
        reader_read_and_tail->Initialize(stream_name);

        std::vector<double> data;
        for (unsigned int i = 0; i < num_elements; i++) {
            data.push_back((double) i);
        }

        int64_t num_written = 0;
        int64_t num_read = 0;

        std::vector<std::thread> writer_threads;
        if (writing_pool_size == 1) {
            writer_threads.emplace_back(write_data, writer, data, &num_written, num_iterations_to_write);
        } else {
            writer_threads.emplace_back(write_data_multi,
                                        writer,
                                        data,
                                        &num_written,
                                        num_iterations_to_write,
                                        writing_pool_size);
        }

        std::thread reader_thread(read_data, reader, data, &num_read);
        std::thread tail_thread(tail_data, reader_tail);
        std::thread reader_tailer_thread(read_and_tail_data, reader_read_and_tail, data);

        for (auto &t : writer_threads) {
            t.join();
        }
        reader_thread.join();
        tail_thread.join();
        reader_tailer_thread.join();

        ASSERT_EQ(num_read, num_written);
        ASSERT_EQ(num_read, reader->total_samples_read());
        ASSERT_EQ(num_written, writer->total_samples_written());
    }

    shared_ptr<StreamReader> reader;
    shared_ptr<StreamReader> reader_tail;
    shared_ptr<StreamReader> reader_read_and_tail;
    shared_ptr<StreamWriter> writer;
    string stream_name;
    string tmp_directory;
    int num_elements;
    int writing_pool_size = 1;
    int num_iterations_to_write;
    bool compute_local_versus_global_clock;
    StreamCompression::Type compression_type = StreamCompression::Type::UNCOMPRESSED;
    std::unordered_map<std::string, std::string> compression_params;
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

TEST_F(IntegrationTest, TestCompressionLossless) {
    compression_type = StreamCompression::Type::ZFP_LOSSLESS;
    compression_params = {
        {"data_type", "double"},
        {"num_cols", "1"},
    };
    run();
}

TEST_F(IntegrationTest, TestCompressionNearLossless) {
    // While this is technically "lossy", the documentation of ZFP reports that a tolerance of 0.0 is near-lossless;
    // for our cases, we assume that it's lossless in these tests.
    compression_type = StreamCompression::Type::ZFP_LOSSY;
    compression_params = {
        {"data_type", "double"},
        {"num_cols", "1"},
        {"tolerance", "0.0"},
    };
    run();
}

TEST_F(IntegrationTest, TestCompressionDummy) {
    compression_type = StreamCompression::Type::DUMMY;
    compression_params = {
    };
    run();
}

TEST_F(IntegrationTest, Test_ParallelSend) {
    writing_pool_size = 4;
    run();
}
