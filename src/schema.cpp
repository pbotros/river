//
// Created by Paul Botros on 10/29/19.
//

#include "schema.h"
#include <fmt/format.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/foreach.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/bimap.hpp>

namespace river {
namespace internal {
    typedef boost::bimap<FieldDefinition::Type, string> type_bimap;

    static const type_bimap mapping() {
        type_bimap map;
        map.insert(type_bimap::value_type(FieldDefinition::DOUBLE, "DOUBLE"));
        map.insert(type_bimap::value_type(FieldDefinition::FLOAT, "FLOAT"));
        map.insert(type_bimap::value_type(FieldDefinition::INT32, "INT32"));
        map.insert(type_bimap::value_type(FieldDefinition::INT64, "INT64"));
        map.insert(type_bimap::value_type(FieldDefinition::FIXED_WIDTH_BYTES, "FIXED_WIDTH_BYTES"));
        map.insert(type_bimap::value_type(FieldDefinition::VARIABLE_WIDTH_BYTES, "VARIABLE_WIDTH_BYTES"));
        return map;
    }

    string serialize_schema(const StreamSchema &schema) {
        type_bimap type_map = mapping();

        boost::property_tree::ptree parent;
        boost::property_tree::ptree holder;
        for (auto &it : schema.field_definitions) {
            boost::property_tree::ptree field;
            field.put("name", it.name);
            field.put("size", it.size);
            field.put("type", type_map.left.at(it.type));
            holder.push_back(std::make_pair("", field));
        }
        parent.add_child("field_definitions", holder);

        std::stringstream ss;
        boost::property_tree::json_parser::write_json(ss, parent);
        return ss.str();
    }

    StreamSchema deserialize_schema(const string& json) {
        stringstream ss;
        ss << json;

        boost::property_tree::ptree pt;
        boost::property_tree::json_parser::read_json(ss, pt);

        type_bimap type_map = mapping();

        vector<FieldDefinition> field_definitions;
        BOOST_FOREACH(boost::property_tree::ptree::value_type &field, pt.get_child("field_definitions")) {
            auto name = field.second.get<string>("name");
            auto size = field.second.get<int>("size");
            auto type_int = field.second.get<string>("type");
            field_definitions.push_back(FieldDefinition(name, type_map.right.at(type_int), size));
        }

        return StreamSchema(field_definitions);
    }
}
}
