#include "mex.h"
#include "class_handle.hpp"
#include "mex_helpers.hpp"
#include <river/river.h>

using namespace river;

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
    plhs[0] = convertPtr2Mat<StreamWriter>(new StreamWriter(*connection));
    return;
  }

  // Check there is a second input, which should be the class instance handle
  if (nrhs < 2) {
    mexErrMsgTxt("Second input should be a class instance handle.");
  }

  // Delete
  if (!strcmp("delete", cmd)) {
    // Destroy the C++ object
    destroyObject<StreamWriter>(prhs[1]);
    // Warn if other commands were ignored
    if (nlhs != 0 || nrhs != 2)
      mexWarnMsgTxt("Delete: Unexpected arguments ignored.");
    return;
  }

  // Get the class instance pointer from the second input
  StreamWriter* instance = convertMat2Ptr<StreamWriter>(prhs[1]);

  if (!strcmp("initialize", cmd)) {
    if (nlhs != 0) {
      mexErrMsgTxt("Initialize: no outputs expected.");
      return;
    }
    if (nrhs == 4) {
      auto stream_name = to_string(prhs[2]);
      mxArray *handle = mxGetProperty(prhs[3], 0, "objectHandle");
      StreamSchema *schema = convertMat2Ptr<StreamSchema>(handle);
      instance->Initialize(*stream_name, *schema);
      // TODO: metadata not supported
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
    if (nlhs != 1) {
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
  } else if (!strcmp("write", cmd)) {
    if (nlhs != 0) {
      mexErrMsgTxt("write: expected 0 outputs.");
      return;
    }

    if (nrhs != 3) {
      mexErrMsgTxt("write: expected 1 input param.");
      return;
    }

    const mxArray *data = prhs[2];
    if (mxGetClassID(data) != mxCELL_CLASS) {
      mexErrMsgTxt("Write: data should be cell array of cols");
      return;
    }

    auto field_defs = instance->schema().field_definitions;
    int n_cols = field_defs.size();
    if (mxGetNumberOfElements(data) != n_cols) {
      mexErrMsgTxt("Write: data should be cell array of cols (wrong #)");
      return;
    }

    int64_t n_to_write = mxGetNumberOfElements(mxGetCell(data, 1));
    int64_t sample_size = instance->schema().sample_size();
    int64_t total_size_bytes = sample_size * n_to_write;
    std::vector<char> buffer(total_size_bytes);

    // Copy the table columns into a temp buffer before giving to River
    int64_t col_offset_bytes = 0;
    for (int col_idx = 0; col_idx < n_cols; col_idx++) {
      auto field_def = field_defs[col_idx];
      switch (field_def.type) {
        case FieldDefinition::DOUBLE: {
                                        mxDouble *data_col = mxGetDoubles(mxGetCell(data, col_idx));
                                        for (int row_idx = 0; row_idx < n_to_write; row_idx++) {
                                          const char *buffer_offset = &buffer[row_idx * sample_size + col_offset_bytes];
                                          *((double *) buffer_offset) = data_col[row_idx];
                                        }
                                      } break;
        case FieldDefinition::FLOAT: {
                                       mxSingle *data_col = mxGetSingles(mxGetCell(data, col_idx));
                                       for (int row_idx = 0; row_idx < n_to_write; row_idx++) {
                                         const char *buffer_offset = &buffer[row_idx * sample_size + col_offset_bytes];
                                         *((float *) buffer_offset) = data_col[row_idx];
                                       }
                                     } break;
        case FieldDefinition::INT32: {
                                       mxInt32 *data_col = mxGetInt32s(mxGetCell(data, col_idx));
                                       for (int row_idx = 0; row_idx < n_to_write; row_idx++) {
                                         const char *buffer_offset = &buffer[row_idx * sample_size + col_offset_bytes];
                                         *((int32_t *) buffer_offset) = data_col[row_idx];
                                       }
                                     } break;
        case FieldDefinition::INT64: {
                                       mxInt64 *data_col = mxGetInt64s(mxGetCell(data, col_idx));
                                       for (int row_idx = 0; row_idx < n_to_write; row_idx++) {
                                         const char *buffer_offset = &buffer[row_idx * sample_size + col_offset_bytes];
                                         *((int64_t *) buffer_offset) = data_col[row_idx];
                                       }
                                     } break;
        default: {
                   mexErrMsgTxt("write: unhandled field def.");
                 } return;
      }
      col_offset_bytes += field_def.size;
    }

    instance->WriteBytes(buffer.data(), n_to_write);
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
