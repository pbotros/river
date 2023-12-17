#include "gtest/gtest.h"
#include <boost/filesystem.hpp>

#include "ingester_settings.h"

using namespace std;
using namespace river;

class IngesterSettingsTest : public ::testing::Test {
protected:
    void SetUp() override {
        tmp_directory = (boost::filesystem::temp_directory_path() / boost::filesystem::unique_path())
                .make_preferred().string();
        boost::filesystem::create_directory(tmp_directory);

    }

    void TearDown() override {
        if (!tmp_directory.empty()) {
            boost::filesystem::remove_all(tmp_directory);
        }
    }

    std::vector<river::FieldDefinition> fields(std::initializer_list<std::string> field_names) {
        std::vector<river::FieldDefinition> ret;
        for (const auto& field_name : field_names) {
            ret.emplace_back(field_name, river::FieldDefinition::Type::INT32, 4);
        }
        return ret;
    }

    string stream_name;
    string tmp_directory;
    string stream_directory;
    std::optional<std::vector<std::regex>> settings_columns_whitelist;
    std::optional<std::vector<std::regex>> settings_columns_blacklist;
    bool terminated;
    unique_ptr<internal::Redis> redis;
};

TEST_F(IngesterSettingsTest, TestSimple) {
    std::string stream_settings_src = R"(
{
    "stream_settings": [
        {
            "stream_name_regex": "some-prefix-.*",
            "columns_blacklist": [
                "channel_.*"
            ]
        },
        {
            "stream_name_regex": "some-prefix-2-.*",
            "columns_whitelist": [
                "whitelisted_channels_.*"
            ],
            "bytes_per_row_group": 1048576,
            "minimum_age_seconds_before_deletion": 30
        },
        {
            "stream_name_regex": ".*"
        }
    ],
    "global_settings": {
        "bytes_per_row_group": 134217728,
        "minimum_age_seconds_before_deletion": 60
    }
}
)";
    auto settings_filename = (boost::filesystem::path(tmp_directory) / "settings.json").make_preferred().string();
    {
        std::ofstream out(settings_filename);
        out << stream_settings_src;
        out.close();
    }
    auto parsed = ParseStreamSettingsJson(settings_filename);
    ASSERT_EQ(parsed.size(), 3);

    std::regex stream_name_regex = parsed[0].first;
    auto settings = parsed[0].second;
    ASSERT_TRUE(std::regex_match("some-prefix-foobar", stream_name_regex));
    ASSERT_FALSE(std::regex_match("some-not-matching-prefix", stream_name_regex));
    ASSERT_FALSE(settings.columns_whitelist.has_value());
    ASSERT_TRUE(settings.columns_blacklist.has_value());
    ASSERT_EQ(settings.columns_blacklist.value().size(), 1);
    ASSERT_EQ(settings.Filter(fields({"channel_011", "chanzzz_011"})).size(), 1);
    ASSERT_EQ(settings.Filter(fields({"not_matching_011"})).size(), 1);
    ASSERT_EQ(settings.bytes_per_row_group, 134217728);
    ASSERT_EQ(settings.minimum_age_seconds_before_deletion, 60);

    settings = parsed[1].second;
    ASSERT_FALSE(settings.columns_blacklist.has_value());
    ASSERT_TRUE(settings.columns_whitelist.has_value());
    ASSERT_EQ(settings.columns_whitelist.value().size(), 1);
    ASSERT_EQ(settings.Filter(fields({"whitelisted_channels_"})).size(), 1);
    ASSERT_EQ(settings.Filter(fields({"not_channels_"})).size(), 0);
    ASSERT_EQ(settings.bytes_per_row_group, 1048576);
    ASSERT_EQ(settings.minimum_age_seconds_before_deletion, 30);

    settings = parsed[2].second;
    ASSERT_FALSE(settings.columns_blacklist.has_value());
    ASSERT_FALSE(settings.columns_whitelist.has_value());
    ASSERT_EQ(settings.Filter(fields({"anything"})).size(), 1);
    ASSERT_EQ(settings.Filter(fields({"whatever"})).size(), 1);
    ASSERT_EQ(settings.bytes_per_row_group, 134217728);
    ASSERT_EQ(settings.minimum_age_seconds_before_deletion, 60);
}

TEST_F(IngesterSettingsTest, TestDefault) {
    auto parsed = DefaultStreamSettings();
    ASSERT_EQ(parsed.size(), 1);

    std::regex stream_name_regex = parsed[0].first;
    auto settings = parsed[0].second;
    ASSERT_TRUE(std::regex_match("anything", stream_name_regex));
    ASSERT_TRUE(std::regex_match("definitely_anything", stream_name_regex));
    ASSERT_FALSE(settings.columns_whitelist.has_value());
    ASSERT_FALSE(settings.columns_blacklist.has_value());
    ASSERT_EQ(settings.Filter(fields({"channel_011", "chanzzz_011"})).size(), 2);
    ASSERT_EQ(settings.Filter(fields({"not_matching_011"})).size(), 1);
    ASSERT_GT(settings.bytes_per_row_group, 0);
    ASSERT_GT(settings.minimum_age_seconds_before_deletion, 0);
}
