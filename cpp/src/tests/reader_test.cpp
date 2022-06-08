#include "gtest/gtest.h"
#include "../river.h"
#include <thread>
#include <chrono>
#include <fmt/format.h>
#include <glog/logging.h>
#include "../tools/uuid.h"

using namespace std;
using namespace river;

static const int NUM_ELEMENTS = 256;

class TestStreamReaderListener : public internal::StreamReaderListener {
public:
    vector<string> old_stream_keys;
    vector<string> new_stream_keys;

    void OnStreamKeyChange(const string &old_stream_key, const string &new_stream_key) override {
        old_stream_keys.push_back(old_stream_key);
        new_stream_keys.push_back(new_stream_key);
    }
};

class StreamReaderTest : public ::testing::Test {
public:
    void SetUp() override {
        regenerate_stream_name();
        struct timeval timeout = {1, 500000};
        redis = redisConnectWithTimeout("127.0.0.1", 6379, timeout);

        reader_ = NewStreamReader<int>();
    }

    void regenerate_stream_name() {
        stream_name = uuid::generate_uuid_v4();
    }

    template <class T> shared_ptr<StreamReader> NewStreamReader() {
        return NewStreamReader<T>(10000);
    }

    template <class T> shared_ptr<StreamReader> NewStreamReader(int max_fetch_size) {
        // NB: most tests (i.e. ones that aren't variable binary) don't use the field definition, so set arbitrarily
        return NewStreamReader<T>(max_fetch_size, FieldDefinition::INT64);
    }

    template<class T>
    shared_ptr<StreamReader> NewStreamReader(int max_fetch_size, FieldDefinition::Type type) {
        set_necessary_metadata<T>(type);
        return make_unique<StreamReader>(RedisConnection("127.0.0.1", 6379), max_fetch_size);
    }

     void TearDown() override {
         reader_->Stop();
     }

    shared_ptr<StreamReader> reader_;
    string stream_name;
    redisContext *redis;

    template <class T>
    void xadd_sample(int stream_key_suffix, int i, T *val_bytes, size_t val_size) {
        xadd_sample(stream_key_suffix, i, val_bytes, val_size, "*");
    }

    template <class T>
    void xadd_sample(int stream_key_suffix, int i, T *val_bytes, size_t val_size, const string& key) {
        redisCommand(redis,
                     "XADD %s-%d %s i %d val %b",
                     stream_name.c_str(),
                     stream_key_suffix,
                     key.c_str(),
                     i,
                     reinterpret_cast<char *>(val_bytes),
                     val_size);
    }


    template <class T>
    void assert_read_works(T *data, size_t size, void (*checker)(T, int)) {
        assert_read_works(NewStreamReader<T>(), data, size, checker);
    }

    template <class T>
    void assert_read_works(shared_ptr<StreamReader> reader, T *data, size_t size, void (*checker)(T, int)) {
        for (int i = 0; i < NUM_ELEMENTS; i++) {
            xadd_sample(0, i, reinterpret_cast<char *>(&data[i]), size);
        }

        reader->Initialize(stream_name);

        T read_data[NUM_ELEMENTS];
        int *read_data_sizes = new int[NUM_ELEMENTS];
        int64_t num_read = reader->Read(read_data, NUM_ELEMENTS, &read_data_sizes);
        ASSERT_EQ(num_read, NUM_ELEMENTS);
        for (int i = 0; i < NUM_ELEMENTS; i++) {
            ASSERT_EQ(read_data_sizes[i], sizeof(T));
            checker(read_data[i], i);
        }
        delete[] read_data_sizes;
    }

  void write_tombstone(int64_t index, const char *key = "*") {
    redisCommand(redis,
                 "XADD %s-0 %s tombstone 1 next_stream_key %s-1 sample_index %llu",
                 stream_name.c_str(),
                 key,
                 stream_name.c_str(),
                 index);
  }

