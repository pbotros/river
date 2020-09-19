#include <chrono>
#include <fstream>
#include <glog/logging.h>
#include <fmt/format.h>
#include <cxxopts.hpp>

#include "../river.h"

using namespace river;
using namespace std;

int main(int argc, char **argv) {
  google::InitGoogleLogging("river");

  cxxopts::Options options
      ("RiverReader", "Reads raw data from a River stream via a StreamReader from STDIN. Reads until STDIN is closed.");
  options.add_options()
      ("help",
       "Reads raw data from a River stream via a StreamWriter and outputs raw binary to STDOUT. Reads until the stream is finished or STDOUT is closed.")
      ("redis_hostname,h", "Redis hostname [required]", cxxopts::value<std::string>())
      ("redis_port,p", "Redis port [optional]", cxxopts::value<int>()->default_value("6379"))
      ("redis_password,w", "Redis password [optional]", cxxopts::value<string>())
      ("redis_password_file,f", "Redis password file [optional]", cxxopts::value<string>())
      ("stream_name", "Stream name to read data from [required]", cxxopts::value<string>())
      ("batch_size", "Number of rows to read at a time [optional]", cxxopts::value<int>()->default_value("1"));

  auto result = options.parse(argc, argv);

  string redis_hostname = result["redis_hostname"].as<string>();
  int redis_port = result["redis_port"].as<int>();
  string redis_password = result["redis_password"].as<string>();
  string redis_password_file = result["redis_password_file"].as<string>();
  string stream_name = result["stream_name"].as<string>();
  int batch_size = result["batch_size"].as<int>();

  if (!redis_password_file.empty() && redis_password.empty()) {
    ifstream infile;
    infile.open(redis_password_file);
    infile >> redis_password;
  }

  river::RedisConnection rc(redis_hostname, redis_port, redis_password);

  StreamReader reader(rc);
  reader.Initialize(stream_name);

  const StreamSchema &schema = reader.schema();
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
