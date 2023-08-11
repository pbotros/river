#include "Python.h"
#include <river/river.h>
#include <exception>
#include <string>

using namespace std;

#ifdef __cplusplus
  #define EXPORT_RIVER_EXCEPTION extern
#else
  #define EXPORT_RIVER_EXCEPTION extern "C"
#endif
EXPORT_RIVER_EXCEPTION PyObject *stream_exists_exception;
EXPORT_RIVER_EXCEPTION PyObject *stream_does_not_exist_exception;
EXPORT_RIVER_EXCEPTION PyObject *stream_reader_exception;
EXPORT_RIVER_EXCEPTION PyObject *stream_writer_exception;
EXPORT_RIVER_EXCEPTION PyObject *redis_exception;

void raise_py_error() {
  try {
    throw;
  } catch (river::StreamExistsException& e) {
    PyObject *e_obj = PyObject_CallFunction(
        stream_exists_exception, "s", e.what());
    PyErr_SetObject(stream_exists_exception, e_obj);
  } catch (river::StreamDoesNotExistException& e) {
    PyObject *e_obj = PyObject_CallFunction(
        stream_does_not_exist_exception, "s", e.what());
    PyErr_SetObject(stream_does_not_exist_exception, e_obj);
  } catch (river::StreamWriterException& e) {
      PyObject *e_obj = PyObject_CallFunction(
              stream_writer_exception, "s", e.what());
      PyErr_SetObject(stream_writer_exception, e_obj);
  } catch (river::StreamReaderException& e) {
      PyObject *e_obj = PyObject_CallFunction(
              stream_reader_exception, "s", e.what());
      PyErr_SetObject(stream_reader_exception, e_obj);
  } catch (river::internal::RedisException& e) {
      PyObject *e_obj = PyObject_CallFunction(
              redis_exception, "s", e.what());
      PyErr_SetObject(redis_exception, e_obj);
  } catch (const std::exception& e) {
    PyErr_SetString(PyExc_RuntimeError, e.what());
  }
}

