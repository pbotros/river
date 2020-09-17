#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <glog/logging.h>
#include <fmt/format.h>

#include "../river.h"

using namespace river;
using namespace std;
namespace po = boost::program_options;

int main(int argc, char **argv) {
    google::InitGoogleLogging("river");

    string redis_hostname;
    int redis_port;
    string redis_password;
    string redis_password_file;

    int batch_size;
    int sample_size;
    int64_t num_samples;

    po::options_description desc("Allowed options");
    desc.add_options()
            ("help",
             "Reads raw data from a River stream via a StreamWriter and outputs raw binary to STDOUT. Reads until the stream is finished or STDOUT is closed.")
            ("redis_hostname,h", po::value<string>(&redis_hostname)->required(), "Redis hostname [required]")
            ("redis_port,p", po::value<int>(&redis_port)->default_value(6379), "Redis port [optional]")
            ("redis_password,w", po::value<string>(&redis_password), "Redis password [optional]")
            ("redis_password_file,f", po::value<string>(&redis_password_file), "Redis password file [optional]")
            ("num_samples", po::value<int64_t>(&num_samples)->default_value(1000000), "Number of samples to write to redis [default 1 million]")
            ("sample_size", po::value<int>(&sample_size)->default_value(8), "Number of bytes per sample for benchmarking [default 8]")
            ("batch_size", po::value<int>(&batch_size)->default_value(10240), "Number of rows to write at a time for benchmarking [default 1]");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);

    if (vm.count("help")) {
        cerr << desc << endl;
        return 1;
    }
    po::notify(vm);

    if (!redis_password_file.empty() && redis_password.empty()) {
        ifstream infile;
        infile.open(redis_password_file);
        infile >> redis_password;
    }

    river::RedisConnection connection(redis_hostname, redis_port, redis_password);
    StreamReader reader(connection);
    StreamWriter writer(connection);

    boost::uuids::uuid uuid = boost::uuids::random_generator()();
    stringstream ss;
    ss << uuid;
    string stream_name = ss.str();

    StreamSchema schema(vector<FieldDefinition>({
        FieldDefinition("field", FieldDefinition::FIXED_WIDTH_BYTES, sample_size)
    }));
    writer.Initialize(stream_name, schema);

    vector<char> data(batch_size * sample_size);
    for (char i = 0; i < (char) (batch_size * sample_size); i++) {
        data[i] = i;
    }

    auto start_time = chrono::high_resolution_clock::now();
    int64_t num_written = 0;
    while (num_written < num_samples) {
        int64_t remaining = num_samples - num_written;
        auto num_to_write = remaining > batch_size ? batch_size : remaining;
        writer.WriteBytes(&data.front(), num_to_write);
        num_written += num_to_write;
    }
    writer.Stop();

    auto end_time = chrono::high_resolution_clock::now();
    long long int us = chrono::duration_cast<chrono::microseconds>(end_time - start_time).count();
    double throughput = num_samples / (us / 1e6);
    cout << fmt::format(
        "Put {} elements in {:.3f} ms ({:.3f} items/sec, {:.3f} MB/sec) for stream {}",
        num_written, us / 1000.0f, throughput, throughput * sample_size / 1024 / 1024, stream_name)
              << endl;

    reader.Initialize(stream_name);

    start_time = chrono::high_resolution_clock::now();
    int64_t num_read = 0;
    vector<char> read_data(batch_size * sample_size);
    while (reader.Good()) {
        num_read += reader.ReadBytes(&read_data.front(), batch_size);
    }
    end_time = chrono::high_resolution_clock::now();
    us = chrono::duration_cast<chrono::microseconds>(end_time - start_time).count();
    throughput = num_samples / (us / 1e6);
    cout << fmt::format(
        "Finished reading {} elements in {:.3f} ms ({:.3f} items/sec, {:.3f} MB/sec) for stream {}",
        num_read, us / 1000.0f, throughput, throughput * sample_size / 1024 / 1024, stream_name)
              << endl;
}
