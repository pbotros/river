//
// Created by Paul Botros on 8/10/23.
//

#ifndef RIVER_SRC_REDIS_WRITER_COMMANDS_H_
#define RIVER_SRC_REDIS_WRITER_COMMANDS_H_

#include <string>
#include <vector>

namespace river {

/**
 * A class tailored towards performant writing of data in StreamWriter.
 *
 * Typically, there's a "formatting" step when using Redis. When calling a command, you pass in all of the arguments
 * to that command, and the hiredis library will format those arguments into one long string, and then send that
 * over the socket. However, when the command is large -- in our cases, consisting of large amounts of binary data --
 * this introduces a copy of the data during the formatting step. It so happens that binary strings (or "bulk strings")
 * are passed over the wire unchanged, so this formatting-specific copy is technically unnecessary.
 *
 * This class encapsulates a preformatted redis command and just switches out where we know the big "binary bulk string"
 * corresponding to our River data to be. In the implementation, we're careful to always put the binary bulk data at
 * the end of the string. We "switch" out where the bulk string is by simply keeping track of string pointers instead of
 * actually copying the data.
 *
 * This should then be used in conjunction with river::Redis::SendCommandPreformatted.
 */
class RedisWriterCommand {
public:
    RedisWriterCommand(const std::string &formatted_command);
    RedisWriterCommand(const RedisWriterCommand &other);

    std::vector<std::pair<const char *, size_t>> ReplaceLastBulkStringAndAssemble(
        const char *data, size_t data_length);

private:
    std::string formatted_command_prefix_;
    std::string formatted_total_size_bytes_;
    std::vector<const char *> command_parts_;
    std::vector<size_t> command_parts_lens_;
};

}

#endif //RIVER_SRC_REDIS_WRITER_COMMANDS_H_
