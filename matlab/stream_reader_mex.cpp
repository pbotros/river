#include "mex.h"
#include "class_handle.hpp"
#include "mex_helpers.hpp"
#include <river/river.h>
#include <string.h>
#include <memory>

using namespace river;
using namespace std;

template<typename TypeMx>
using MxArraySetter = void (*)(mxArray*, TypeMx *);

template<class TypeC, class TypeMx>
mxArray *set_data(const char *buffer, mxClassID class_id, MxArraySetter<TypeMx> setter) {
  TypeC val = *((const TypeC *) buffer);
  TypeMx *dynamicData = (TypeMx *) mxMalloc(1 * sizeof(TypeC));
  dynamicData[0] = val;

  mxArray *ret = mxCreateNumericMatrix(0, 0, class_id, mxREAL);
  setter(ret, dynamicData);
  mxSetM(ret, 1);
  mxSetN(ret, 1);
  return ret;
}


void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
  // Get the command string
  char cmd[64];
  if (nrhs < 1 || mxGetString(prhs[0], cmd, sizeof(cmd)))
    mexErrMsgTxt("First input should be a command string less than 64 characters long.");

  // New
  if (!strcmp("new", cmd)) {
    // Check parameters
    if (nlhs != 1) {
      mexErrMsgTxt("New: One output expected.");
      return;
    }
    if (nrhs != 2) {
      mexErrMsgTxt("New: One input expected");
      return;
    }

    mxArray *handle = mxGetProperty(prhs[1], 0, "objectHandle");
    RedisConnection *connection = convertMat2Ptr<RedisConnection>(handle);
    plhs[0] = convertPtr2Mat<StreamReader>(new StreamReader(*connection));
    return;
  }

  // Check there is a second input, which should be the class instance handle
  if (nrhs < 2) {
    mexErrMsgTxt("Second input should be a class instance handle.");
  }

  // Delete
  if (!strcmp("delete", cmd)) {
    // Destroy the C++ object
    destroyObject<StreamReader>(prhs[1]);
    // Warn if other commands were ignored
    if (nlhs != 0 || nrhs != 2)
      mexWarnMsgTxt("Delete: Unexpected arguments ignored.");
    return;
  }

  // Get the class instance pointer from the second input
  StreamReader* instance = convertMat2Ptr<StreamReader>(prhs[1]);

  if (!strcmp("initialize", cmd)) {
    if (nlhs != 0) {
      mexErrMsgTxt("Initialize: no outputs expected.");
      return;
    }
    if (nrhs == 3) {
      auto stream_name = to_string(prhs[2]);
      instance->Initialize(*stream_name);
    } else if (nrhs == 4) {
      auto stream_name = to_string(prhs[2]);
      int timeout_ms = to_int(prhs[3]);
      instance->Initialize(*stream_name, timeout_ms);
    } else {
      mexErrMsgTxt("Initialize: Unexpected arguments.");
    }
  } else if (!strcmp("stream_name", cmd)) {
    if (nlhs > 1) {
      mexErrMsgTxt("stream_name: expected 1 output.");
      return;
    } else {
      plhs[0] = from_string(instance->stream_name());
      return;
    }
  } else if (!strcmp("schema_field_names", cmd)) {
    if (nlhs > 1) {
      mexErrMsgTxt("schema: expected 1 output.");
      return;
    } else {
      auto schema = instance->schema();
      mxArray *ret = mxCreateCellMatrix(schema.field_definitions.size(), 1);
      for (int i = 0; i < schema.field_definitions.size(); i++) {
        FieldDefinition fd = schema.field_definitions[i];
        mxSetCell(ret, i, from_string(fd.name));
      }
      plhs[0] = ret;
      return;
    }
  } else if (!strcmp("schema_field_types", cmd)) {
    if (nlhs > 1) {
      mexErrMsgTxt("schema: expected 1 output.");
      return;
    } else {
      auto schema = instance->schema();
      mxArray *ret = mxCreateCellMatrix(schema.field_definitions.size(), 1);
      for (int i = 0; i < schema.field_definitions.size(); i++) {
        FieldDefinition fd = schema.field_definitions[i];
        switch (fd.type) {
          case FieldDefinition::DOUBLE:
            mxSetCell(ret, i, from_string("double"));
            break;
          case FieldDefinition::FLOAT:
            mxSetCell(ret, i, from_string("single"));
            break;
          case FieldDefinition::INT32:
            mxSetCell(ret, i, from_string("int32"));
            break;
          case FieldDefinition::INT64:
            mxSetCell(ret, i, from_string("int64"));
            break;
          default:
            mxSetCell(ret, i, from_string("UNKNOWN"));
            break;
        }
      }
      plhs[0] = ret;
      return;
    }
  } else if (!strcmp("read", cmd)) {
    if (nlhs > 2) {
      mexErrMsgTxt("read: expected 2 outputs.");
      return;
    }

    if (nrhs != 3 && nrhs != 4) {
      mexErrMsgTxt("read: expected 1 or 2 input params.");
      return;
    }

    int64_t n_to_read = to_int(prhs[2]);
    int64_t sample_size = instance->schema().sample_size();
    int64_t total_size_bytes = sample_size * n_to_read;
    std::vector<char> buffer(total_size_bytes);
    int64_t num_read = instance->ReadBytes(buffer.data(), n_to_read);

    plhs[0] = from_int(num_read);

    auto field_defs = instance->schema().field_definitions;
    int n_cols = field_defs.size();

    // The vector of unique_ptr will ensure the pointers are cleared up
    // upon destruction, while the _ptrs var is usable for the C API.
    vector<unique_ptr<char>> stream_names_chars;
    vector<const char*> stream_names_chars_ptrs;
    for (int col_idx = 0; col_idx < n_cols; col_idx++) {
      auto field_def = field_defs[col_idx];

      char *c = new char[field_def.name.size() + 1];
      strcpy(c, field_def.name.c_str());
      stream_names_chars.push_back(unique_ptr<char>(c));
      stream_names_chars_ptrs.push_back(c);
    }

    if (num_read <= 0) {
      plhs[1] = mxCreateStructMatrix(1, 0, stream_names_chars_ptrs.size(), stream_names_chars_ptrs.data());
      return;
    }

    mxArray *out = mxCreateStructMatrix(1, num_read, stream_names_chars_ptrs.size(), stream_names_chars_ptrs.data());
    for (int row_idx = 0; row_idx < num_read; row_idx++) {
      int64_t col_offset_bytes = 0;
      for (int col_idx = 0; col_idx < n_cols; col_idx++) {
        auto field_def = field_defs[col_idx];
        const char *buffer_offset = &buffer[row_idx * sample_size + col_offset_bytes];
        mxArray *val;
        switch (field_def.type) {
          case FieldDefinition::DOUBLE:
            {
              val = set_data<double, mxDouble>(buffer_offset, mxDOUBLE_CLASS, (MxArraySetter<mxDouble>)mxSetDoubles);
            } break;
          case FieldDefinition::INT32:
            {
              val = set_data<int32_t, mxInt32>(buffer_offset, mxINT32_CLASS, (MxArraySetter<mxInt32>)mxSetInt32s);
            } break;
          default:
            {
              mexErrMsgTxt("read: unhandled field def.");
            } return;
        }
        mxSetFieldByNumber(out, row_idx, col_idx, val);
        col_offset_bytes += field_def.size;
      }
    }

    for (int i = 0; i < mxGetNumberOfFields(out); i++) {
      const char *foo = mxGetFieldNameByNumber(out, i);
    }
    plhs[1] = out;
    return;
  } else if (!strcmp("stop", cmd)) {
    if (nrhs == 2) {
      instance->Stop();
      return;
    } else {
      mexErrMsgTxt("Initialize: Unexpected arguments.");
    }
  } else {
    // Got here, so command not recognized
    mexErrMsgTxt("Command not recognized.");
  }
}
