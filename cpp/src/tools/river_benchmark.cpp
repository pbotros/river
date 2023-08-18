#include <chrono>
#include <glog/logging.h>
#include <fmt/format.h>
#include <fstream>
#include <cxxopts.hpp>
#include "uuid.h"
#include "../river.h"
#include <nlohmann/json.hpp>

using namespace river;
using json = nlohmann::json;
using namespace std;

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
       ;

  auto result = options.parse(argc, argv);

    string redis_hostname = result["redis_hostname"].as<string>();
    int redis_port = result["redis_port"].as<int>();
    string redis_password = result["redis_password"].as<string>();
    string redis_password_file = result["redis_password_file"].as<string>();
    int batch_size = result["batch_size"].as<int>();
    int sample_size = result["sample_size"].as<int>();
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
  int64_t data_index = 0;
  while (num_written < num_samples) {
    int64_t remaining = num_samples - num_written;
    auto num_to_write = remaining > batch_size ? batch_size : remaining;
    writer.WriteBytes(&data.front() + data_index, num_to_write);
    data_index += num_to_write * sample_size;
    num_written += num_to_write;
  }
  writer.Stop();

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
