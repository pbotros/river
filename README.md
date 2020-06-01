# River

A high-throughput, structured streaming framework built atop Redis Streams. Designed to stream megabytes/sec of data from one writer to multiple readers, as found in IoT and research applications. Supports "ingestion" of streams via a separate long-running binary that persists existing (i.e., even ongoing) streams to disk via the Apache Parquet format.

Written in C++ with bindings in Python.

## Installation

Compilation by source is currently the only way to install. The below steps will compile the C++ library and install both the C++ library/headers and the Python bindings. This project uses CMake.

### Prerequisites
River expects several packages to be installed in the standard system-wide directories, including:

- Python 3.7
- Boost 1.67+
- Google Log (glog)
- Apache Arrow and Parquet (only if building/installing the Ingester)

Use your favorite package manager to install the above. For instance, on Mac OSX, run:

```
brew update
brew install pkg-config cmake # build tools
brew install python3-dev   # Python 3.7 at the time of writing
brew install boost         # Boost
brew install glog  # Google Log
```

If building & installing the ingester, also do:

```
brew install apache-arrow  # Arrow (and Parquet)
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

### Verifying Installation

To test whether the installation was correct, run the benchmark, assuming you're running Redis on localhost:

```
# From the root of the river repository
cd build/release/src
./river_benchmark --redis_hostname 127.0.0.1  --batch_size 1 --row_size 128 --num_samples 1000
```

## Tutorial: C++

Sample code that writes some sample data to a River stream and then reads and then prints that data to stdout can be found in [river_example.cpp](https://github.com/pbotros/river/blob/master/src/tools/river_example.cpp).


## Tutorial: Python
An example python script that writes random data to a River stream and reads it back:

```python
import river
import uuid
import numpy as np

# Create a River StreamWriter that connects to Redis at localhost with port 6379 (the default)
w = river.StreamWriter(river.RedisConnection("127.0.0.1", 6379))

# River's Python bindings has built-in support for conversion between River's schema objects
# and numpy's dtype. These lines initialize a stream where each sample has a single field,
# a double.
dt = np.dtype([('col1', np.double])
w.initialize(str(uuid.uuid4()), river.StreamSchema.from_dtype(dt))

# Write data! Writes an array of doubles to the stream. It is on the user to ensure that the given numpy array
# passed # in is formatted according to the stream schema, else garbage can be written to the stream.
w.write(np.random.random((10,)), dtype=np.double))

# Stops the stream, declaring no more samples are to be written. This "finalizes" the stream and is a required call
# to tell any readers (including the ingester) where to stop.
w.stop()

# We're done with writing now; let's create the Reader and then initialize it with the stream we want to read from.
r = river.StreamReader(river.RedisConnection("127.0.0.1", 6379))
r.initialize(w.stream_name)

# Here, we'll read one sample at a time, and print it out:
data = np.empty((1,), dtype=np.double)

# Similar to I/O streams, casting the StreamReader as a bool will tell you whether the stream
# is "good" for reading, i.e. if the stream is open and not ended yet. We read from it, and
# check the return value of read() for the number of elements actually read. Note that this
# return value will always be less than or equal to the size of the numpy array passed-in.
while r:
  if r.read(data) > 0:
    print(data[0])
```

## Ingester

River comes with an "Ingester" binary that streams data to disk and then subsequently deletes any persisted data from the in-memory cache in Redis once it is considered sufficiently "stale". This allows streams to continue without being constrained by memory of the Redis server.

By default, building the ingester is *NOT* enabled, as a typical system configuration will have many readers and writers distributed across a variety of computers but a single instance of ingestion running on a local computer.

To enable building the ingester, enable the CMake flag `RIVER_BUILD_INGESTER` as in the following example:

```
cmake -DCMAKE_BUILD_TYPE=Release -DRIVER_BUILD_INGESTER=ON -G "CodeBlocks - Unix Makefiles" ../..
make
sudo make install
```

This will install the binary, `river-ingester`, in your default installation path (e.g. /usr/local/bin by default on Mac/Unix systems). `river-ingester` takes a number of parameters that control its behavior that are documented in its `--help` option.

The Ingester is an almost-vanilla reader of the stream, reading alongside any other River StreamReaders. It reads some number of chunks of data and writes them to disk in these chunks to an intermediate parquet file.  Intermediate parquet files are written in the ingestion directory as the stream is still going, with filenames in the pattern `data_0001.parquet`, `data_0002.parquet`, etc. Once the stream is terminated (i.e. whenever a StreamWriter#stop() is called for the stream), the ingester will combine each intermediate file into a single combined file, `data.parquet`, and delete the intermediate files. Note that the intermediate files are themselves wholly contained parquet files and can be read individually if desired.


## Performance

On a 2019 16-inch Macbook Pro with 2.6 GHz i7 and 16GB ram, writing/reading to Redis at localhost, and with no data in Redis before testing, performance varies as a function of sample size:


| Sample Size (bytes) | Samples/sec, Writing (Hz) | Throughput, Writing (MB/s) | Samples/sec, Reading (Hz) | Throughput, Reading (MB/s)
| --- | --- | --- | --- | --- |
| 8 | 32864.150 | 0.251 | 26647.573 | 0.203 |
| 32 | 31458.211 | 0.960 | 24169.149 | 0.738 |
| 128 | 31262.547 | 3.816 | 25718.370 | 3.139 |
| 512 | 30897.317 | 15.087 | 27827.574 | 13.588 |
| 2048 | 30412.374 | 59.399 | 27261.411 | 53.245 |
| 8192 | 26668.753 | 208.350 | 25193.926 | 196.828 |
| 32768 | 12981.937 | 405.686 | 13893.308 | 434.166 |


Above performance tests were run with:

```
build/release/src/river_benchmark -h 127.0.0.1 --num_samples 300000 --sample_size <sample size> --batch_size 1
```


For an example application where performance is more than enough: River was developed in an electrical engineering / neuroscience lab in order to power a soft-realtime, multi-device system that takes as input multi-channel neural data and outputs data to a Raspberry Pi, a computing machine, and to a graphing computer for experimenter monitoring. River's Ingester then makes data available immediately after experimentation for post-hoc analysis.


## Troubleshooting

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

## Development
### C++ API
See writer.h and reader.h for the main public APIs. Documentation and Python stub code to-be-done in the future.

