#include "gtest/gtest.h"
#include "ingester_threadpool.h"
#include <memory>
#include <unordered_map>

using namespace std;

bool should_raise = false;

class IngesterThreadpoolConcurrencyTester {
public:
    unordered_map<string, int> seen_count;
    boost::mutex mutex_;

    int record_stream_name(const string &stream_name) {
        boost::lock_guard<boost::mutex> lock(mutex_);
        if (seen_count.find(stream_name) != seen_count.end()) {
            seen_count[stream_name]++;
        } else {
            seen_count[stream_name] = 1;
        }
        if (should_raise) {
            throw exception();
        }
        return 0;
    }
};

class IngesterThreadpoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        should_raise = false;
        tester = make_shared<IngesterThreadpoolConcurrencyTester>();
        auto func = boost::bind(&IngesterThreadpoolConcurrencyTester::record_stream_name, tester, _1);
        pool = make_unique<IngesterThreadPool<string, int>>(8, func);
    }

    unique_ptr<IngesterThreadPool<string, int>> pool;
    shared_ptr<IngesterThreadpoolConcurrencyTester> tester;
};

TEST_F(IngesterThreadpoolTest, TestWorks) {
    int elements = 10000;
    for (int i = 0; i < elements; i++) {
        pool->enqueue_stream(fmt::format("stream-{}", i));
    }
    pool->stop();

    // "Enqueue" a bunch of elements after stop() has been called; should be ignored.
    for (int i = 0; i < 10; i++) {
        pool->enqueue_stream(fmt::format("stream-{}", i));
    }
    ASSERT_EQ(tester->seen_count.size(), elements);
    for (int i = 0; i < elements; i++) {
        const string &stream_name = fmt::format("stream-{}", i);

        ASSERT_TRUE(pool->visit_result(
                stream_name,
                +[](const exception &) {
                    FAIL();
                },
                +[](const int &i) {
                    ASSERT_EQ(i, 0);
                }));
        ASSERT_NE(tester->seen_count.find(stream_name), tester->seen_count.end());
        ASSERT_EQ(tester->seen_count[stream_name], 1);
    }
}

TEST_F(IngesterThreadpoolTest, TestExceptionsIgnored) {
    should_raise = true;

    int elements = 100;
    for (int i = 0; i < elements; i++) {
        pool->enqueue_stream(fmt::format("stream-{}", i));
    }
    pool->stop();

    ASSERT_EQ(tester->seen_count.size(), elements);
    for (int i = 0; i < elements; i++) {
        const string &stream_name = fmt::format("stream-{}", i);
        ASSERT_NE(tester->seen_count.find(stream_name), tester->seen_count.end());
        ASSERT_EQ(tester->seen_count[stream_name], 1);
    }
}