  void write_eof(int64_t index, int stream_suffix = 0, const char *key = "*") {
    redisCommand(this->redis,
                 "XADD %s-%d %s eof 1 sample_index %llu",
                 this->stream_name.c_str(),
                 stream_suffix,
                 key,
                 index);
  }

public:
    template <class T>
    void set_necessary_metadata(FieldDefinition::Type type) {
        auto now_us = chrono::duration_cast<std::chrono::microseconds>(
                chrono::system_clock::now().time_since_epoch()).count();
        redisCommand(redis, "HSET %s-metadata "
                            "first_stream_key %s-0 "
                            "local_minus_server_clock_us 0 "
                            "initialized_at_us %llu "
                            "user_metadata %s",
                     stream_name.c_str(), stream_name.c_str(), now_us, "{}");

        vector<FieldDefinition> field_definitions;
        const FieldDefinition &def = FieldDefinition("field1", type, sizeof(T));
        field_definitions.push_back(def);
        StreamSchema schema(field_definitions);
        redisCommand(redis, "HSET %s-metadata schema %s", stream_name.c_str(), schema.ToJson().c_str());
    }
};

TEST_F(StreamReaderTest, TestDouble) {
    double data[NUM_ELEMENTS];
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        data[i] = i;
    }
    assert_read_works<double>(data, sizeof(double), [](double element, int index) {
        ASSERT_DOUBLE_EQ(element, (double) index);
    });
}

TEST_F(StreamReaderTest, TestFloat) {
    float data[NUM_ELEMENTS];
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        data[i] = i;
    }
    assert_read_works<float>(data, sizeof(float), [](float element, int index) {
        ASSERT_FLOAT_EQ(element, (float) index);
    });
}

TEST_F(StreamReaderTest, TestLongLongInt) {
    uint64_t data[NUM_ELEMENTS];
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        data[i] = i;
    }
    assert_read_works<uint64_t>(data, sizeof(uint64_t),
                                              [](uint64_t element, int index) {
                                                  ASSERT_EQ(element, static_cast<uint64_t>(index));
                                              });
}

TEST_F(StreamReaderTest, TestInteger) {
    int data[NUM_ELEMENTS];
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        data[i] = i;
    }
    assert_read_works<int>(data, sizeof(int), [](int element, int index) {
        ASSERT_EQ(element, (int) index);
    });
}

TEST_F(StreamReaderTest, TestVariableBinaryRequiresSizes) {
    shared_ptr<StreamReader> r = NewStreamReader<char>(10000, FieldDefinition::VARIABLE_WIDTH_BYTES);
    r->Initialize(stream_name);
    char read_data[1];
  int64_t ret = r->Read(read_data, NUM_ELEMENTS, nullptr);
  ASSERT_EQ(ret, -1);
    r->Stop();
}

TEST_F(StreamReaderTest, TestVariableBinary) {
    // Create an array of [<1 byte>, <2 bytes>, ..., <NUM_ELEMENT - 1 bytes>, <NUM_ELEMENT bytes>]
    // where each segment is [0, ..., <num bytes - 1>]
    size_t sizes[NUM_ELEMENTS];
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        sizes[i] = i + 1;
        vector<char> data(i + 1);
        for (int j = 0; j <= i; j++) {
            data[j] = (char) j;
        }
        xadd_sample(0, i, &data.front(), sizes[i]);
    }

    shared_ptr<StreamReader> r = NewStreamReader<char>(10000, FieldDefinition::VARIABLE_WIDTH_BYTES);
    r->Initialize(stream_name);

    char read_data[NUM_ELEMENTS * (NUM_ELEMENTS + 1) / 2];
    auto *read_sizes = new int[NUM_ELEMENTS * (NUM_ELEMENTS + 1) / 2];
    r->Read(read_data, NUM_ELEMENTS, &read_sizes);
    unsigned int read_data_idx = 0;
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        ASSERT_EQ(read_sizes[i], i + 1);
        for (int j = 0; j < read_sizes[i]; j++) {
            ASSERT_EQ(read_data[read_data_idx++], (char) j);
        }
    }
    delete[] read_sizes;
}


