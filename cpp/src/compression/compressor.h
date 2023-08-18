//
// Created by Paul Botros on 8/11/23.
//

#ifndef RIVER_SRC_COMPRESSION_COMPRESSOR_H_
#define RIVER_SRC_COMPRESSION_COMPRESSOR_H_

#include <utility>
#include <string>
#include <cstdlib>
#include <vector>
#include <memory>
#include "compressor_types.h"

namespace river {

template <class DataTypeT>
class ZfpDecompressor : public Decompressor {
public:
    std::vector<char> decompress(const char *data, size_t length) override;
};

class ZfpCompressorImpl;

template <class DataTypeT>
class ZfpCompressor : public Compressor {
public:
    /**
     * @param num_cols number of columns for each block of data that will be given to the compressor.
     * @param tolerance if <= 0, then reversible mode, aka lossless mode, will be used. Else, this is the allowed
     * absolute tolerance as specified
     */
    ZfpCompressor(int num_cols, double tolerance, bool use_openmp);
    ~ZfpCompressor() noexcept override;
//    std::shared_ptr<char> compress(const char *data, size_t length);
    CompressedObject compress(const char *data, size_t length) override;
//    std::unique_ptr<char> compress(const char *data, size_t length) override;
private:
    ZfpCompressorImpl *impl_;
    int num_cols_;
    double tolerance_;
    bool use_openmp_;
};

class DummyCompressor : public Compressor, public Decompressor {
public:
    ~DummyCompressor() override = default;
    CompressedObject compress(const char *data, size_t length) override {
        return {data, length};
    }
    std::vector<char> decompress(const char *data, size_t length) override {
        return {data, data + length};
    }
};

std::unique_ptr<Decompressor> CreateDecompressor(const StreamCompression &compression);
std::unique_ptr<Compressor> CreateCompressor(const StreamCompression &compression);

}

#endif //RIVER_SRC_COMPRESSION_COMPRESSOR_H_
