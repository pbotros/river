# River

[![DOI](https://zenodo.org/badge/268426515.svg)](https://zenodo.org/badge/latestdoi/268426515)

A high-throughput, structured streaming framework built atop an industry-standard in-memory database, Redis. Capable of streaming large-volume, high-bandwidth data from one producer to multiple consumers. Supports online _ingestion_ -- persistence of streamed data to disk for offline analysis -- and indefinitely long streams.

Written in C++ with bindings in Python and experimental bindings in MATLAB.

## Premise

Research and Internet-of-Things (IoT) applications often need to pipe data between devices in near-realtime -- a temperature sensor relays data to a microcontroller that controls a thermostat. While a home-grown solution can likely work for simple systems, more complex systems will inevitably require data produced by a single device to be read by multiple sources, often simultaneously -- that temperature sensor might need to also relay its data to a computer for a realtime display. These requirements intensify with the growing data capabilities of our hardware. Crafting a multi-reader system like this from scratch quickly becomes an untenable effort.

Enter streaming frameworks: libraries designed to "produce" data to many "consumers". There are many robust and industry-standard streaming frameworks out there such as [RabbitMQ](https://www.rabbitmq.com/), [Kafka](https://kafka.apache.org/), and [ZeroMQ](https://zeromq.org/); however, they can be cumbersome to install & manage for non-enterprise environments (e.g. Kafka), have limited single-stream throughput (e.g., RabbitMQ's [~50k messages/sec](https://www.cloudamqp.com/blog/2018-01-08-part2-rabbitmq-best-practice-for-high-performance.html) max even with persistence disabled), or require non-trivial application-level code to be usable for multi-reader streaming (e.g., ZeroMQ). These frameworks are tailored towards stricter requirements than is often required for our settings here, where perhaps some trailing data can be dropped and/or delivered twice in failure cases.

**River** was created to meet the needs of streaming in a research or IoT world: stream data from one device to many others, prioritizing minimal setup and high performance over strict guarantees on message delivery and persistence*. River is built on the high-throughput [Redis Streams](https://redis.io/topics/streams-intro), released in Redis 5.0, and then layers a schema and metadata to make it easy to stream.

However, streaming is often only the _first_ part of the story. Researchers and makers often want to see what was streamed after-the-fact - to analyze that data offline. River addresses this unmet need with its "data ingestion": persisting data that was streamed via River to disk. Packaged in a separate binary, the `ingester` is a long-running server process that polls for River streams created in Redis and automatically writes the data in batches to disk using a columnar data storage format, [Apache Parquet](https://parquet.apache.org/). Once a segment of data is persisted and considered sufficiently stale, the `ingester` will delete this data from Redis, thus affording for indefinitely large streams.

_\* River utilizes Redis for all data storage and thus has the same data consistency guarantees as is configured in your Redis server._

## Installation

For the C++ library and the ingester binary, installation via Conda is the preferred way:

```bash
conda install -c conda-forge river-cpp
```
**NOTE: the ingester binary isn't available on Windows due to compilation issues. Use WSL or Linux/Mac for the ingester**.

For the Python bindings, conda is also preferred:

```bash
conda install -c conda-forge river-py
```

For the (experimental) MATLAB bindings, see the MATLAB README for details.

For compiling from source, see further down.

## Documentation
Documentation for the C++ and Python libraries can be found at https://pbotros.github.io/river/docs/index.html.

## Tutorial: C++

Sample code that writes some sample data to a River stream and then reads and then prints that data to stdout can be found in [river_example.cpp](https://github.com/pbotros/river/blob/master/cpp/src/tools/river_example.cpp). The Python tutorial can be followed to understand how River works.


## Tutorial: Python

### Redis
River utilizes Redis as its underlying database. If you don't have it running, the [Redis website](https://redis.io/) has great instructions on downloading and installing. Note that Redis is currently only supported on Linux, Mac, and Windows Subsystem for Linux (WSL).

This tutorial assumes Redis is running at localhost `127.0.0.1` on the standard port 6379, but adjust the following if it's not.

### Writing

First, let's create your first stream via river's `StreamWriter`:

```python
import river
import uuid
import numpy as np

stream_name = str(uuid.uuid4())
print("Creating a stream with name", stream_name)

# Create a River StreamWriter that connects to Redis at localhost with port 6379 (the default)
w = river.StreamWriter(river.RedisConnection("127.0.0.1", 6379))

# River's Python bindings has built-in support for conversion between River's schema objects
# and numpy's dtype. These lines initialize a stream where each sample has a single field,
# a double, named `col1`. See the documentation on StreamSchema for a complete list of supported
# NumPy types.
dt = np.dtype([('col1', np.double)])
w.initialize(stream_name, river.StreamSchema.from_dtype(dt))

# Create a buffer of data to write to the stream. #new_buffer
# is syntactic sugar for creating a numpy array with the schema's
# dtype (e.g. `np.empty((10,) dtype=dt)`).
to_write = w.new_buffer(10)
to_write['col1'] = np.arange(10, dtype=np.double)

# Use a context manager approach so we can't forget to stop the stream:
with w:
  # Write data! Writes an array of doubles to the stream. It is on the user to ensure that the given numpy array
  # passed in is formatted according to the stream schema, else garbage can be written to the stream.
  w.write(to_write)

# Whenever the context ends (i.e. this `with` block terminates), w.stop() is called,
# so if you're not using a context like this, you can call w.stop() manually.
# This serves to "stop" the stream and declares no more samples are to be written.
# This "finalizes" the stream and is a required call to tell any readers (including
# the ingester) where to stop. Otherwise, all readers will continually timeout at the
# end of your stream, waiting for more samples.
```

### Reading

Great! You have your first stream. In the same Python session, let's read it back and print out the contents:

```python
# Create the Reader and then initialize it with the stream we want to read from.
r = river.StreamReader(river.RedisConnection("127.0.0.1", 6379))

# The #initialize() call accepts a timeout in milliseconds for the maximum amount
# of time to wait for the stream to be created, if it is not already.
r.initialize(w.stream_name, 1000)

# Here, we'll read one sample at a time, and print it out. The number of samples
# read per invocation is decided by the size of the buffer passed in, so in this
# case, we create a new empty buffer with `new_buffer` (again, syntactic sugar for
# creating a numpy array with appropriate dtype). In real use cases, you should
# read as many samples per call as your latency/system tolerates, to amoritze overhead
# of each call.
data = r.new_buffer(1)

# Like with the writer, you can use a context manager to auto-stop the reader after
# you're done.
with r:
  while True:
    # Similar to the style of many I/O streams, we pass in a buffer that will be
    # filled with read data when available. In this case, since `data` is of size
    # 1, at most 1 sample will be read from the stream at a time. The second parameter
    # is the timeout in milliseconds: the max amount of time this call will block until
    # the given number of samples is available. In this case, it's 100ms max.
    #
    # The return value returns the number of samples read into the buffer. It should always
    # be checked. -1 is returned once EOF is encountered.
    num_read = r.read(data, 100)
    if num_read > 0:
      print(data['col1'][0])
    elif num_read == 0:
      print('Timeout occurred.')
      continue
    else:
      print('EOF encountered for stream', r.stream_name)
      break

# When the context exists, r.stop() is called, so without a context you can call it manually.
# Calling #stop() frees resources allocated within the StreamReader; this reader cannot be used again.
```

Your output should print out 0.0, 1.0, 2.0, ..., 9.0. Note that, although in this example we wrote the stream and then read back the stream sequentially, both chunks of code can be run simultaneously; the reader will block as requested if there are not enough samples in the stream.

### Ingester

Now let's ingest some data via the `river-ingester` binary:

```bash
GLOG_alsologtostderr=1 river-ingester -h 127.0.0.1 -o river_streams
```

This will begin the `ingester`, which will check Redis for any existing streams. River uses GLOG for logging, and so the environment variable `GLOG_alsologtostderr` prints out any logging information to STDERR.

The logs should include:

```
...
Starting ingestion of stream <your stream name>
...
Stream metadata for <your stream name> deleted.
...
```

After these log lines, you can ctrl-C the ingester. The following files should have been written in the `river_streams` directory:

```bash
$> ls -R river_streams
...
river_streams/<your stream name>:
data.parquet  metadata.json
```

You can then print out the contents of `data.parquet` via Pandas and confirm it's what's expected:

```bash
$> python -c 'import pandas as pd; print(pd.read_parquet("river_streams/<your stream name>/data.parquet"))'
   sample_index              key   timestamp_ms  col1
0             0  1591593828887-0  1591593828887   0.0
1             1  1591593828887-1  1591593828887   1.0
2             2  1591593828887-2  1591593828887   2.0
3             3  1591593828887-3  1591593828887   3.0
4             4  1591593828887-4  1591593828887   4.0
5             5  1591593828887-5  1591593828887   5.0
6             6  1591593828887-6  1591593828887   6.0
7             7  1591593828887-7  1591593828887   7.0
8             8  1591593828887-8  1591593828887   8.0
9             9  1591593828887-9  1591593828887   9.0
```

You can see a couple columns in addition to the data we wrote have been added, namely:

- `sample_index`: 0-indexed index of the sample/row
- `key`: a globally unique identifier for the row (it's actually the Redis key of the sample)
- `timestamp_ms`: the UNIX timestamp in milliseconds of the Redis server

For those interested in interrogating data while a stream is ongoing: the `ingester` writes intermediate files in the form of `data_XXXX.parquet` in the given output directory while the stream is ongoing, where `XXXX` is of the form `0000, 0001, ...` . Each Parquet file represents a disjoint set of data written in ascending `sample_index` .

Finally, let's look at the `metadata.json` and highlight a few key fields:

```
$> cat river_streams/<your stream name>/metadata.json | jq
{
  "stream_name": "57031e25-ad00-49f6-8e42-3b69a4684fa9",
  "local_minus_server_clock_us": "0",
  "initialized_at_us": "1591593828887568",
  "ingestion_status": "COMPLETED"
}
```

- `ingestion_status`: can be `COMPLETED` or `IN_PROGRESS`. Reflects the status of ingesting this particular stream.
- `local_minus_server_clock_us`: estimated difference between the local and server (i.e. Redis) clocks in microseconds.
- `initialized_at_us`: the local UNIX timestamp in microseconds at which `StreamWriter#initialize()` was called.

## Performance

On a 2019 16-inch Macbook Pro with 2.6 GHz i7 and 16GB ram, writing/reading to Redis at localhost, and with no data in Redis before testing, performance varies as a function of sample size and batch size:

![Graph](https://raw.githubusercontent.com/pbotros/river/master/misc/performance.png)

Above performance tests were run with:

```
build/release/src/river_benchmark -h 127.0.0.1 --num_samples 300000 --sample_size <sample size> --batch_size <batch size>
```

The above parameter "batch size" controls how many samples at a time to write to River (i.e., `StreamWriter`'s `num_samples` parameter in `Write`). As can be seen in the above graphs, batching writes drastically improves performance and can be used where appropriate.

## Compiling from Source

You can also compile by source and install manually. The below steps will compile the C++ library and install both the C++ library/headers and the Python bindings. This project uses CMake.

### Prerequisites
River expects several packages to be installed in the standard system-wide directories, including:

- Python 3.7+
- Google Log (glog)

If you're also building and installing the Ingester, you'll need:
- Boost 1.67+
- Apache Arrow and Parquet

Use your favorite package manager to install the above. For instance, run:

```
conda install -c conda-forge pkg-config cmake # build tools
conda install -c conda-forge glog  # Google Log
# brew install python3-dev  # If not already installed via conda
# conda install -c conda-forge boost-cpp arrow-cpp # Boost/Arrow/Parquet, if installing ingester
```

### Installing

Since River uses CMake, you can use standard CMake commands such as (if on Mac or Linux):

```
git clone git@github.com:pbotros/river.git
cd river
mkdir -p build/release
cd build/release
cmake -G "Unix Makefiles" -DRIVER_BUILD_INGESTER=0 -DCMAKE_BUILD_TYPE=Release ../..
make
sudo make install  # if on Mac, can omit sudo
sudo ldconfig  # if on Linux
```

Replace `{r,R}elease` with `{d,D}ebug` in the above to build debug binaries with debugging symbols if needed. If on Windows, you can use the CMake GUI, or replace the "-G" command with the appropriate identifier (e.g. `-G "Visual Studio 15 2017"`).

By default, building the ingester is *NOT* enabled, as a typical system configuration will have many readers and writers distributed across a variety of computers but a single instance of ingestion running on a local computer. 

To enable building the ingester, enable the CMake flag `RIVER_BUILD_INGESTER` as in the following example:

```
cmake -DCMAKE_BUILD_TYPE=Release -DRIVER_BUILD_INGESTER=1 -G "CodeBlocks - Unix Makefiles" ../..
make
sudo make install
```

This will build and install a `river-ingester` binary in your default install path, e.g., `/usr/local/bin/` on modern Mac/Unix systems. Run it with the `--help` option for more details.

### Verifying Installation

To test whether the installation was correct, run the benchmark, assuming you're running Redis on localhost:

```
# From the root of the river repository
cd build/release/src
./river_benchmark --redis_hostname 127.0.0.1  --batch_size 1 --sample_size 128 --num_samples 1000
```



### Troubleshooting

### Installing Google Log (GLOG)
On Mac, `brew install glog` seems to work fine to resolve dependencies needed for Google Log. However, on other distros where the version of GLOG is too old and doesn't include a CMakeLists.txt (i.e. Raspbian Buster, Ubuntu 18.04), GLOG needs to be compiled and installed from source.

```
cd /some/directory
git clone https://github.com/google/glog.git
cd glog
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -G "CodeBlocks - Unix Makefiles" -DBUILD_SHARED_LIBS=ON ..
make
sudo make install
```

If you get an error like `ERROR: flag 'logtostderr' was defined more than once (in files 'src/logging.cc' and '/some/path/to/logging.cc').`, then you might have multiple installations of GLOG / GFlags. To fix this, you can have CMake build GLOG from source instead of relying on your system versions of GLOG. Do this by uninstalling glog:

```
sudo apt remove libgflags-dev libglog-dev
```

### Installing Boost on Linux
In some Linux distributions, the packaged version of Boost might be too old. In order to install Boost from source, follow [the Boost website](https://www.boost.org/doc/libs/1_57_0/more/getting_started/unix-variants.html). In particular, the following commands will install the libraries needed, once you've downloaded the most recent release and un-tar'd it:

```
./bootstrap --with-libraries=filesystem,graph,program_options,system,headers,thread
./b2
sudo ./b2 install
```


### Installing Boost on Windows
Boost can be installed via a precompiled binary posted by the boost team. [Go here](https://sourceforge.net/projects/boost/files/boost-binaries) to find the latest precompiled Boost binaries. You can also install via conda.


## Contributing

### C++ API
TODO

### Regenerating Python Stubs
```bash
python3 /path/to/mypy/mypy/mypy/stubgen.py -p $(python3 -c "import river as _, os; print(os.path.dirname(_.__file__))") -m river && mv out/river.pyi python/```
```
