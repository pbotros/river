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
    INT32 = criver.FIELD_DEFINITION_INT32
    INT64 = criver.FIELD_DEFINITION_INT64
    FIXED_WIDTH_BYTES = criver.FIELD_DEFINITION_FIXED_WIDTH_BYTES
    VARIABLE_WIDTH_BYTES = criver.FIELD_DEFINITION_VARIABLE_WIDTH_BYTES

cdef class FieldDefinition:
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
        if dtype.fields is None:
            raise ValueError('Can only convert a structured array dtype with names.')

        cdef list field_definitions = []
        for field_name, details in dict(dtype.fields).items():
            type = details[0]

            if type == np.float64:
                field_definitions.append(FieldDefinition(field_name, FieldType.DOUBLE))
            elif type == np.float32:
                field_definitions.append(FieldDefinition(field_name, FieldType.FLOAT))
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
    cdef shared_ptr[criver.StreamReader] _reader

    def __cinit__(self, RedisConnection connection, int max_fetch_size = -1):
        if max_fetch_size <= 0:
            self._reader = shared_ptr[criver.StreamReader](new criver.StreamReader(deref(connection._connection)))
        else:
            self._reader = shared_ptr[criver.StreamReader](new criver.StreamReader(
                deref(connection._connection), max_fetch_size))

    def initialize(self, stream_name: str, timeout_ms: int = -1):
        if timeout_ms > 0:
            deref(self._reader).Initialize(stream_name.encode('UTF-8'), timeout_ms)
        else:
            deref(self._reader).Initialize(stream_name.encode('UTF-8'))

    @property
    def schema(self: StreamReader) -> StreamSchema:
        return StreamSchema.from_c(deref(self._reader).schema())

    @property
    def stream_name(self: StreamReader) -> str:
        return deref(self._reader).stream_name()

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
    def good(self: StreamReader) -> bool:
        return deref(self._reader).Good()

    def __bool__(self):
        return self.good

    def read(self: StreamReader, arr: np.ndarray, timeout_ms: int = -1) -> int:
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
        cdef char* pointer = <char *> arr.data
        cdef int64_t ret = 0

        with nogil:
            if timeout_ms <= 0:
                ret = deref(self._reader).TailBytes(pointer)
            else:
                ret = deref(self._reader).TailBytes(pointer, timeout_ms)
        return ret

    def stop(self) -> None:
        deref(self._reader).Stop()

cdef class StreamWriter:
    cdef shared_ptr[criver.StreamWriter] _writer

    def __cinit__(self, RedisConnection connection,
                  unsigned long keys_per_redis_stream = -1,
                  int batch_size = -1):
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
        return deref(self._writer).stream_name()

    @property
    def metadata(self):
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

    def write(self, arr: np.ndarray) -> None:
        cdef int64_t size = arr.size
        cdef char* pointer = <char *> arr.data

        with nogil:
            deref(self._writer).WriteBytes(pointer, size)

    def stop(self) -> None:
        deref(self._writer).Stop()
