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
    if (nrhs != 3) {
      mexErrMsgTxt("New: One input expected");
      return;
    }
    // Format: variable names, then variable types
    // Same format as in: https://www.mathworks.com/help/matlab/ref/table.html
    const mxArray *varNames = prhs[1];
    const mxArray *varTypes = prhs[2];
    int num_vars = mxGetNumberOfElements(varNames);
    if (num_vars <= 0) {
      mexErrMsgTxt("New: expected > 0 vars");
      return;
    }

    if (num_vars != mxGetNumberOfElements(varTypes)) {
      mexErrMsgTxt("New: expected equal number of var names and types");
      return;
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
    auto field_defs = instance->field_definitions;
    mxArray *out = mxCreateCellMatrix(1, field_defs.size());
    for (int i = 0; i < field_defs.size(); i++) {
      mxSetCell(out, i, from_string(field_defs[i].name));
    }
    plhs[0] = out;
  } else if (!strcmp("field_types", cmd)) {
    auto field_defs = instance->field_definitions;
    mxArray *out = mxCreateCellMatrix(1, field_defs.size());
    for (int i = 0; i < field_defs.size(); i++) {
      switch (field_defs[i].type) {
        case FieldDefinition::DOUBLE:
          mxSetCell(out, i, from_string("double"));
          break;
        case FieldDefinition::FLOAT:
          mxSetCell(out, i, from_string("single"));
          break;
        case FieldDefinition::INT32:
          mxSetCell(out, i, from_string("int32"));
          break;
        case FieldDefinition::INT64:
          mxSetCell(out, i, from_string("int64"));
          break;
        default:
          mexErrMsgTxt("Unhandled field def type");
          return;
      }
    }
    plhs[0] = out;
  } else {
    mexErrMsgTxt("Command not recognized.");
  }
}
