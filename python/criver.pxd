# distutils: language = c++
from libcpp.vector cimport vector
from libcpp.memory cimport shared_ptr
from libcpp.unordered_map cimport unordered_map
from libcpp.string cimport string
from libcpp cimport bool
from libc.stdint cimport int64_t

cdef extern from "exception_handling.h" nogil:
  cdef void raise_py_error()

cdef extern from "<river/river.h>" namespace "river" nogil:
    cdef cppclass RedisConnection:
        RedisConnection(string redis_hostname, int redis_port);
        RedisConnection(string redis_hostname, int redis_port, string redis_password);

        const string redis_hostname();
        const int redis_port();
        const string redis_password();

    ctypedef enum FieldDefinition_Type "river::FieldDefinition::Type":
        FIELD_DEFINITION_DOUBLE "river::FieldDefinition::Type::DOUBLE"
        FIELD_DEFINITION_FLOAT "river::FieldDefinition::Type::FLOAT"
        FIELD_DEFINITION_INT16 "river::FieldDefinition::Type::INT16"
        FIELD_DEFINITION_INT32 "river::FieldDefinition::Type::INT32"
        FIELD_DEFINITION_INT64 "river::FieldDefinition::Type::INT64"
        FIELD_DEFINITION_FIXED_WIDTH_BYTES "river::FieldDefinition::Type::FIXED_WIDTH_BYTES"
        FIELD_DEFINITION_VARIABLE_WIDTH_BYTES "river::FieldDefinition::Type::VARIABLE_WIDTH_BYTES"

    cdef cppclass FieldDefinition:
        string name;
        int size;
        FieldDefinition_Type type;

        FieldDefinition(string name, FieldDefinition_Type type, int size);

    cdef cppclass StreamSchema:
        StreamSchema();
        StreamSchema(StreamSchema schema);
        StreamSchema(vector[FieldDefinition] field_definitions);
        vector[FieldDefinition] field_definitions;

    cdef cppclass StreamReader:
        StreamReader(RedisConnection connection) except +raise_py_error;
        StreamReader(RedisConnection connection, int max_fetch_size) except +raise_py_error;

        void Initialize(const string stream_name) except +raise_py_error;
        void Initialize(const string stream_name, int timeout_ms) except +raise_py_error;

        int64_t ReadBytes(char *buffer, int64_t num_samples) except +raise_py_error;
        int64_t ReadBytes(char *buffer, int64_t num_samples, int **sizes) except +raise_py_error;
        int64_t ReadBytes(char *buffer, int64_t num_samples, int **sizes, string **keys) except +raise_py_error;
        int64_t ReadBytes(char *buffer, int64_t num_samples, int **sizes, string **keys, int timeout_ms) except +raise_py_error;

        int64_t TailBytes(char *buffer) except +raise_py_error;
        int64_t TailBytes(char *buffer, int timeout_ms) except +raise_py_error;
        int64_t TailBytes(char *buffer, int timeout_ms, char *key) except +raise_py_error;
        int64_t TailBytes(char *buffer, int timeout_ms, char *key, int64_t *sample_index) except +raise_py_error;

        void Stop() except +raise_py_error;

        bool Good() except +raise_py_error;
        bool operator bool() except +raise_py_error;
        int64_t total_samples_read() except +raise_py_error;
        int64_t initialized_at_us() except +raise_py_error;
        StreamSchema schema() except +raise_py_error;
        string stream_name() except +raise_py_error;
        unordered_map[string, string] Metadata() except +raise_py_error;

    cdef cppclass StreamWriter:
        StreamWriter(RedisConnection connection) except +raise_py_error;
        StreamWriter(RedisConnection connection, int64_t keys_per_redis_stream) except +raise_py_error;
        StreamWriter(RedisConnection connection, int64_t keys_per_redis_stream, int batch_size) except +raise_py_error;

        void Initialize(const string stream_name, StreamSchema schema) except +raise_py_error;
        void Initialize(const string stream_name, StreamSchema schema, unordered_map[string, string] metadata) except +raise_py_error;

        void WriteBytes(char *buffer, int64_t num_samples) except +raise_py_error;

        void Stop() except +raise_py_error;

        int64_t total_samples_written() except +raise_py_error;
        int64_t initialized_at_us() except +raise_py_error;
        StreamSchema schema() except +raise_py_error;
        string stream_name() except +raise_py_error;

        unordered_map[string, string] Metadata() except +raise_py_error;
        void SetMetadata(unordered_map[string, string] m) except +raise_py_error;
