//
// Created by Paul Botros on 8/11/23.
//

#ifndef RIVER_SRC_COMPRESSION_COMPRESSOR_TYPES_H_
#define RIVER_SRC_COMPRESSION_COMPRESSOR_TYPES_H_

#include <utility>
#include <string>
#include <cstdlib>
#include <vector>
#include <unordered_map>

namespace river {

/**
 * Encapsulates various types of compression that can be done to data within a stream. Each compression type can take
 * different parameters specific to it to customize the compression.
 *
 * All compression is done transparently, such that the writers and the readers of data don't have to deal with
 * compressed data themselves.
 */
class StreamCompression {
public:
    enum class Type {
        UNCOMPRESSED = 0,
        ZFP_LOSSLESS = 1,
        ZFP_LOSSY = 2,
    };

    explicit StreamCompression() : type_(Type::UNCOMPRESSED) {}

    explicit StreamCompression(Type type, std::initializer_list<std::pair<const std::string, std::string>> params = {}) :
        type_(type), params_(params) {}

    explicit StreamCompression(Type type, const std::unordered_map<std::string, std::string>& params) :
        type_(type), params_(params) {}

    Type type() const {
        return type_;
    }

    std::string name() const {
        switch (type_) {
            case Type::UNCOMPRESSED: return "UNCOMPRESSED";
            case Type::ZFP_LOSSLESS: return "ZFP_LOSSLESS";
            case Type::ZFP_LOSSY: return "ZFP_LOSSY";
        }
        throw std::invalid_argument("Unhandled type");
    }

    std::unordered_map<std::string, std::string> params() const {
        return params_;
    }

    static StreamCompression Create(
        const std::string &name,
        const std::unordered_map<std::string, std::string>& params) {
        if (name == "UNCOMPRESSED") {
            return StreamCompression(Type::UNCOMPRESSED, params);
        } else if (name == "ZFP_LOSSLESS") {
            return StreamCompression(Type::ZFP_LOSSLESS, params);
        } else if (name == "ZFP_LOSSY") {
            return StreamCompression(Type::ZFP_LOSSY, params);
        } else {
            throw std::invalid_argument("Unhandled type");
        }
    }
private:
    StreamCompression::Type type_;
    std::unordered_map<std::string, std::string> params_;
};

/**
 * Interface for a class that compresses data.
 */
class Compressor {
public:
    /**
     * Compresses input data, returning a new std::vector of compressed bytes.
     */
    virtual std::vector<char> compress(const char *data, size_t length) = 0;
    virtual ~Compressor() = default;
};

/**
 * Interface for decompressing data.
 */
class Decompressor {
public:
    /**
     * Decompresses input compressed data, returning a new std::vector of decompressed bytes.
     */
    virtual std::vector<char> decompress(const char *data, size_t length) = 0;
    virtual ~Decompressor() = default;
};

}

#endif //RIVER_SRC_COMPRESSION_COMPRESSOR_TYPES_H_