TEST_F(StreamReaderTest, TestMultipleBatches) {
    int data[NUM_ELEMENTS];
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        data[i] = i;
    }
    shared_ptr<StreamReader> reader = NewStreamReader<int>(NUM_ELEMENTS / 2);
    assert_read_works<int>(reader, data, sizeof(int), [](int element, int index) {
        ASSERT_EQ(element, (int) index);
    });
}


TEST_F(StreamReaderTest, TestTombstone) {
    int data[NUM_ELEMENTS];
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        data[i] = i;
    }

    for (int i = 0; i < NUM_ELEMENTS / 2; i++) {
        xadd_sample(0, i, reinterpret_cast<char *>(&data[i]), sizeof(int));
    }
    write_tombstone(NUM_ELEMENTS / 2 - 1);
    for (int i = NUM_ELEMENTS / 2; i < NUM_ELEMENTS; i++) {
        xadd_sample(1, i, reinterpret_cast<char *>(&data[i]), sizeof(int));
    }

    auto listener = new TestStreamReaderListener();
    reader_->AddListener(listener);
    reader_->Initialize(stream_name);

    int read_data[NUM_ELEMENTS];
    reader_->Read(read_data, NUM_ELEMENTS);
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        ASSERT_EQ(read_data[i], i);
    }

    ASSERT_EQ(listener->old_stream_keys.size(), 2);
    ASSERT_EQ(listener->new_stream_keys.size(), 2);

    ASSERT_TRUE(listener->old_stream_keys.at(0).empty());
    ASSERT_STREQ(listener->new_stream_keys.at(0).c_str(), fmt::format("{}-0", stream_name).c_str());

    ASSERT_STREQ(listener->old_stream_keys.at(1).c_str(), fmt::format("{}-0", stream_name).c_str());
    ASSERT_STREQ(listener->new_stream_keys.at(1).c_str(), fmt::format("{}-1", stream_name).c_str());
}

TEST_F(StreamReaderTest, TestEofEmpty) {
    int data[NUM_ELEMENTS];
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        data[i] = i;
    }

    // EOF occurs just after the first batch, and so the remaining items should be empty.
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        xadd_sample(0, i, reinterpret_cast<char *>(&data[i]), sizeof(int));
    }
    write_eof(NUM_ELEMENTS - 1);

    auto listener = new TestStreamReaderListener();
    reader_->AddListener(listener);
    reader_->Initialize(stream_name);

    int read_data[NUM_ELEMENTS];
    ASSERT_TRUE(reader_->Good());
    int64_t num_read = reader_->Read(read_data, NUM_ELEMENTS);
    ASSERT_EQ(num_read, NUM_ELEMENTS);
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        ASSERT_EQ(read_data[i], i);
    }
    ASSERT_TRUE(reader_->Good());

    // Try to read again - no elements left to read
    num_read = reader_->Read(read_data, NUM_ELEMENTS);
    ASSERT_EQ(num_read, -1);
    ASSERT_FALSE(reader_->Good());

    // EOF key got set when EOF was hit
    ASSERT_FALSE(reader_->eof_key().empty());

    // And any subsequent calls fail
    ASSERT_EQ(reader_->Read(read_data, 1), -1);

    ASSERT_EQ(listener->old_stream_keys.size(), 2);
    ASSERT_EQ(listener->new_stream_keys.size(), 2);

    ASSERT_TRUE(listener->old_stream_keys.at(0).empty());
    ASSERT_STREQ(listener->new_stream_keys.at(0).c_str(), fmt::format("{}-0", stream_name).c_str());

    ASSERT_STREQ(listener->old_stream_keys.at(1).c_str(), fmt::format("{}-0", stream_name).c_str());
    ASSERT_TRUE(listener->new_stream_keys.at(1).empty());
}

