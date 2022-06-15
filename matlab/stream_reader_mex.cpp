#include "mex.h"
#include "class_handle.hpp"
#include "mex_helpers.hpp"
#include "stream_schema_helper.h"
#include <river/river.h>

using namespace river;

template<class TypeC, class TypeMx>
TypeMx *set_data(
    const FieldDefinition& field_def,
    const char *buffer,
    int64_t num_read,
    int64_t sample_size,
    int64_t col_offset_bytes) {
  TypeMx *dynamicData = (TypeMx *) mxMalloc(num_read * sizeof(TypeC));
  for (int row_idx = 0; row_idx < num_read; row_idx++) {
    const char *buffer_offset = &buffer[sample_size * row_idx + col_offset_bytes];
    dynamicData[row_idx] = *((const TypeC *) buffer_offset);
  }
  return dynamicData;

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
    if (nlhs != 1) {
      mexErrMsgTxt("stream_name: expected 1 output.");
      return;
    } else {
      plhs[0] = from_string(instance->stream_name());
      return;
    }
  } else if (!strcmp("schema_field_names", cmd)) {
    if (nlhs != 1) {
      mexErrMsgTxt("schema: expected 1 output.");
      return;
    } else {
      plhs[0] = schema_field_names(instance->schema());
      return;
    }
  } else if (!strcmp("schema_field_sizes", cmd)) {
    if (nlhs > 1) {
      mexErrMsgTxt("schema: expected 1 output.");
      return;
    }
    plhs[0] = schema_field_sizes(instance->schema());
  } else if (!strcmp("schema_field_types", cmd)) {
    if (nlhs != 1) {
      mexErrMsgTxt("schema: expected 1 output.");
      return;
    } else {
      plhs[0] = schema_field_types(instance->schema());
      return;
    }
  } else if (!strcmp("read", cmd)) {
    if (nlhs != 2) {
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
    if (num_read >= 0) {
      auto field_defs = instance->schema().field_definitions;
      int n_cols = field_defs.size();

      mxArray *out = mxCreateCellMatrix(1, n_cols);
      int64_t col_offset_bytes = 0;
      for (int col_idx = 0; col_idx < n_cols; col_idx++) {
        auto field_def = field_defs[col_idx];
        mxArray *out_col = nullptr;

        switch (field_def.type) {
          case FieldDefinition::DOUBLE:
            {
              mxDouble *out_col_data = set_data<double, mxDouble>(
                  field_def,
                  buffer.data(),
                  num_read,
                  sample_size,
                  col_offset_bytes);
              out_col = mxCreateNumericMatrix(
                  0, 0, mxDOUBLE_CLASS, mxREAL);
              mxSetDoubles(out_col, out_col_data);
            } break;
          case FieldDefinition::FLOAT:
            {
              mxSingle *out_col_data = set_data<float, mxSingle>(
                  field_def,
                  buffer.data(),
                  num_read,
                  sample_size,
                  col_offset_bytes);
              out_col = mxCreateNumericMatrix(
                  0, 0, mxSINGLE_CLASS, mxREAL);
              mxSetSingles(out_col, out_col_data);
            } break;
          case FieldDefinition::INT32:
            {
              mxInt32 *out_col_data = set_data<int32_t, mxInt32>(
                  field_def,
                  buffer.data(),
                  num_read,
                  sample_size,
                  col_offset_bytes);
              out_col = mxCreateNumericMatrix(
                  0, 0, mxINT32_CLASS, mxREAL);
              mxSetInt32s(out_col, out_col_data);
            } break;
          case FieldDefinition::INT64:
            {
              mxInt64 *out_col_data = set_data<int64_t, mxInt64>(
                  field_def,
                  buffer.data(),
                  num_read,
                  sample_size,
                  col_offset_bytes);
              out_col = mxCreateNumericMatrix(
                  0, 0, mxINT64_CLASS, mxREAL);
              mxSetInt64s(out_col, out_col_data);
            } break;
          case FieldDefinition::FIXED_WIDTH_BYTES:
            {
              out_col = mxCreateCellMatrix(num_read, 1);
              for (int row_idx = 0; row_idx < num_read; row_idx++) {
                mxUint8 *out_col_data = (mxUint8 *) mxMalloc(field_def.size * sizeof(mxUint8));

                const char *buffer_offset = &buffer[sample_size * row_idx + col_offset_bytes];
                for (int byte_idx = 0; byte_idx < field_def.size; byte_idx++) {
                  out_col_data[byte_idx] = buffer_offset[byte_idx];
                }

                mxArray *out_col_data_wrapped = mxCreateNumericMatrix(0, 0, mxUINT8_CLASS, mxREAL);
                mxSetUint8s(out_col_data_wrapped, out_col_data);
                mxSetM(out_col_data_wrapped, 1);
                mxSetN(out_col_data_wrapped, field_def.size);
                mxSetCell(out_col, row_idx, out_col_data_wrapped);
              }
            } break;
          default:
            {
              mexErrMsgTxt("read: unhandled field def.");
            } return;
        }

        if (field_def.type != FieldDefinition::FIXED_WIDTH_BYTES) {
          mxSetM(out_col, num_read);
          mxSetN(out_col, 1);
        }
        mxSetCell(out, col_idx, out_col);
        col_offset_bytes += field_def.size;
      }
      plhs[1] = out;
    } else {
      plhs[1] = mxCreateCellMatrix(0, 0);
    }
    return;
  } else if (!strcmp("stop", cmd)) {
    if (nlhs != 0) {
      mexErrMsgTxt("stop: no outputs expected.");
      return;
    }
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
