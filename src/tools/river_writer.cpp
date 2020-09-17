#include <chrono>
#include <fstream>
#include <glog/logging.h>
#include <boost/program_options.hpp>
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
    string stream_name;
    string schema_json;
    int batch_size;

    po::options_description desc("Allowed options");
    desc.add_options()
            ("help", "Write raw data from STDIN via a River StreamWriter according to the given schema. Reads until STDIN is closed.")
            ("redis_hostname,h", po::value<string>(&redis_hostname)->required(), "Redis hostname [required]")
            ("redis_port,p", po::value<int>(&redis_port)->default_value(6379), "Redis port [optional]")
            ("redis_password,w", po::value<string>(&redis_password), "Redis password [optional]")
            ("redis_password_file,f", po::value<string>(&redis_password_file), "Redis password file [optional]")
            ("stream_name", po::value<string>(&stream_name)->required(), "Stream name to put data to [required]")
            ("batch_size", po::value<int>(&batch_size)->default_value(1), "Number of rows to write at a time [optional]")
            ("schema", po::value<string>(&schema_json)->required(), "Schema in JSON format");

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

    river::RedisConnection rc(redis_hostname, redis_port, redis_password);

    StreamSchema schema = StreamSchema::FromJson(schema_json);
    StreamWriter writer(rc);
    writer.Initialize(stream_name, schema);

    vector<char> buffer(batch_size * schema.sample_size());
    int num_buffered_rows = 0;

    cout << "Beginning writing for stream " << stream_name << endl;
    auto start_time = chrono::high_resolution_clock::now();
    size_t bytes_per_row = schema.sample_size();
    while (std::cin.read(reinterpret_cast<char *>(&buffer.front() + num_buffered_rows * bytes_per_row), bytes_per_row)) {
        num_buffered_rows++;
        if (num_buffered_rows == batch_size) {
            writer.WriteBytes(&buffer.front(), batch_size);
            num_buffered_rows = 0;
        }
    }
    if (num_buffered_rows > 0) {
        writer.WriteBytes(&buffer.front(), num_buffered_rows);
    }
    writer.Stop();
    auto end_time = chrono::high_resolution_clock::now();

    auto us = chrono::duration_cast<chrono::microseconds>(end_time - start_time).count();
    auto num_elements = writer.total_samples_written();
    auto throughput = num_elements / (us / 1e6);

    cout << fmt::format(
            "Finished writing {} elements in {:.3f} ms ({:.3f} items/sec, {:.3f} MB/sec) for stream {}",
            num_elements, us / 1000.0f, throughput, throughput * bytes_per_row / 1024 / 1024, stream_name)
              << endl;
}
