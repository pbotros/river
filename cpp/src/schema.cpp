//
// Created by Paul Botros on 10/29/19.
//

#include "schema.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace river {

std::string StreamSchema::ToJson() const {
  json holder = json::array();
  for (auto &it : field_definitions) {
    json field;
    field["name"] = it.name;
    field["size"] = it.size;
    switch (it.type) {
      case FieldDefinition::DOUBLE:
        field["type"] = "DOUBLE";
        break;
      case FieldDefinition::FLOAT:
        field["type"] = "FLOAT";
        break;
      case FieldDefinition::INT16:
        field["type"] = "INT16";
        break;
      case FieldDefinition::INT32:
        field["type"] = "INT32";
        break;
      case FieldDefinition::INT64:
        field["type"] = "INT64";
        break;
      case FieldDefinition::FIXED_WIDTH_BYTES:
        field["type"] = "FIXED_WIDTH_BYTES";
        break;
      case FieldDefinition::VARIABLE_WIDTH_BYTES:
        field["type"] = "VARIABLE_WIDTH_BYTES";
        break;
      default:
        throw std::invalid_argument("Unhandled type");
    }
    holder.push_back(field);
  }
  json parent;
  parent["field_definitions"] = holder;
  return parent.dump();
}

StreamSchema StreamSchema::FromJson(std::string json_str) {
    json pt = json::parse(json_str);

    std::vector<FieldDefinition> field_definitions;
    for (const auto &field : pt["field_definitions"]) {
      std::string name = field["name"];
      int size;
      if (field["size"].is_number_integer()) {
        size = field["size"];
      } else {
        size = std::stoi(field["size"].get<std::string>());
      }
      std::string type_str = field["type"];
      FieldDefinition::Type type;
      if (type_str == "DOUBLE") {
        type = FieldDefinition::DOUBLE;
      } else if (type_str == "FLOAT") {
        type = FieldDefinition::FLOAT;
      } else if (type_str == "INT16") {
        type = FieldDefinition::INT16;
      } else if (type_str == "INT32") {
        type = FieldDefinition::INT32;
      } else if (type_str == "INT64") {
        type = FieldDefinition::INT64;
      } else if (type_str == "FIXED_WIDTH_BYTES") {
        type = FieldDefinition::FIXED_WIDTH_BYTES;
      } else if (type_str == "VARIABLE_WIDTH_BYTES") {
        type = FieldDefinition::VARIABLE_WIDTH_BYTES;
      } else {
        throw std::invalid_argument("Invalid type");
      }
      field_definitions.emplace_back(name, type, size);
    }

    return StreamSchema(field_definitions);
}
}
