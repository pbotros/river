# distutils: language = c++
from libcpp.memory cimport unique_ptr, shared_ptr
cimport cython
cimport criver
from libcpp.string cimport string
from cython.operator cimport dereference as deref
from libcpp.vector cimport vector
from libcpp.unordered_map cimport unordered_map
from libcpp cimport bool
from libc.stdint cimport int64_t
from cpython.ref cimport PyObject
import numpy as np
cimport numpy as np

class StreamExistsException(RuntimeError):
    pass

cdef public PyObject* stream_exists_exception = <PyObject*>StreamExistsException;


class StreamDoesNotExistException(RuntimeError):
    pass
cdef public PyObject* stream_does_not_exist_exception = <PyObject*>StreamDoesNotExistException;


class StreamReaderException(RuntimeError):
    pass
cdef public PyObject* stream_reader_exception = <PyObject*>StreamReaderException;

class StreamWriterException(RuntimeError):
    pass
cdef public PyObject* stream_writer_exception = <PyObject*>StreamWriterException;

class RedisException(RuntimeError):
    pass

cdef public PyObject* redis_exception = <PyObject*>RedisException;


cdef class RedisConnection:
    cdef shared_ptr[criver.RedisConnection] _connection

    def __cinit__(self, str redis_hostname, int redis_port, str redis_password = None):
        if redis_password is None:
            self._connection = shared_ptr[criver.RedisConnection](new criver.RedisConnection(
                redis_hostname.encode('UTF-8'), redis_port))
        else:
            self._connection = shared_ptr[criver.RedisConnection](new criver.RedisConnection(
                redis_hostname.encode('UTF-8'), redis_port, redis_password.encode('UTF-8')))

    @property
    def redis_hostname(self) -> str:
        return deref(self._connection).redis_hostname_

    @property
    def redis_port(self) -> int:
        return deref(self._connection).redis_port_

    @property
    def redis_password(self) -> str:
        return deref(self._connection).redis_password_

cpdef enum FieldType:
    DOUBLE = criver.FIELD_DEFINITION_DOUBLE
    FLOAT = criver.FIELD_DEFINITION_FLOAT
    INT16 = criver.FIELD_DEFINITION_INT16
    INT32 = criver.FIELD_DEFINITION_INT32
    INT64 = criver.FIELD_DEFINITION_INT64
    FIXED_WIDTH_BYTES = criver.FIELD_DEFINITION_FIXED_WIDTH_BYTES
    VARIABLE_WIDTH_BYTES = criver.FIELD_DEFINITION_VARIABLE_WIDTH_BYTES

cdef class FieldDefinition:
    """
    One or more fields that are present in each sample of a particular stream. This definition governs how this will be
    serialized to Redis and the columns in the persisted file. A collection of field definitions are housed within a
    StreamSchema.

    While most field definitions are fixed-width( e.g. doubles, floats, etc.), the VARIABLE_WIDTH_BYTES field is a bit
    different. If you want to use a variable-width bytes (e.g. a dynamic-length string or byte array), then specify
    VARIABLE_WIDTH_BYTES but this must be your only field; this is for simplicity for handling serialization/deserialization.
    In this case, the size should correspond to the MAX size possible for this field, which is needed when serializing/deserializing.
    """
    cdef shared_ptr[criver.FieldDefinition] _definition

    def __cinit__(self, str name, FieldType type, size_t size = 0):
        if size == 0:
            if type == FieldType.FIXED_WIDTH_BYTES or \
                    type == FieldType.VARIABLE_WIDTH_BYTES:
                raise ValueError('Need to provide size with fixed/variable width bytes')
            elif type == FieldType.DOUBLE:
                size = 8
            elif type == FieldType.FLOAT:
                size = 4
            elif type == FieldType.INT16:
                size = 2
            elif type == FieldType.INT32:
                size = 4
            elif type == FieldType.INT64:
                size = 8
            else:
                raise ValueError('Unhandled field definition type!')

        self._definition = shared_ptr[criver.FieldDefinition](new criver.FieldDefinition(
            name.encode('UTF-8'), <criver.FieldDefinition_Type> type, size))

    @property
    def name(self):
        return deref(self._definition).name.decode('UTF-8')

    @property
    def type(self):
        return <FieldType> deref(self._definition).type

    cdef add_to(self, vector[criver.FieldDefinition] *vec):
        deref(vec).push_back(deref(self._definition))

    @staticmethod
    cdef FieldDefinition from_c(criver.FieldDefinition *fd):
        return FieldDefinition(
            deref(fd).name.decode('UTF-8'),
            <FieldType> deref(fd).type,
            deref(fd).size)

