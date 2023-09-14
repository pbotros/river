//
// Created by Paul Botros on 8/11/23.
//

#include "compressor.h"
#include <cassert>


namespace river {

class ZfpCompressorImpl {
};

template <class DataTypeT>
ZfpCompressor<DataTypeT>::ZfpCompressor(int num_cols, double tolerance, bool use_openmp)
    : impl_(nullptr) {
        throw std::logic_error("ZFP compression is disabled via build flags. Re-build and re-install River with"
                               " the appropriate ZFP build flag enabled.");
}

template <class DataTypeT>
ZfpCompressor<DataTypeT>::~ZfpCompressor() noexcept {
}

template <class DataTypeT>
std::vector<char> ZfpCompressor<DataTypeT>::compress(const char *data, size_t length) {
    throw std::logic_error("ZFP compression is disabled via build flags. Re-build and re-install River with"
                           " the appropriate ZFP build flag enabled.");
}

template <class DataTypeT>
std::vector<char> ZfpDecompressor<DataTypeT>::decompress(const char *data, size_t length) {
    throw std::logic_error("ZFP compression is disabled via build flags. Re-build and re-install River with"
                           " the appropriate ZFP build flag enabled.");
}

// And then handle the linker for templated classes by explicitly declaring possible types supported:
template class ZfpCompressor<int16_t>;
template class ZfpCompressor<int32_t>;
template class ZfpCompressor<float>;
template class ZfpCompressor<double>;
template class ZfpDecompressor<int16_t>;
template class ZfpDecompressor<int32_t>;
template class ZfpDecompressor<float>;
template class ZfpDecompressor<double>;
}
