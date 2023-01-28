//
// Created by Paul Botros on 11/15/19.
//

#ifndef PARENT_INGESTER_SETTINGS_H
#define PARENT_INGESTER_SETTINGS_H


#include <vector>
#include <optional>
#include <regex>
#include <algorithm>
#include <river.h>
#include <nlohmann/json.hpp>
#include <glog/logging.h>

using json = nlohmann::json;

static const int64_t DEFAULT_BYTES_PER_ROW_GROUP = 134217728LL; // 128MB
static const int DEFAULT_MINIMUM_AGE_SECONDS_BEFORE_DELETION = 60; // 60s lookback

class StreamIngestionSettings {
public:
    StreamIngestionSettings() {}

    StreamIngestionSettings(
            std::optional<std::vector<std::regex>> columns_blacklist_,
            std::optional<std::vector<std::regex>> columns_whitelist_,
            int64_t bytes_per_row_group_,
            int minimum_age_seconds_before_deletion_) {
        // Whitelist takes priority over blacklist; see filter below.
        columns_blacklist = columns_blacklist_;
        columns_whitelist = columns_whitelist_;
        bytes_per_row_group = bytes_per_row_group_;
        minimum_age_seconds_before_deletion = minimum_age_seconds_before_deletion_;
    }

    std::vector<river::FieldDefinition> Filter(const std::vector<river::FieldDefinition>& fields) const {
        if (columns_whitelist.has_value()) {
            return FilterList(fields, columns_whitelist.value(), false);
        } else if (columns_blacklist.has_value()) {
            return FilterList(fields, columns_blacklist.value(), true);
        } else {
            std::vector<river::FieldDefinition> ret(fields.begin(), fields.end());
            return ret;
        }
    }

    std::optional<std::vector<std::regex>> columns_blacklist;
    std::optional<std::vector<std::regex>> columns_whitelist;
    int64_t bytes_per_row_group;
    int minimum_age_seconds_before_deletion;


private:
    static std::vector<river::FieldDefinition> FilterList(
            const std::vector<river::FieldDefinition>& fields,
            const std::vector<std::regex>& list,
            bool default_value) {
        std::vector<river::FieldDefinition> ret;
        for (const auto& field : fields) {
            bool should_add = default_value;
            for (const auto &column_whitelist: list) {
                if (std::regex_match(field.name, column_whitelist)) {
                    should_add = !should_add;
                    break;
                }
            }
            if (should_add) {
                ret.push_back(field);
            }
        }
        return ret;
    }
};

static std::vector<std::pair<std::regex, StreamIngestionSettings>> ParseStreamSettingsJson(const json& settings_json) {
    int64_t bytes_per_row_group_global = DEFAULT_BYTES_PER_ROW_GROUP;
    int minimum_age_seconds_before_deletion_global = DEFAULT_MINIMUM_AGE_SECONDS_BEFORE_DELETION;
    if (settings_json.contains("global_settings")) {
        minimum_age_seconds_before_deletion_global = settings_json.value(
                "minimum_age_seconds_before_deletion",
                minimum_age_seconds_before_deletion_global);
        bytes_per_row_group_global = settings_json.value("bytes_per_row_group", bytes_per_row_group_global);
    }

    std::vector<std::pair<std::regex, StreamIngestionSettings>> ret;
    if (!settings_json.contains("stream_settings")) {
        LOG(WARNING) << "Warning: stream settings was empty. Was that intentional to not consume any streams?"
                     << std::endl;
        return ret;
    }

    for (const json& setting_json : settings_json["stream_settings"]) {
        std::regex stream_name_regex = setting_json["stream_name_regex"];
        StreamIngestionSettings settings;
        if (setting_json.contains("columns_blacklist")) {
            auto columns_blacklist_str = setting_json["columns_blacklist"].get<std::vector<std::string>>();

            std::vector<std::regex> blacklist(columns_blacklist_str.size());
            std::transform(
                    columns_blacklist_str.cbegin(),
                    columns_blacklist_str.cend(),
                    blacklist.begin(),
                    [](const std::string& s) {
                        return std::regex(s);
                    });
            settings.columns_blacklist = blacklist;
        }

        if (setting_json.contains("columns_whitelist")) {
            auto columns_whitelist_str = setting_json["columns_whitelist"].get<std::vector<std::string>>();
            std::vector<std::regex> whitelist(columns_whitelist_str.size());
            std::transform(
                    columns_whitelist_str.cbegin(),
                    columns_whitelist_str.cend(),
                    whitelist.begin(),
                    [](const std::string& s) {
                        return std::regex(s);
                    });
            settings.columns_whitelist = whitelist;
        }
        settings.bytes_per_row_group = setting_json.value(
                "bytes_per_row_group", bytes_per_row_group_global);
        settings.minimum_age_seconds_before_deletion = setting_json.value(
                "minimum_age_seconds_before_deletion", minimum_age_seconds_before_deletion_global);
        ret.emplace_back(stream_name_regex, settings);
    }

    return ret;
}

static std::vector<std::pair<std::regex, StreamIngestionSettings>> ParseStreamSettingsJson(const std::string& path) {
    std::ifstream ifs(path);
    json settings_json = json::parse(ifs);
    return ParseStreamSettingsJson(settings_json);
}

static std::vector<std::pair<std::regex, StreamIngestionSettings>> DefaultStreamSettings() {
    json catchall_stream_setting;
    catchall_stream_setting["stream_name_regex"] = ".*";
    json j;
    j["stream_settings"] = json::array({catchall_stream_setting});
    return ParseStreamSettingsJson(j);
}

#endif //PARENT_INGESTER_SETTINGS_H
