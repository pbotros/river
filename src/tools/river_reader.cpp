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
            ("help", "Reads raw data from a River stream via a StreamWriter and outputs raw binary to STDOUT. Reads until the stream is finished or STDOUT is closed.")
            ("redis_hostname,h", po::value<string>(&redis_hostname)->required(), "Redis hostname [required]")
            ("redis_port,p", po::value<int>(&redis_port)->default_value(6379), "Redis port [optional]")
            ("redis_password,w", po::value<string>(&redis_password), "Redis password [optional]")
            ("redis_password_file,f", po::value<string>(&redis_password_file), "Redis password file [optional]")
            ("stream_name", po::value<string>(&stream_name)->required(), "Stream name to read data from [required]")
            ("batch_size", po::value<int>(&batch_size)->default_value(1), "Number of rows to read at a time [optional]");

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

    StreamReader reader(rc);
    reader.Initialize(stream_name);

    const StreamSchema& schema = reader.schema();
    vector<char> buffer(batch_size * schema.sample_size());

    cout << "Beginning reading for stream " << stream_name << endl;
    auto start_time = chrono::high_resolution_clock::now();
    size_t bytes_per_row = schema.sample_size();
    while (reader && std::cout) {
        int64_t num_read = reader.ReadBytes(&buffer.front(), batch_size);
        std::cout.write(&buffer.front(), num_read * bytes_per_row);
    }
    reader.Stop();
    auto end_time = chrono::high_resolution_clock::now();

    auto us = chrono::duration_cast<chrono::microseconds>(end_time - start_time).count();
    auto num_elements = reader.total_samples_read();
    auto throughput = num_elements / (us / 1e6);

    cout << fmt::format(
            "Finished reading {} elements in {:.3f} ms ({:.3f} items/sec, {:.3f} MB/sec) for stream {}",
            num_elements, us / 1000.0f, throughput, throughput * bytes_per_row / 1024 / 1024, stream_name)
              << endl;
}
