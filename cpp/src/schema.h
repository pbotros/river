//
// Created by Paul Botros on 10/29/19.
//

#ifndef PARENT_SCHEMA_H
#define PARENT_SCHEMA_H

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace river {

/**
 * One or more fields that are present in each sample of a particular stream. This definition governs how this will be
 * serialized to Redis and the columns in the persisted file.
 *
 * While most field definitions are fixed-width, the VARIABLE_WIDTH_BYTES field is a bit different. If you want to use
 * a variable-width bytes (e.g. a dynamic-length string or byte array), then specify VARIABLE_WIDTH_BYTES but this must
 * be your only field; this is for simplicity for handling serialization/deserialization. In this case, the size should
 * correspond to the MAX size possible for this field, which is needed when serializing/deserializing.
 */
typedef struct FieldDefinition {
    std::string name;
    int size;

    /* If adding a new stream, ensure to add a test in ingester_test to confirm round-trip works. */
    typedef enum type {
        DOUBLE,
        FLOAT,
        INT16,
        INT32,
        INT64,
        FIXED_WIDTH_BYTES,
        VARIABLE_WIDTH_BYTES,
    } Type;
    Type type;

    FieldDefinition &operator=(const FieldDefinition &t) = default;

    FieldDefinition(std::string name_, Type type_, int size_) : name(std::move(name_)), size(size_), type(type_) {}
} FieldDefinition;

/**
 * The schema for a particular stream. A stream has exactly one schema over its lifetime; this schema defines both the
 * writing and reading structure of the stream (and, if in use, the on-disk representation of the stream).
 */
class StreamSchema {
 public:
  std::vector<FieldDefinition> field_definitions;

  explicit StreamSchema() : field_definitions(std::vector<FieldDefinition>()) {}

  StreamSchema(std::vector<FieldDefinition> field_definitions_) {
    field_definitions = field_definitions_;
  }

  StreamSchema &operator=(const StreamSchema &t) = default;

  int sample_size() const {
    int total = 0;
    for (auto &it : field_definitions) {
      total += it.size;
    }
    return total;
  }

  bool has_variable_width_field() const {
    for (auto &it : field_definitions) {
      if (it.type == FieldDefinition::VARIABLE_WIDTH_BYTES) {
        return true;
      }
    }
    return false;
  }

  std::string ToJson() const;

  static StreamSchema FromJson(std::string json);
};
}

#endif //PARENT_SCHEMA_H