cdef class StreamSchema:
    """
    The schema for a particular stream. A stream has exactly one schema over its lifetime; this schema defines both the
    writing and reading structure of the stream (and, if in use, the on-disk representation of the stream).
    """
    cdef shared_ptr[criver.StreamSchema] _schema

    def __cinit__(self, list field_definitions):
        if field_definitions is None:
            return

        cdef FieldDefinition fd
        cdef vector[criver.FieldDefinition] field_def_vec = vector[criver.FieldDefinition]()

        for fd_py in field_definitions:
            fd = <FieldDefinition> fd_py
            fd.add_to(&field_def_vec)

        self._schema = shared_ptr[criver.StreamSchema](new criver.StreamSchema(field_def_vec))

    @property
    def field_definitions(self):
        cdef list ret = []
        cdef criver.FieldDefinition *fd_c
        cdef vector[criver.FieldDefinition] field_def_vec = <vector[criver.FieldDefinition]> \
            deref(self._schema).field_definitions

        for i in range(field_def_vec.size()):
            fd_c = &field_def_vec.at(i)
            ret.append(FieldDefinition.from_c(fd_c))
        return ret

    @staticmethod
    cdef StreamSchema from_c(criver.StreamSchema schema):
        cdef StreamSchema ret = StreamSchema.__new__(StreamSchema, [])
        ret._schema = shared_ptr[criver.StreamSchema](new criver.StreamSchema(schema))
        return ret

    def dtype(self) -> np.dtype:
        """
        Returns the equivalent NumPy dtype for this schema instance.
        """

        cdef list dtypes
        cdef criver.FieldDefinition *field_definition

        dtypes = list()
        cdef vector[criver.FieldDefinition] field_def_vec = <vector[criver.FieldDefinition]> \
            deref(self._schema).field_definitions
        for i in range(field_def_vec.size()):
            field_definition = &field_def_vec.at(i)
            field_name = deref(field_definition).name.decode('UTF-8')
            type = deref(field_definition).type

            if type == criver.FieldDefinition_Type.FIELD_DEFINITION_DOUBLE:
                dtypes.append((field_name, np.float64))
            elif type == criver.FieldDefinition_Type.FIELD_DEFINITION_FLOAT:
                dtypes.append((field_name, np.float32))
            elif type == criver.FieldDefinition_Type.FIELD_DEFINITION_INT16:
                dtypes.append((field_name, np.int16))
            elif type == criver.FieldDefinition_Type.FIELD_DEFINITION_INT64:
                dtypes.append((field_name, np.int64))
            elif type == criver.FieldDefinition_Type.FIELD_DEFINITION_INT32:
                dtypes.append((field_name, np.int32))
            elif type == criver.FieldDefinition_Type.FIELD_DEFINITION_FIXED_WIDTH_BYTES \
                    or type == criver.FieldDefinition_Type.FIELD_DEFINITION_VARIABLE_WIDTH_BYTES:
                dtypes.append((field_name, np.dtype((np.void, deref(field_definition).size))))
            else:
                raise ValueError('Unhandled numpy conversion')

        return np.dtype(dtypes)

    @staticmethod
    def from_dtype(dtype: np.dtype):
        """
        Creates a StreamSchema from a given NumPy dtype. This dtype should be a "structured array"
        dtype, where the dtype is a collection of named fields. See
        https://numpy.org/doc/stable/user/basics.rec.html for more details.
        """
        if dtype.fields is None:
            raise ValueError('Can only convert a structured array dtype with names.')

        cdef list field_definitions = []
        for field_name, details in dict(dtype.fields).items():
            type = details[0]

            if type == np.float64:
                field_definitions.append(FieldDefinition(field_name, FieldType.DOUBLE))
            elif type == np.float32:
                field_definitions.append(FieldDefinition(field_name, FieldType.FLOAT))
            elif type == np.int16:
                field_definitions.append(FieldDefinition(field_name, FieldType.INT16))
            elif type == np.int64:
                field_definitions.append(FieldDefinition(field_name, FieldType.INT64))
            elif type == np.int32:
                field_definitions.append(FieldDefinition(field_name, FieldType.INT32))
            elif type.kind == 'V':
                field_definitions.append(FieldDefinition(
                    field_name, FieldType.FIXED_WIDTH_BYTES, type.itemsize))
            else:
                raise ValueError('Unhandled numpy conversion')

        return StreamSchema(field_definitions)