TEST_F(StreamReaderTest, TestEofContainsSome) {
    int data[NUM_ELEMENTS];
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        data[i] = i;
    }

    // Place EOF at half
    int num_elements_written = NUM_ELEMENTS / 2;
    for (int i = 0; i < num_elements_written; i++) {
        xadd_sample(0, i, reinterpret_cast<char *>(&data[i]), sizeof(int));
    }
    write_eof(num_elements_written - 1);

    reader_->Initialize(stream_name);

    int read_data[NUM_ELEMENTS];
    ASSERT_TRUE(reader_->Good());
    int num_read = reader_->Read(read_data, NUM_ELEMENTS);

    ASSERT_FALSE(reader_->Good());
    ASSERT_EQ(num_read, num_elements_written);
    for (int i = 0; i < num_elements_written; i++) {
        ASSERT_EQ(read_data[i], i);
    }

    // Subsequent calls fail
    ASSERT_EQ(reader_->Read(read_data, 1), -1);
}

TEST_F(StreamReaderTest, TestXRead) {
    reader_->Initialize(stream_name);

    int first = 0;
    xadd_sample(0, 0, &first, sizeof(int));

    std::thread t([](StreamReaderTest *self) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        int x = 1;
        self->xadd_sample(0, 1, &x, sizeof(int));

        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        int y = 2;
        self->xadd_sample(0, 2, &y, sizeof(int));
    }, this);

    int read_data[3];
    reader_->Read(read_data, 3);
    ASSERT_EQ(read_data[0], 0);
    ASSERT_EQ(read_data[1], 1);
    ASSERT_EQ(read_data[2], 2);
    t.join();
}

TEST_F(StreamReaderTest, TestTimeout_TimesOut) {
    reader_->Initialize(stream_name);

    // Initialize arbitrarily and make sure it didn't change
    int read_data[3] = { -1, -1, -1 };

    int64_t num_read = reader_->Read(read_data, 3, nullptr, nullptr, 1500);
    ASSERT_EQ(num_read, 0);
    ASSERT_EQ(read_data[0], -1);
    ASSERT_EQ(read_data[1], -1);
    ASSERT_EQ(read_data[2], -1);

    int first = 100;
    xadd_sample(0, 0, &first, sizeof(int));

    num_read = reader_->Read(read_data, 3, nullptr, nullptr, 500);
    ASSERT_EQ(num_read, 1);
    ASSERT_EQ(read_data[0], 100);
    ASSERT_EQ(read_data[1], -1);
    ASSERT_EQ(read_data[2], -1);
}

float test_timeout(shared_ptr<StreamReader> reader_, const int timeout) {
    // Initialize arbitrarily and make sure it didn't change
    int read_data[3];

    long long before = chrono::duration_cast<std::chrono::nanoseconds>(
            chrono::high_resolution_clock::now().time_since_epoch()).count();
    reader_->Read(read_data, 3, nullptr, nullptr, timeout);
    long long after = chrono::duration_cast<std::chrono::nanoseconds>(
            chrono::high_resolution_clock::now().time_since_epoch()).count();
    return (after - before) / 1e6;
}

// NB: this test is marked as "DISABLED" since it's pretty finicky and dependent on the system running, ongoing load,
// etc. Enable this by deleting "DISABLED_" prefix to manually test timeout precisions.
TEST_F(StreamReaderTest, DISABLED_TestTimeout_Precision) {
    reader_->Initialize(stream_name);

    const int timeout_jitter_ms = 1;

    // Check for <= 1ms jitter in timeout.
    for (int timeout = 1; timeout < 30; timeout += 2) {
        LOG(INFO) << "Testing timeout " << timeout << "ms..." << endl;
        int missed = 0;
        int num_reps = 20;
        for (int j = 0; j < num_reps; j++) {
            float t = test_timeout(reader_, timeout);
            if (abs(timeout - t) >= timeout_jitter_ms) {
                missed++;
            }
        }
        // Allow <= 20% misses; hopefully it's better than this IRL!
        ASSERT_LE(missed, num_reps / 5);
    }

    for (int timeout = 100; timeout <= 600; timeout += 100) {
        LOG(INFO) << "Testing timeout " << timeout << "ms..." << endl;
        int missed = 0;
        for (int j = 0; j < 5; j++) {
            float t = test_timeout(reader_, timeout);
            if (abs(timeout - t) >= timeout_jitter_ms) {
                missed++;
            }
        }

        // Allow <= 1 misses; hopefully it's better than this IRL!
        ASSERT_LE(missed, 1);
    }
}

