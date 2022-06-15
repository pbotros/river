#include "mex.h"
#include "class_handle.hpp"
#include "mex_helpers.hpp"
#include "stream_schema_helper.h"
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
    if (nrhs != 3 && nrhs != 4) {
      mexErrMsgTxt("New: One input expected");
      return;
    }
    // Format: variable names, then variable types
    // Same format as in: https://www.mathworks.com/help/matlab/ref/table.html
    const mxArray *varNames = prhs[1];
    const mxArray *varTypes = prhs[2];
    const mxArray **fieldSizes;
    if (nrhs >= 4) {
      fieldSizes = &prhs[3];
    } else {
      fieldSizes = nullptr;
    }

    int num_vars = mxGetNumberOfElements(varNames);
    if (num_vars <= 0) {
      mexErrMsgTxt("New: expected > 0 vars");
      return;
    }

    if (num_vars != mxGetNumberOfElements(varTypes)) {
      mexErrMsgTxt("New: expected equal number of var names and types");
      return;
    }

    if (fieldSizes != nullptr) {
      if (num_vars != mxGetNumberOfElements(*fieldSizes)) {
        mexErrMsgTxt("New: expected equal number of var names and sizes");
        return;
      }
      if (mxGetClassID(*fieldSizes) != mxDOUBLE_CLASS) {
        mexErrMsgTxt("New: field sizes should be double");
        return;
      }
    }

    if (mxGetClassID(varNames) != mxCELL_CLASS) {
      mexErrMsgTxt("New: var names should be strings");
      return;
    }

    if (mxGetClassID(varTypes) != mxCELL_CLASS) {
      mexErrMsgTxt("New: var types should be strings");
      return;
    }

    std::vector<FieldDefinition> field_defs;
    char field_type_char[256];
    char field_name_char[256];

    for (int i = 0; i < num_vars; i++) {
      mxArray *cell = mxGetCell(varTypes, i);
      mxGetString(cell, field_type_char, sizeof(field_type_char));

      FieldDefinition::Type field_type;
      size_t size;
      if (!strcmp(field_type_char, "double")) {
        field_type = FieldDefinition::DOUBLE;
        size = 8;
      } else if (!strcmp(field_type_char, "float")) {
        field_type = FieldDefinition::FLOAT;
        size = 4;
      } else if (!strcmp(field_type_char, "int32")) {
        field_type = FieldDefinition::INT32;
        size = 4;
      } else if (!strcmp(field_type_char, "int64")) {
        field_type = FieldDefinition::INT64;
        size = 8;
      } else if (!strcmp(field_type_char, "cell")) {
        if (fieldSizes == nullptr) {
          mexErrMsgTxt("If using a FIXED_WIDTH_BYTES via cell, you need to pass in a size.");
          return;
        }
        field_type = FieldDefinition::FIXED_WIDTH_BYTES;
        auto ints = mxGetDoubles(*fieldSizes);
        size = (int64_t) ints[i];
      } else {
        mexErrMsgTxt("New: Unknown field def type");
      }

      cell = mxGetCell(varNames, i);
      auto name = to_string(cell);
      field_defs.push_back(FieldDefinition(*name, field_type, size));
    }

    StreamSchema *schema = new StreamSchema(field_defs);
    plhs[0] = convertPtr2Mat<StreamSchema>(schema);
    return;
  }

  // Check there is a second input, which should be the class instance handle
  if (nrhs < 2) {
    mexErrMsgTxt("Second input should be a class instance handle.");
  }

  // Delete
  if (!strcmp("delete", cmd)) {
    // Destroy the C++ object
    destroyObject<StreamSchema>(prhs[1]);
    // Warn if other commands were ignored
    if (nlhs != 0 || nrhs != 2)
      mexWarnMsgTxt("Delete: Unexpected arguments ignored.");
    return;
  }

  if (nrhs != 2) {
    mexWarnMsgTxt("Unexpected arguments ignored.");
    return;
  }
  if (nlhs == 0) {
    return;
  }
  if (nlhs != 1) {
    mexWarnMsgTxt("Only single output expected.");
    return;
  }

  StreamSchema* instance = convertMat2Ptr<StreamSchema>(prhs[1]);
  if (!strcmp("field_names", cmd)) {
    plhs[0] = schema_field_names(*instance);
  } else if (!strcmp("field_types", cmd)) {
    plhs[0] = schema_field_types(*instance);
  } else if (!strcmp("field_sizes", cmd)) {
    plhs[0] = schema_field_sizes(*instance);
  } else {
    mexErrMsgTxt("Command not recognized.");
  }
}
