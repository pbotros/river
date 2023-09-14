//
// Created by Paul Botros on 8/11/23.
//

#include "compressor.h"
#include <sstream>
#include <cassert>


namespace river {

static std::string GetOrThrow(
    const std::unordered_map<std::string, std::string> &params,
    const std::string &key_name,
    const std::string &compression_type_name) {
    auto it = params.find(key_name);
    if (it == params.end()) {
        std::stringstream ss;
        ss << "Expected " << key_name << " for " << compression_type_name << " compression";
        throw std::invalid_argument(ss.str());
    }
    return it->second;
}

std::unique_ptr<Decompressor> CreateDecompressor(const StreamCompression &compression) {
    switch (compression.type()) {
        case StreamCompression::Type::UNCOMPRESSED: return {};
        case StreamCompression::Type::ZFP_LOSSLESS:
        case StreamCompression::Type::ZFP_LOSSY: {
            auto params = compression.params();
            auto data_type = GetOrThrow(params, "data_type", compression.name());
            if (data_type == "int16") {
                return std::make_unique<ZfpDecompressor<int16_t>>();
            } else if (data_type == "int32") {
                return std::make_unique<ZfpDecompressor<int32_t>>();
            } else if (data_type == "float") {
                return std::make_unique<ZfpDecompressor<float>>();
            } else if (data_type == "double") {
                return std::make_unique<ZfpDecompressor<double>>();
            } else {
                throw std::invalid_argument("Unhandled compression data type");
            }
        };
            break;
        case StreamCompression::Type::DUMMY: return std::make_unique<DummyCompressor>();
            break;
    }
    throw std::invalid_argument("Unhandled decompressor type!");
}

std::unique_ptr<Compressor> CreateCompressor(const StreamCompression &compression) {
    switch (compression.type()) {
        case StreamCompression::Type::UNCOMPRESSED: return {};
        case StreamCompression::Type::ZFP_LOSSLESS:
        case StreamCompression::Type::ZFP_LOSSY: {
            auto params = compression.params();
            auto num_cols = std::stoi(GetOrThrow(params, "num_cols", compression.name()));
            auto data_type = GetOrThrow(params, "data_type", compression.name());
            double tolerance;
            if (compression.type() == StreamCompression::Type::ZFP_LOSSY) {
                tolerance = std::stod(GetOrThrow(params, "tolerance", compression.name()));
            } else {
                // Lossless
                tolerance = -1;
            }

            auto use_openmp_it = params.find("use_openmp");
            bool use_openmp;
            if (use_openmp_it == params.end()) {
                use_openmp = false;
            } else {
                use_openmp = (use_openmp_it->second == "true");
            }

            if (data_type == "int16") {
                return std::make_unique<ZfpCompressor<int16_t>>(num_cols, tolerance, use_openmp);
            } else if (data_type == "int32") {
                return std::make_unique<ZfpCompressor<int32_t>>(num_cols, tolerance, use_openmp);
            } else if (data_type == "float") {
                return std::make_unique<ZfpCompressor<float>>(num_cols, tolerance, use_openmp);
            } else if (data_type == "double") {
                return std::make_unique<ZfpCompressor<double>>(num_cols, tolerance, use_openmp);
            } else {
                throw std::invalid_argument("Unhandled compression data type");
            }
        };
            break;
        case StreamCompression::Type::DUMMY:return std::make_unique<DummyCompressor>();
            break;
    }
    throw std::invalid_argument("Unhandled compression type!");
}
}
