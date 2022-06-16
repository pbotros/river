========
Tutorial
========

Let's do your first stream with River. The following tutorial is written in Python, but the concepts and method names are very similar in C++. Similar example code in C++ can be found in `river_example.cpp <https://github.com/pbotros/river/blob/master/cpp/src/tools/river_example.cpp>`_.

Redis
-----

River utilizes Redis as its underlying database. If you don't have it running, the [Redis website](https://redis.io/) has great instructions on downloading and installing. Note that Redis is currently only supported on Linux, Mac, and Windows Subsystem for Linux (WSL).

This tutorial assumes Redis is running at localhost `127.0.0.1` on the standard port 6379, but adjust the following if it's not.

Writing
-------

First, let's create your first stream via river's `StreamWriter`:


.. code-block:: python

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

Reading
-------

Great! You have your first stream. In the same Python session, let's read it back and print out the contents:

.. code-block:: python

  # Create the Reader and then initialize it with the stream we want to read from.
  r = river.StreamReader(river.RedisConnection("127.0.0.1", 6379))

  # The #initialize() call accepts a timeout in milliseconds for the maximum amount
  # of time to wait for the stream to be created, if it is not already.
  r.initialize(w.stream_name, 1000)

  # Here, we'll read one sample at a time, and print it out. The number of samples
  # read per invocation is decided by the size of the buffer passed in, so in this
  # case, we create a new empty buffer with `new_buffer` (again, syntactic sugar for
  # creating a numpy array with appropriate dtype). In real use cases, you should
  # read as many samples per call as your latency/system tolerates to amoritze overhead
  # of each call, i.e. you should use a buffer with many more samples.
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


Your output should print out 0.0, 1.0, 2.0, ..., 9.0. Note that, although in this example we wrote the stream and then read back the stream sequentially, both chunks of code can be run simultaneously; the reader will block as requested if there are not enough samples in the stream.

Ingestion
---------

Now let's ingest some data via the `river-ingester` binary:

.. code-block:: bash

  GLOG_alsologtostderr=1 river-ingester -h 127.0.0.1 -o river_streams

This will begin the `ingester`, which will check Redis for any existing streams. River uses GLOG for logging, and so the environment variable `GLOG_alsologtostderr` prints out any logging information to STDERR.

The logs should include:

.. code-block:: bash

  ...
  Starting ingestion of stream <your stream name>
  ...
  Stream metadata for <your stream name> deleted.
  ...

After these log lines, you can ctrl-C the ingester. The following files should have been written in the `river_streams` directory:

.. code-block:: bash

  $> ls -R river_streams
  ...
  river_streams/<your stream name>:
  data.parquet  metadata.json

You can then print out the contents of `data.parquet` via Pandas and confirm it's what's expected:

.. code-block:: bash

  python -c 'import pandas as pd; print(pd.read_parquet("river_streams/<your stream name>/data.parquet"))'
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

You can see a couple columns in addition to the data we wrote have been added, namely:

- `sample_index`: 0-indexed index of the sample/row
- `key`: a globally unique identifier for the row (it's actually the Redis key of the sample)
- `timestamp_ms`: the UNIX timestamp in milliseconds of the Redis server

For those interested in interrogating data while a stream is ongoing: the `ingester` writes intermediate files in the form of `data_XXXX.parquet` in the given output directory while the stream is ongoing, where `XXXX` is of the form `0000, 0001, ...` . Each Parquet file represents a disjoint set of data written in ascending `sample_index` .

Finally, let's look at the `metadata.json` and highlight a few key fields:

.. code-block:: bash

  $> cat river_streams/<your stream name>/metadata.json | jq
  {
    "stream_name": "57031e25-ad00-49f6-8e42-3b69a4684fa9",
    "local_minus_server_clock_us": "0",
    "initialized_at_us": "1591593828887568",
    "ingestion_status": "COMPLETED"
  }

- `ingestion_status`: can be `COMPLETED` or `IN_PROGRESS`. Reflects the status of ingesting this particular stream.
- `local_minus_server_clock_us`: estimated difference between the local and server (i.e. Redis) clocks in microseconds.
- `initialized_at_us`: the local UNIX timestamp in microseconds at which `StreamWriter#initialize()` was called.
