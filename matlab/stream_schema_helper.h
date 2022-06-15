#include "mex.h"
#include <river/river.h>

using namespace river;

mxArray *schema_field_sizes(const StreamSchema& schema) {
  auto field_defs = schema.field_definitions;
  mxDouble *dynamicData = (mxDouble *) mxMalloc(field_defs.size() * sizeof(double));
  for (int i = 0; i < field_defs.size(); i++) {
    dynamicData[i] = field_defs[i].size;
  }
  mxArray *out = mxCreateNumericMatrix(1, field_defs.size(), mxDOUBLE_CLASS, mxREAL);
  mxSetDoubles(out, dynamicData);
  mxSetM(out, 1);
  mxSetN(out, field_defs.size());
  return out;
}

mxArray *schema_field_names(const StreamSchema& schema) {
  mxArray *ret = mxCreateCellMatrix(schema.field_definitions.size(), 1);
  for (int i = 0; i < schema.field_definitions.size(); i++) {
    FieldDefinition fd = schema.field_definitions[i];
    mxSetCell(ret, i, from_string(fd.name));
  }
  return ret;
}

mxArray *schema_field_types(const StreamSchema& schema) {
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
      case FieldDefinition::FIXED_WIDTH_BYTES:
        mxSetCell(ret, i, from_string("cell"));
        break;
      default:
        mxSetCell(ret, i, from_string("UNKNOWN"));
        break;
    }
  }
  return ret;
}