TEST_F(StreamReaderTest, TestTail_Simple) {
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        xadd_sample(0, i, reinterpret_cast<char *>(&i), sizeof(int));
    }

    shared_ptr<StreamReader> r = NewStreamReader<int>(10000, FieldDefinition::INT32);
    r->Initialize(stream_name);
    int readout;
    int64_t ret = r->Tail(&readout);

    ASSERT_EQ(readout, NUM_ELEMENTS - 1);
    ASSERT_EQ(ret, NUM_ELEMENTS);
    ASSERT_EQ(r->total_samples_read(), NUM_ELEMENTS);
}

TEST_F(StreamReaderTest, TestTail_SingleElement) {
    shared_ptr<StreamReader> r = NewStreamReader<int>(10000, FieldDefinition::INT32);
    r->Initialize(stream_name);
    int readout;

    // Initial read times out with zero
    ASSERT_EQ(r->Tail(&readout, 100), 0);

    // Add a single element and assert that it says we skipped 1 element.
    int value = 10;
    xadd_sample(0, 0, reinterpret_cast<char *>(&value), sizeof(int));
    ASSERT_EQ(r->Tail(&readout, 100), 1);
    ASSERT_EQ(readout, value);

    // Then it times out with zero returns again
    ASSERT_EQ(r->Tail(&readout, 100), 0);
}

TEST_F(StreamReaderTest, TestTail_ReadThenTails) {
    shared_ptr<StreamReader> r = NewStreamReader<int>(10000, FieldDefinition::INT32);
    r->Initialize(stream_name);
    int readout;

    // Initial read times out with zero
    ASSERT_EQ(r->Tail(&readout, 100), 0);

    // Add three elements; read the first one, tail the last two.
    int value = 10;
    xadd_sample(0, 0, reinterpret_cast<char *>(&value), sizeof(int));
    value = 11;
    xadd_sample(0, 1, reinterpret_cast<char *>(&value), sizeof(int));

    ASSERT_EQ(r->Read(&readout, 1, nullptr, nullptr, 100), 1);
    ASSERT_EQ(readout, 10);

    ASSERT_EQ(r->Tail(&readout, 100), 1);
    ASSERT_EQ(readout, 11);

    value = 12;
    xadd_sample(0, 2, reinterpret_cast<char *>(&value), sizeof(int));
    ASSERT_EQ(r->Tail(&readout, 100), 1);
    ASSERT_EQ(readout, 12);

    // Then it times out with zero returns again
    ASSERT_EQ(r->Tail(&readout, 100), 0);

    ASSERT_EQ(r->total_samples_read(), 3);
}

TEST_F(StreamReaderTest, TestTail_Tombstone) {
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        xadd_sample(0, i, reinterpret_cast<char *>(&i), sizeof(int));
    }
    write_tombstone(NUM_ELEMENTS - 1);
    for (int i = NUM_ELEMENTS; i < 2 * NUM_ELEMENTS; i++) {
        xadd_sample(1, i, reinterpret_cast<char *>(&i), sizeof(int));
    }

    shared_ptr<StreamReader> r = NewStreamReader<int>(10000, FieldDefinition::INT32);
    r->Initialize(stream_name);
    int readout;
    size_t ret = r->Tail(&readout);

    ASSERT_EQ(readout, 2 * NUM_ELEMENTS - 1);
    ASSERT_EQ(ret, 2 * NUM_ELEMENTS);
}