cdef class StreamReader:
    """
    The main entry point for River for reading an existing stream. This class is initialized with a stream name
    corresponding to an existing stream, and allows for batch consumption of the stream. Reads requesting more data than
    is present in the stream will block.

    After constructing a StreamReader, you must call initialize with the name of the stream you wish to read.
    """

    cdef shared_ptr[criver.StreamReader] _reader

    def __cinit__(self, RedisConnection connection, int max_fetch_size = -1):
        """
         Construct an instance of a StreamReader. One StreamReader can be used with at most one underlying stream.

         :param connection: contains parameters to connect to Redis
         :param max_fetch_size: maximum number of elements to fetch from Redis at a time (to prevent untenably large
                                batches if a large number of bytes are consumed).
        """
        if max_fetch_size <= 0:
            self._reader = shared_ptr[criver.StreamReader](new criver.StreamReader(deref(connection._connection)))
        else:
            self._reader = shared_ptr[criver.StreamReader](new criver.StreamReader(
                deref(connection._connection), max_fetch_size))

    def initialize(self, stream_name: str, timeout_ms: int = -1):
        """
        Initialize this reader to a particular stream. If timeout_ms is positive, this call will wait for up to
        `timeout_ms` milliseconds for the stream to be created. When the timeout is exceeded or if no timeout was given
        and the stream does not exist, a StreamReaderException will be raised.
        """

        if timeout_ms > 0:
            deref(self._reader).Initialize(stream_name.encode('UTF-8'), timeout_ms)
        else:
            deref(self._reader).Initialize(stream_name.encode('UTF-8'))

    @property
    def schema(self: StreamReader) -> StreamSchema:
        return StreamSchema.from_c(deref(self._reader).schema())

    @property
    def stream_name(self: StreamReader) -> str:
        return deref(self._reader).stream_name().decode('UTF-8')

    @property
    def metadata(self: StreamReader) -> dict:
        cdef unordered_map[string, string] m = deref(self._reader).Metadata()
        cdef dict ret_bytes = m
        cdef dict ret = dict()
        for key, val in ret_bytes.items():
            ret[key.decode('UTF-8')] = val.decode('UTF-8')
        return ret

    @property
    def total_samples_read(self: StreamReader):
        return deref(self._reader).total_samples_read()

    @property
    def initialized_at_us(self: StreamReader):
        return deref(self._reader).initialized_at_us()

    @property
    def good(self: StreamReader) -> bool:
        """
        Whether this stream is "good" for reading (similar to std::ifstream's #good()). Synonymous with casting to bool.
        Indicates whether more samples can be read from this stream via this StreamReader.
        """
        return deref(self._reader).Good()

    def __bool__(self):
        """
        Same as #good().
        """
        return self.good

    def new_buffer(self, n: int) -> np.ndarray:
        """
        Returns an empty NumPy buffer of size `n` with a dtype matching the stream's schema. Note that the returned
        buffer is simply allocated and not also zeroed, so there's likely to be "junk" seen in the returned array.
        """
        return np.empty(n, dtype=self.schema.dtype())

    def read(self: StreamReader, arr: np.ndarray, timeout_ms: int = -1) -> int:
        """
        Read from the stream from where was last consumed. This call blocks until the desired number of samples is
        available in the underlying stream. The return value indicates how many samples were written to the buffer.
        If EOF has been reached, then #good() will return false, and any attempts to #read() will return -1.

        :param arr: The buffer into which data will be read from the stream. At most `arr.size` samples will be read
                    from the stream and written into `arr`. The return value of this call (if nonnegative) tells how many samples,
                    each of `sample_size` bytes as told by the schema, were written into the buffer. For VARIABLE_WIDTH_BYTES
                    fields, ensure this buffer is large enough to capture the maximum possible read size.

        :param timeout_ms: If positive, the maximum length of time this entire call can block while waiting for samples.
                           After the timeout, the stream can be partially read, and the return value is needed to determine samples read.

        :return: the number of elements read. This will always be less than or equal to `arr.size`. For example, if
                 there is a timeout, this could be a partially read buffer and so can be less than `arr.size`; this number can
                 be less than `arr.size` even if there is no timeout given, in the case of an EOF on the stream.
                 Returns -1 if EOF is encountered; if -1 is returned, buffer is guaranteed to not have been touched.
        """
        cdef int64_t size = arr.size
        cdef char* pointer = <char *> arr.data
        cdef int64_t ret = 0
        cdef int timeout_ms_c = timeout_ms

        with nogil:
            if timeout_ms_c <= 0:
                ret = deref(self._reader).ReadBytes(pointer, size)
            else:
                ret = deref(self._reader).ReadBytes(pointer, size, NULL, NULL, timeout_ms_c)
        return ret

    def tail(self, np.ndarray arr not None, int timeout_ms = -1) -> int:
        """
        Returns the last element in the stream after the previously seen elements. Blocks until there's at least one
        element available in the stream after the current cursor if no timeout is given; else, waits for the timeout.

        :param timeout_ms: If positive, the maximum length of time this entire call can block while waiting for a sample.
                           After the timeout there will be 0 or 1 elements read, and so the return value is needed to determine samples read.

        :return: the number of elements skipped and/or read, including the last element that might be written into the
                 buffer. Thus, this will return 0 in the event of a timeout; this will return >= 1 iff buffer is changed.
                 Returns -1 if there is an EOF in the stream.
        """
        cdef char* pointer = <char *> arr.data
        cdef int64_t ret = 0

        with nogil:
            if timeout_ms <= 0:
                ret = deref(self._reader).TailBytes(pointer)
            else:
                ret = deref(self._reader).TailBytes(pointer, timeout_ms)
        return ret

    def stop(self) -> None:
        """
        Stops this reader from being used in the future. Redis connections are freed; read() will no longer work; good()
        will return false.
        """
        deref(self._reader).Stop()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.stop()


