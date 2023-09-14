//
// Created by Paul Botros on 8/11/23.
//

#include "compressor.h"
#include <zfp.hpp>
#include <sstream>
#include <iostream>
#include <glog/logging.h>
#include <cassert>


namespace river {

class ZfpCompressorImpl {
public:
    int num_rows_;
    int num_cols_;
    std::vector<char> buffer_;
    std::unique_ptr<std::vector<int32_t>> data_promoted;
    zfp_stream *zfp_;
    zfp_field  *field_;

    ~ZfpCompressorImpl() {
        if (zfp_) {
            if (zfp_->stream) {
                stream_close(zfp_->stream);
            }
            zfp_stream_close(zfp_);
        }
        if (field_) {
            zfp_field_free(field_);
        }
    }
};

template <class DataTypeT>
constexpr zfp_type zfp_type_for_class() {
    if constexpr (std::is_same_v<DataTypeT, int16_t> || std::is_same_v<DataTypeT, int32_t>) {
        return zfp_type_int32;
    } else if constexpr (std::is_same_v<DataTypeT, float>) {
        return zfp_type_float;
    } else if constexpr (std::is_same_v<DataTypeT, double>) {
        return zfp_type_double;
    } else {
        throw std::invalid_argument("Unhandled data type for conversion to ZFP");
    }
}

template <class DataTypeT>
ZfpCompressor<DataTypeT>::ZfpCompressor(int num_cols, double tolerance, bool use_openmp)
    : impl_(nullptr) {
    this->num_cols_ = num_cols;
    this->tolerance_ = tolerance;
    this->use_openmp_ = use_openmp;
}

template <class DataTypeT>
ZfpCompressor<DataTypeT>::~ZfpCompressor() noexcept {
    if (impl_ != nullptr) {
        delete impl_;
        impl_ = nullptr;
    }
}

template <class DataTypeT>
std::vector<char> ZfpCompressor<DataTypeT>::compress(const char *data, size_t length) {
    assert(length % (sizeof(DataTypeT) * num_cols_) == 0);
    auto num_rows = (int) (length / sizeof(DataTypeT) / num_cols_);

    if (impl_ == nullptr || impl_->num_rows_ != num_rows) {
        // We lazily instantiate the internal data structure within this compressor to minimize the number of
        // initializations we do. This paradigm leverages the fact that most StreamWriter's will likely call Write()
        // with the same number of rows of data every time.

        // Clean up past impl_ struct (delete nullptr is okay)
        delete impl_;

        // initialize metadata for a compressed stream
        impl_ = new ZfpCompressorImpl();
        impl_->num_rows_ = num_rows;
        impl_->num_cols_ = this->num_cols_;
        impl_->zfp_ = zfp_stream_open(nullptr);

        impl_->field_ = zfp_field_2d(
            nullptr,
            zfp_type_for_class<DataTypeT>(),
            impl_->num_cols_,
            impl_->num_rows_);

        size_t bufsize = zfp_stream_maximum_size(impl_->zfp_, impl_->field_);
        impl_->buffer_.resize(bufsize);

        // initialize metadata for a compressed stream
        if (tolerance_ < 0.0) {
            zfp_stream_set_reversible(impl_->zfp_);
        } else {
            zfp_stream_set_accuracy(impl_->zfp_, tolerance_);
        }
        if (use_openmp_) {
            zfp_stream_set_execution(impl_->zfp_, zfp_exec_omp);
        } else {
            zfp_stream_set_execution(impl_->zfp_, zfp_exec_serial);
        }

        // associate bit stream with allocated buffer
        bitstream *stream = stream_open(impl_->buffer_.data(), impl_->buffer_.size());
        zfp_stream_set_bit_stream(impl_->zfp_, stream);

        if constexpr (std::is_same_v<DataTypeT, int16_t>) {
            impl_->data_promoted = std::make_unique<std::vector<int32_t>>(num_rows * num_cols_);
        }
    }

    // Needs promotion explicitly since ZFP only supports int32's. We copy the logic from the zfp_promote* functions
    // here.
    if constexpr (std::is_same_v<DataTypeT, int16_t>) {
        auto data_int16 = (int16_t *) data;
        for (size_t i = 0; i < impl_->data_promoted->size(); i++) {
            // Promote it properly
            impl_->data_promoted->at(i) = ((int32_t) data_int16[i]) << 15;
        }
        zfp_field_set_pointer(impl_->field_, (void *) impl_->data_promoted->data());
    } else {
        zfp_field_set_pointer(impl_->field_, (void *) data);
    }
    zfp_stream_rewind(impl_->zfp_);

    assert(length == impl_->field_->nx * impl_->field_->ny * sizeof(DataTypeT));

    // compress array
    size_t zfpheadersize = zfp_write_header(impl_->zfp_, impl_->field_, ZFP_HEADER_FULL);
    size_t zfpsize = zfp_compress(impl_->zfp_, impl_->field_);                // return value is byte size of compressed stream

    const char *ret_start = impl_->buffer_.data();
    return {ret_start, ret_start + zfpheadersize + zfpsize};
}

template <class DataTypeT>
std::vector<char> ZfpDecompressor<DataTypeT>::decompress(const char *data, size_t length) {
    // associate bit stream with allocated buffer
    bitstream *stream = stream_open((void *)data, length);         // bit stream to compress to
    zfp_stream *zfp = zfp_stream_open(stream);                  // compressed stream and parameters
    zfp_stream_rewind(zfp);                                   // rewind stream to beginning

    zfp_field* field = zfp_field_alloc();
    size_t zfpheadersize = zfp_read_header(zfp, field, ZFP_HEADER_FULL);

    assert(zfpheadersize > 0);
    assert(field->type == zfp_type_for_class<DataTypeT>());
    assert(field->data == nullptr);
    assert(field->nx > 0);
    assert(field->ny > 0);
    assert(field->nz == 0);
    assert(field->nw == 0);
    auto num_elements = field->nx * field->ny;

    if constexpr (std::is_same_v<DataTypeT, int16_t>) {
        std::vector<char> decompressed_data(sizeof(int32_t) * num_elements);
        zfp_field_set_pointer(field, decompressed_data.data());
        zfp_decompress(zfp, field);

        auto *decompressed_int32 = (int32_t *) field->data;
        std::vector<char> ret(sizeof(int16_t) * num_elements);
        auto *decompressed_int16 = (int16_t *) ret.data();
        for (size_t i = 0; i < num_elements; i++) {
            // Copies logic in zfp's zfp_demote* int16s.
            int32_t val = (decompressed_int32[i] >> 15);
            decompressed_int16[i] = (int16_t) ((std::max<int32_t>)(-0x8000, (std::min<int32_t>)(val, 0x7fff)));
        }

        zfp_field_free(field);
        return ret;
    } else {
        std::vector<char> decompressed_data(sizeof(DataTypeT) * num_elements);
        zfp_field_set_pointer(field, decompressed_data.data());
        zfp_decompress(zfp, field);
        zfp_field_free(field);
        return decompressed_data;
    }
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
