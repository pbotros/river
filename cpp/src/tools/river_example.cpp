#include <random>
#include <sstream>
#include <glog/logging.h>
#include "../river.h"

int main(int, char **) {
    // River uses GLOG for logging.
    google::InitGoogleLogging("river");

    // Generate a random stream name
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::stringstream ss;
    ss << "example-" << dis(gen) << dis(gen) << dis(gen) << dis(gen);
    std::string stream_name = ss.str();

    // Generate some test data
    double data[10];
    for (int i = 0; i < 10; i++) {
        data[i] = i;
    }

    std::cout << "Creating River stream with stream name " << stream_name << std::endl;

    // Create a River StreamWriter that connects to Redis at localhost with port 6379 (the default)
    river::RedisConnection connection("127.0.0.1", 6379);
    river::StreamWriter writer(connection);

    // Define the schema of the stream. This stream will have a single field in each sample with field name "column1".
    // This field will be a double of 8 bytes.
    river::StreamSchema schema(std::vector<river::FieldDefinition>(
        {
            river::FieldDefinition("column1", river::FieldDefinition::DOUBLE, 8)
        }));

    // Initializes the StreamWriter with the above schema and stream name. This "claims" the given stream name in Redis;
    // after this, we can write to the stream.
    writer.Initialize(stream_name, schema);

    // Write data! Writes an array of doubles to the stream. It is on the user to ensure that the given pointer passed
    // in is formatted according to the stream schema, else garbage can be written to the stream.
    // This is also equivalent to: writer.WriteBytes(reinterpret_cast<char *>(data), 10);
    writer.Write(data, 10);

    // Stops the stream, declaring no more samples are to be written. This "finalizes" the stream and is a required call
    // to tell any readers (including the ingester) where to stop.
    writer.Stop();

    // We're done with writing now; let's create the Reader and then initialize it with the stream we want to read from.
    river::StreamReader reader(connection);
    reader.Initialize(stream_name);

    // For the reader, we'll read one sample at a time as an example.
    double datum;

    while (reader) {
        // Similar to a C++ I/O stream, this reads data into the given pointer up to the given number of samples. Note
        // that the actual number of samples read can be less than the given number of samples, so checking the return
        // value is required.
        int num_read = reader.Read(&datum, 1);
        if (num_read > 0) {
            std::cout << datum << std::endl;
        }
    }

    // Cleans up any Redis connections within the reader.
    reader.Stop();
}