cdef class StreamWriter:
    """
    The main entry point for River for writing a new stream. Streams are defined by a schema and a stream name, both of
    which are given in the `initialize()` call. All samples written to this stream must belong to the same schema. Once
    there are no more elements in this stream, call `stop()`; this will signal to any other readers that the stream has
    ended.
    """
    cdef shared_ptr[criver.StreamWriter] _writer

    def __cinit__(self, RedisConnection connection,
                  int64_t keys_per_redis_stream = -1,
                  int batch_size = -1):
        """
        Construct an instance of StreamWriter. One StreamWriter belongs to at most one stream.

        :param connection: Parameters to connect to Redis.
        :param batch_size: Number of samples in a batch that will be written/read from redis. Increasing this
                           number makes batches bigger and thus reduces the number of writes/reads to redis, but then also increases the
                           average latency of the stream.
        :param keys_per_redis_stream: the number of keys in each underlying redis stream. Default value reasoning is:
                                      2^24 = 17M keys per stream => ~350MB of memory on 64-bit redis with 8-byte sample size.
        """

        if keys_per_redis_stream > 0 and batch_size > 0:
            self._writer = shared_ptr[criver.StreamWriter](new criver.StreamWriter(
                deref(connection._connection), keys_per_redis_stream, batch_size))
        elif keys_per_redis_stream > 0:
            self._writer = shared_ptr[criver.StreamWriter](new criver.StreamWriter(
                deref(connection._connection), keys_per_redis_stream))
        else:
            self._writer = shared_ptr[criver.StreamWriter](new criver.StreamWriter(
                deref(connection._connection)))

    def initialize(self: StreamWriter, stream_name: str, schema: StreamSchema, user_metadata: dict = None):
        """
        Initialize this stream for writing. The given stream name must be unique within the Redis used. This
        initialization puts necessary information (e.g. schemas and timestamps) into redis. Optionally, it can accept
        a dict of user metadata to put in to Redis atomically.
        """
        cdef unordered_map[string, string] c_metadata
        if user_metadata is not None:
            for key, val in user_metadata.items():
                c_metadata[key.encode('UTF-8')] = val.encode('UTF-8')
            deref(self._writer).Initialize(stream_name.encode('UTF-8'),
                                           deref(schema._schema),
                                           c_metadata)
        else:
            deref(self._writer).Initialize(stream_name.encode('UTF-8'), deref(schema._schema))

    @property
    def schema(self):
        return StreamSchema.from_c(deref(self._writer).schema())

    @property
    def stream_name(self):
        return deref(self._writer).stream_name().decode('UTF-8')

    @property
    def metadata(self):
        """
        Returns all metadata set for this stream. Implementation note: this does a call to Redis under-the-hood and
        so can incur a little bit of overhead.
        """
        cdef unordered_map[string, string] m = deref(self._writer).Metadata()
        cdef dict ret_bytes = m
        cdef dict ret = dict()
        for key, val in ret_bytes.items():
            ret[key.decode('UTF-8')] = val.decode('UTF-8')
        return ret

    @metadata.setter
    def metadata(self, dict value):
        cdef unordered_map[string, string] v
        for key, val in value.items():
            v[key.encode('UTF-8')] = val.encode('UTF-8')
        deref(self._writer).SetMetadata(v)

    @property
    def total_samples_written(self):
        return deref(self._writer).total_samples_written()

    @property
    def initialized_at_us(self: StreamWriter):
        return deref(self._writer).initialized_at_us()

    def new_buffer(self, n: int) -> np.ndarray:
        """
        Returns an empty NumPy buffer of size `n` with a dtype matching the stream's schema. Note that the returned
        buffer is simply allocated and not also zeroed, so there's likely to be "junk" seen in the returned array.
        """
        return np.empty(n, dtype=self.schema.dtype())

    def write(self, arr: np.ndarray) -> None:
        """
        Writes data to the stream. Assumes that each element of the array `arr` is of the correct sample size (as
        defined by the stream's schema's sample size), and will write bytes in order of the schema fields. `arr.size`
        samples will be written to the stream.
        """
        cdef int64_t size = arr.size
        cdef char* pointer = <char *> arr.data

        with nogil:
            deref(self._writer).WriteBytes(pointer, size)

    def stop(self) -> None:
        """
        Stops this stream permanently. This method must be called once the stream is finished in order to notify readers
        that the stream has terminated.
        """
        deref(self._writer).Stop()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.stop()
