#include <chrono>
#include <glog/logging.h>
#include <fmt/format.h>
#include <fstream>
#include <cxxopts.hpp>
#include "uuid.h"
#include "../river.h"
#include <nlohmann/json.hpp>
#include <condition_variable>
#include <mutex>
#include <tracy/Tracy.hpp>

using namespace river;
using json = nlohmann::json;
using namespace std;

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

int main(int argc, char **argv) {
    google::InitGoogleLogging("river");

    cxxopts::Options options("RiverBenchmark", "Benchmarks river readers and writers.");
    options.add_options()
        ("help",
         "Reads raw data from a River stream via a StreamWriter and outputs raw binary to STDOUT. Reads until the stream is finished or STDOUT is closed.")
        ("h,redis_hostname", "Redis hostname [required]", cxxopts::value<std::string>())
        ("p,redis_port", "Redis port [optional]", cxxopts::value<int>()->default_value("6379"))
        ("w,redis_password", "Redis password [optional]", cxxopts::value<string>()->default_value(""))
        ("f,redis_password_file", "Redis password file [optional]", cxxopts::value<string>()->default_value(""))
        ("num_samples",
         "Number of samples to write to redis [default 1 million]",
         cxxopts::value<int64_t>()->default_value("1000000"))
        ("sample_size",
         "Number of bytes per sample for benchmarking [default 8]",
         cxxopts::value<int>()->default_value("8"))
        ("batch_size",
         "Number of rows to write at a time for benchmarking [default 10240]",
         cxxopts::value<int>()->default_value("10240"))
        ("compression_type",
         "Name of compression type",
         cxxopts::value<std::string>()->default_value("UNCOMPRESSED"))
        ("compression_params",
         "Json-serialized string for parameters",
         cxxopts::value<std::string>()->default_value("{}"))
        ("input_file",
         "Path to an input file to load data; must be of size num_samples * sample_size",
         cxxopts::value<std::string>()->default_value(""))
        ("pool_size",
         "Size of writing pool, i.e. how many threads to simultaneously send data,",
         cxxopts::value<int>()->default_value("1"))
         ;

    auto result = options.parse(argc, argv);

    string redis_hostname = result["redis_hostname"].as<string>();
    int redis_port = result["redis_port"].as<int>();
    string redis_password = result["redis_password"].as<string>();
    string redis_password_file = result["redis_password_file"].as<string>();
    int batch_size = result["batch_size"].as<int>();
    int sample_size = result["sample_size"].as<int>();
    int pool_size = result["pool_size"].as<int>();
    int64_t num_samples = result["num_samples"].as<int64_t>();

    string compression_type = result["compression_type"].as<string>();
    string compression_params_json = result["compression_params"].as<string>();
    std::unordered_map<std::string, std::string> compression_params = json::parse(compression_params_json);

    string input_file = result["input_file"].as<string>();

    if (!redis_password_file.empty() && redis_password.empty()) {
        std::ifstream infile;
        infile.open(redis_password_file);
        infile >> redis_password;
    }

    river::RedisConnection connection(redis_hostname, redis_port, redis_password);
    StreamReader reader(connection);
    StreamWriter writer(StreamWriterParamsBuilder()
                            .connection(connection)
                            .compression(StreamCompression::Create(compression_type, compression_params))
                            .pool_size(pool_size)
                            .build());

    string stream_name = uuid::generate_uuid_v4();

    StreamSchema schema(vector<FieldDefinition>({
                                                    FieldDefinition("field",
                                                                    FieldDefinition::FIXED_WIDTH_BYTES,
                                                                    sample_size)
                                                }));
    writer.Initialize(stream_name, schema);

    std::vector<char> data;
    if (!input_file.empty()) {
        std::ifstream in(input_file, std::ios::binary);
        data = {std::istreambuf_iterator<char>(in), {}};
        if ((int64_t) data.size() != num_samples * sample_size) {
            throw std::invalid_argument("Passed in an input_file with wrong number of bytes?");
        }
    } else {
        data.resize(num_samples * sample_size);
        for (int64_t i = 0; i < num_samples * sample_size; i++) {
            data[i] = (char) i;
        }
    }

    auto start_time = chrono::steady_clock::now();
    int64_t num_written = 0;
    if (pool_size == 1) {
        int64_t data_index = 0;
        while (num_written < num_samples) {
            int64_t remaining = num_samples - num_written;
            auto num_to_write = remaining > batch_size ? batch_size : remaining;
            writer.WriteBytes(&data.front() + data_index, num_to_write);
            data_index += num_to_write * sample_size;
            num_written += num_to_write;
        }
        writer.Stop();
    } else {
        ZoneScopedN("Writing multithread");
        std::mutex preparation_lock;

        // Tracks the number of threads currently sending any data
        Semaphore sending_semaphore;
        for (int i = 0; i < pool_size; ++i) {
            sending_semaphore.release();
        }

        std::atomic_int64_t num_written_ptr = 0;

        std::function<void()> send_data([
                                            &preparation_lock,
                                            num_samples,
                                            &num_written_ptr,
                                            &data,
                                            pool_size,
                                            batch_size,
                                            sample_size,
                                            &sending_semaphore,
                                            &writer]() mutable {
            ZoneScopedN("InThreadWrites");
            while (true) {
                std::optional<PreparedData> prepared_data;
                {
                    ZoneScopedN("InThreadPreparation");
                    std::scoped_lock<std::mutex> guard(preparation_lock);

                    int64_t remaining = num_samples - num_written_ptr.load();
                    auto num_to_write = remaining > batch_size ? batch_size : remaining;
                    if (num_to_write == 0) {
                        break;
                    }
                    auto data_offset = (sample_size * num_written_ptr.load());
                    prepared_data = writer.PrepareBytes(&data.front() + data_offset, num_to_write);
                    if (!prepared_data) {
                        ZoneScopedN("InThreadStreamSwitch");
                        // Failed to prepare; must mean we need to halt all sends...
                        // Acquire all of them, thus guaranteeing nobody is sending right now
                        for (int i = 0; i < pool_size; ++i) {
                            sending_semaphore.acquire();
                        }
                        writer.WriteBytes(
                            &data.front() + data_offset,
                            num_to_write);

                        // Restore the pool to how it was now
                        for (int i = 0; i < pool_size; ++i) {
                            sending_semaphore.release();
                        }
                    }
                    num_written_ptr.fetch_add(num_to_write);
                }

                if (!prepared_data) {
                    // Already handled this above; nothing else to send.
                    continue;
                }

                ZoneScopedN("InThreadSending");
                sending_semaphore.acquire();
                auto num_sent = writer.SendPrepared(prepared_data.value());
//                num_written_ptr.fetch_add(num_sent);
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

        writer.Stop();

        num_written = num_written_ptr.load();
    }

    auto end_time = chrono::steady_clock::now();
    long long int us = chrono::duration_cast<chrono::microseconds>(end_time - start_time).count();
    double throughput = num_samples / (us / 1e6);
    cout << fmt::format(
        "Put {} elements in {:.3f} ms ({:.3f} items/sec, {:.3f} MB/sec) for stream {}",
        num_written, us / 1000.0f, throughput, throughput * sample_size / 1024 / 1024, stream_name)
         << endl;

    reader.Initialize(stream_name);

    start_time = chrono::steady_clock::now();
    int64_t num_read = 0;
    vector<char> read_data(batch_size * sample_size);
    while (true) {
        auto num_read_loop = reader.ReadBytes(&read_data.front(), batch_size);
        if (num_read_loop < 0) {
            break;
        }
        num_read += num_read_loop;
    }
    end_time = chrono::steady_clock::now();
    us = chrono::duration_cast<chrono::microseconds>(end_time - start_time).count();
    throughput = num_samples / (us / 1e6);
    cout << fmt::format(
        "Finished reading {} elements in {:.3f} ms ({:.3f} items/sec, {:.3f} MB/sec) for stream {}",
        num_read, us / 1000.0f, throughput, throughput * sample_size / 1024 / 1024, stream_name)
         << endl;
}