TEST_F(StreamReaderTest, TestTail_EmptyUsesXRead) {
    std::thread writer_thread([](StreamReaderTest *self) {
        // Wait a second for (presumingly) it to be blocking on XREAD. Can be verified with log statements.
        std::this_thread::sleep_for(std::chrono::seconds(1));
        int i = 10;
        self->xadd_sample(0, 0, reinterpret_cast<char *>(&i), sizeof(int));
    }, this);

    shared_ptr<StreamReader> r = NewStreamReader<int>(10000, FieldDefinition::INT32);
    r->Initialize(stream_name);
    int readout;
    int64_t ret = r->Tail(&readout);
    writer_thread.join();

    ASSERT_EQ(readout, 10);
    ASSERT_EQ(ret, 1);
}

TEST_F(StreamReaderTest, TestTail_MovesCursor) {
    shared_ptr<StreamReader> r = NewStreamReader<int>(10000, FieldDefinition::INT32);
    r->Initialize(stream_name);

    for (int i = 0; i < NUM_ELEMENTS; i++) {
        xadd_sample(0, i, reinterpret_cast<char *>(&i), sizeof(int));
    }

    int readout;
    int64_t ret = r->Tail(&readout);

    ASSERT_EQ(readout, NUM_ELEMENTS - 1);
    ASSERT_EQ(ret, NUM_ELEMENTS);

    // THEN add another round of elements and make sure the read() picks up after the tail().
    for (int i = NUM_ELEMENTS; i < 2 * NUM_ELEMENTS; i++) {
        xadd_sample(0, i, reinterpret_cast<char *>(&i), sizeof(int));
    }

    int read_buf[NUM_ELEMENTS];
    int64_t num_read = r->Read(read_buf, NUM_ELEMENTS);
    ASSERT_EQ(num_read, NUM_ELEMENTS);

    for (int i = 0; i < NUM_ELEMENTS; i++) {
        ASSERT_EQ(read_buf[i], i + NUM_ELEMENTS);
    }
}

TEST_F(StreamReaderTest, TestTail_HandlesEof) {
    shared_ptr<StreamReader> r = NewStreamReader<int>(10000, FieldDefinition::INT32);
    r->Initialize(stream_name);

    for (int i = 0; i < NUM_ELEMENTS; i++) {
        xadd_sample(0, i, reinterpret_cast<char *>(&i), sizeof(int));
    }
    write_eof(NUM_ELEMENTS - 1);

    int readout;
    ASSERT_EQ(r->Tail(&readout), -1);
}

TEST_F(StreamReaderTest, TestTail_TimesOut) {
    shared_ptr<StreamReader> r = NewStreamReader<int>(10000, FieldDefinition::INT32);
    r->Initialize(stream_name);
    int readout = 100;
    int64_t ret = r->Tail(&readout, 100);

    ASSERT_EQ(readout, 100); // unchanged
    ASSERT_EQ(ret, 0);
}

TEST_F(StreamReaderTest, TestTail_BlocksForData) {
    shared_ptr<StreamReader> r = NewStreamReader<int>(10000, FieldDefinition::INT32);
    r->Initialize(stream_name);

    std::thread writer_thread([](StreamReaderTest *self) {
        // Wait a second for (presumingly) it to be blocking on XREAD. Can be verified with log statements.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        int i = 10;
        self->xadd_sample(0, 0, reinterpret_cast<char *>(&i), sizeof(int));
    }, this);

    int readout = 100;
    int64_t ret = r->Tail(&readout, 10000);
    writer_thread.join();

    ASSERT_EQ(readout, 10); // unchanged
    ASSERT_EQ(ret, 1);
}

TEST_F(StreamReaderTest, TestInitialize) {
    regenerate_stream_name();
    auto *reader = new StreamReader(RedisConnection("127.0.0.1", 6379));
    // Doesn't exist, no timeout i.e. no retries => throws
    ASSERT_THROW(reader->Initialize(stream_name), StreamDoesNotExistException);
    ASSERT_FALSE(reader->is_initialized());

    // Doesn't exist, even with timeout => throws
    ASSERT_THROW(reader->Initialize(stream_name, 500), StreamDoesNotExistException);
    ASSERT_FALSE(reader->is_initialized());

    std::thread writer_thread([](StreamReaderTest *test) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        test->set_necessary_metadata<int>(FieldDefinition::Type::INT32);
    }, this);
    reader->Initialize(stream_name, 5000);
    writer_thread.join();
    ASSERT_TRUE(reader->is_initialized());
}

