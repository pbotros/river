import os
import uuid
import tempfile
import time
from typing import Optional

import numpy as np
from multiprocessing import Process

import gc
import pandas as pd
import river


def read_and_record(redis_hostname, redis_password, reader_id, stream_name, batch_size):
    reader = river.StreamReader(river.RedisConnection(redis_hostname, 6379, redis_password))
    reader.initialize(stream_name)
    b = reader.new_buffer(batch_size)
    samples_received_at = []
    try:
        gc.collect()
        gc.disable()
        with reader:
            while True:
                num_read = reader.read(b)
                if num_read < 0:
                    break
                elif num_read == 0:
                    continue
                samples_received_at.append(time.perf_counter())
    finally:
        gc.enable()

    df = pd.DataFrame({'sample_index': np.arange(len(samples_received_at)), 'sample_received_at': samples_received_at})

    fn = os.path.join(tempfile.gettempdir(), f'{stream_name}_reader_{reader_id}.parquet')
    df.to_parquet(fn)


def test_performance(
        redis_hostname: str,
        redis_port: int,
        redis_password: Optional[str],
        dt: float,
        batch_size: int,
        n_samples: int,
        sample_size_bytes: int,
        num_readers: int,
        read_simultaneously: bool = True,
):
    dtype = np.dtype([('data', f'V{sample_size_bytes}')])
    schema = river.StreamSchema.from_dtype(dtype)

    buffer = np.empty((n_samples,), dtype=dtype)
    for i in range(n_samples):
        x = i % 64
        y = i // 64
        b = bytes(chr(x).encode('ascii'))
        zb = bytes(chr(0).encode('ascii'))
        s = bytes.ljust(bytes.rjust(b, 8 - y, zb), y, zb)
        buffer['data'][i] = s

    writer = river.StreamWriter(river.RedisConnection(redis_hostname, 6379, redis_password))
    stream_name = str(uuid.uuid4())
    writer.initialize(stream_name, schema)

    processes = []
    for i in range(num_readers):
        p = Process(target=read_and_record, args=(redis_hostname, redis_password, i, stream_name, batch_size))
        if read_simultaneously:
            p.start()
        processes.append(p)

    n_batches = int(np.ceil(n_samples / batch_size))
    batches = np.array_split(np.arange(n_samples), n_batches)
    print(f'Writing with batch size {batch_size}, # batches {n_batches} [total {n_samples}]')
    to_write = [buffer[batch] for batch in batches]

    n_primed = min(n_batches, 100)

    # PRIME everything:
    start_time = time.perf_counter()
    for i in range(min(n_batches, 100)):
        writer.write(to_write[i])
        end_time = time.perf_counter()
        to_sleep = start_time + (i + 1) * dt - end_time
        if to_sleep > 0:
            time.sleep(to_sleep)

    print('Sleeping, waiting for everything to catch up...')
    time.sleep(1.0)
    print('Executing!')

    times = []
    try:
        gc.collect()
        gc.disable()
        start_time = time.perf_counter()
        for i in range(n_batches):
            sample_ready_at = time.perf_counter()
            writer.write(to_write[i])
            times.append(sample_ready_at)
            to_sleep = start_time + (i + 1) * dt - sample_ready_at
            if to_sleep > 0:
                time.sleep(to_sleep)
        writer.stop()
    finally:
        gc.enable()
        gc.collect()

    if not read_simultaneously:
        time.sleep(1.0)
        for p in processes:
            p.start()
    for p in processes:
        p.join()

    # Delete it manually
    import redis
    r = redis.StrictRedis(host=redis_hostname, port=redis_port, password=redis_password)
    print(r.delete(f'{stream_name}-0'))
    print(r.delete(f'{stream_name}-metadata'))

    reader_dfs = []
    for i in range(num_readers):
        reader_df = pd.read_parquet(f'/tmp/{stream_name}_reader_{i}.parquet').assign(reader_id=i)
        reader_dfs.append(reader_df)

    # Across all readers, take the latest received sample time for each sample
    reader_df = (
        pd.concat(reader_dfs)
            .groupby('sample_index')['sample_received_at'].max()
            .reset_index())

    writer_df = pd.DataFrame({'sample_written_at': times, 'sample_index': np.arange(len(times)) + n_primed})

    df = (
        writer_df
            .merge(reader_df, on='sample_index')
            .assign(latency=lambda x: x['sample_received_at'] - x['sample_written_at'])
            .assign(latency_ms=lambda x: x['latency'] * 1e3)
            .assign(dt=dt)
            .assign(batch_size=batch_size)
            .assign(n_samples=n_samples)
            .assign(sample_size_bytes=sample_size_bytes)
            .assign(num_readers=num_readers)
    )
    return df
