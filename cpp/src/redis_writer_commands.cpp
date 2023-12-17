//
// Created by Paul Botros on 8/10/23.
//

#include "redis_writer_commands.h"
#include <spdlog/fmt/fmt.h>

namespace river {

RedisWriterCommand::RedisWriterCommand(const std::string &formatted_command) {
    if (formatted_command[0] != '*') {
        throw std::invalid_argument("Expected array type for commands!");
    }
    auto delimiter_pos = formatted_command.find("\r\n", 1);
    auto num_array_elements = std::stoi(formatted_command.substr(1, delimiter_pos - 1));

    // Start right after the *[0-9...]\r\n
    auto formatted_command_pos = delimiter_pos + 2;
    // Skip until the last command
    for (int i = 0; i < num_array_elements - 1; i++) {
        if (formatted_command[formatted_command_pos] != '$') {
            throw std::invalid_argument("Expected only bulk strings for XADD commands.");
        }
        auto delimiter_pos_bulk_string = formatted_command.find("\r\n", formatted_command_pos + 1);
        auto bulk_string_size =
            std::stoi(formatted_command.substr(formatted_command_pos + 1,
                                               delimiter_pos_bulk_string - formatted_command_pos - 1));
        formatted_command_pos = delimiter_pos_bulk_string + 2 + bulk_string_size + 2;
    }

    // Split an entire command into 6 portions, assuming the command is a Redis ARRAY type.
    // <prefix>, $, <data length>, \r\n, <data>, \r\n
    // where the prefix is everything up until the start of the last bulk string in the array command.
    command_parts_.resize(6);
    command_parts_lens_.resize(6);

    formatted_command_prefix_ = formatted_command.substr(0, formatted_command_pos);
    command_parts_[0] = formatted_command_prefix_.c_str();
    command_parts_lens_[0] = formatted_command_prefix_.size();

    command_parts_[1] = "$";
    command_parts_lens_[1] = 1;

    // Omit #2 which will be the length of the last bulk string

    command_parts_[3] = "\r\n";
    command_parts_lens_[3] = 2;

    // Omit #4 which will be the actual data

    command_parts_[5] = "\r\n";
    command_parts_lens_[5] = 2;
}

std::vector<std::pair<const char *, size_t>> RedisWriterCommand::ReplaceLastBulkStringAndAssemble(
    const char *data, size_t data_length) {
    // Make sure we hold on to this formatted string so it remains allocated
    formatted_total_size_bytes_ = fmt::format_int(data_length).str();
    command_parts_[2] = formatted_total_size_bytes_.c_str();
    command_parts_lens_[2] = formatted_total_size_bytes_.size();

    command_parts_[4] = data;
    command_parts_lens_[4] = data_length;

    std::vector<std::pair<const char *, size_t>> ret(6);
    for (int i = 0; i < 6; i++) {
        ret.emplace_back(command_parts_[i], command_parts_lens_[i]);
    }
    return ret;
}
}