TEST_F(StreamReaderTest, TestSeek) {

    auto listener = new TestStreamReaderListener();
    reader_->AddListener(listener);
    reader_->Initialize(stream_name);

    ASSERT_TRUE(listener->old_stream_keys.at(0).empty());
    ASSERT_STREQ(listener->new_stream_keys.at(0).c_str(), fmt::format("{}-0", stream_name).c_str());

    int read;

    // Write 10 keys with a manually specified redis key, so we can have fine precision for testing seeks
    for (int id = 10; id < 10 + 10; id++) {
        xadd_sample(0, id - 10, reinterpret_cast<char *>(&id), sizeof(int), fmt::format("{}-0", id));
    }
    // Shouldn't change the cursor, so the #read() reads the first element
    ASSERT_EQ(reader_->Seek("0-0"), 0);
    ASSERT_EQ(reader_->Read(&read, 1), 1);
    ASSERT_EQ(read, 10);
    ASSERT_EQ(reader_->total_samples_read(), 1);

    // Shouldn't change the cursor, again, so read just reads the second element
    ASSERT_EQ(reader_->Seek("0-0"), 0);
    ASSERT_EQ(reader_->Read(&read, 1), 1);
    ASSERT_EQ(read, 11);
    ASSERT_EQ(reader_->total_samples_read(), 2);

    // Seek to the 3rd element (12-0), so the next read should be the 4th (13-0)
    ASSERT_EQ(reader_->Seek("12-0"), 1);
    ASSERT_EQ(reader_->Read(&read, 1), 1);
    ASSERT_EQ(read, 13);
    ASSERT_EQ(reader_->total_samples_read(), 4);

    // Seek to just past the 5th element (14-1), so the next read should be the 6th element (15-0)
    ASSERT_EQ(reader_->Seek("14-1"), 1);
    ASSERT_EQ(reader_->Read(&read, 1), 1);
    ASSERT_EQ(read, 15);

    // Insert a tombstone after the last element (19-0).
    write_tombstone(10, "19-1");
    for (int id = 20; id < 20 + 10; id++) {
        xadd_sample(1, id - 10, reinterpret_cast<char *>(&id), sizeof(int), fmt::format("{}-0", id));
    }

    // Seek to an element after the tombstone
    ASSERT_EQ(reader_->Seek("22-0"), 7);
    ASSERT_EQ(reader_->Read(&read, 1), 1);
    ASSERT_EQ(read, 23);

    ASSERT_STREQ(listener->old_stream_keys.at(1).c_str(), fmt::format("{}-0", stream_name).c_str());
    ASSERT_STREQ(listener->new_stream_keys.at(1).c_str(), fmt::format("{}-1", stream_name).c_str());

    // Seek to a really large key, which should seek to the end. A subsequent read with no new samples should thus
    // timeout and read no elements.
    ASSERT_EQ(reader_->Seek("100-0"), 6);
    int64_t num_read = reader_->Read(&read, 1, nullptr, nullptr, 100);
    ASSERT_EQ(num_read, 0);

    // Insert something to the end, and the cursor should pick that up.
    int id = 30;
    xadd_sample(1, id - 10, reinterpret_cast<char *>(&id), sizeof(int), fmt::format("{}-0", id));
    ASSERT_EQ(reader_->Read(&read, 1), 1);
    ASSERT_EQ(read, 30);

    // Finally, insert an EOF, and try to seek past it.
    write_eof(30, 1, "30-1");
    ASSERT_EQ(reader_->Seek("100-0"), -1);

    // 21 elements inserted in this test
    ASSERT_EQ(reader_->total_samples_read(), 21);
}

TEST_F(StreamReaderTest, TestMetadata) {
    reader_->Initialize(stream_name);
    unordered_map<string, string> expected = unordered_map<string, string>();
    ASSERT_EQ(reader_->Metadata(), expected);
}
