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
      ("RiverWriter",
       "Reads raw data from a River stream via a StreamWriter and outputs raw binary to STDOUT. Reads until the stream is finished or STDOUT is closed.");
  options.add_options()
      ("help",
       "Reads raw data from a River stream via a StreamWriter and outputs raw binary to STDOUT. Reads until the stream is finished or STDOUT is closed.")
      ("h,redis_hostname", "Redis hostname [required]", cxxopts::value<std::string>())
      ("p,redis_port", "Redis port [optional]", cxxopts::value<int>()->default_value("6379"))
      ("w,redis_password", "Redis password [optional]", cxxopts::value<string>()->default_value(""))
      ("f,redis_password_file", "Redis password file [optional]", cxxopts::value<string>()->default_value(""))
      ("stream_name", "Stream name to read data from [required]", cxxopts::value<string>())
      ("batch_size", "Number of rows to read at a time [optional]", cxxopts::value<int>()->default_value("1"))
      ("schema", "Schema in JSON format", cxxopts::value<string>());

  auto result = options.parse(argc, argv);

  string redis_hostname = result["redis_hostname"].as<string>();
  int redis_port = result["redis_port"].as<int>();
  string redis_password = result["redis_password"].as<string>();
  string redis_password_file = result["redis_password_file"].as<string>();
  string stream_name = result["stream_name"].as<string>();
  int batch_size = result["batch_size"].as<int>();
  string schema_json = result["schema"].as<string>();

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
  auto start_time = chrono::steady_clock::now();
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
  auto end_time = chrono::steady_clock::now();

  auto us = chrono::duration_cast<chrono::microseconds>(end_time - start_time).count();
  auto num_elements = writer.total_samples_written();
  auto throughput = num_elements / (us / 1e6);

  cout << fmt::format(
      "Finished writing {} elements in {:.3f} ms ({:.3f} items/sec, {:.3f} MB/sec) for stream {}",
      num_elements, us / 1000.0f, throughput, throughput * bytes_per_row / 1024 / 1024, stream_name)
       << endl;
}
