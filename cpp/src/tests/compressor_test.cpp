#include "gtest/gtest.h"
#include "../compression/compressor.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <numeric>

class CompressorTest : public ::testing::Test {
public:
    static std::vector<int16_t> ReadInputSinesInt16() {
        /**
         * To generate this file:
         * n_samples = 64
         * n_chs = 4096
         * phases = np.linspace(0, np.pi, n_chs)
         * freqs = np.linspace(1, 10, n_chs)
         * output = np.sin(freqs * np.arange(n_samples).reshape((-1, 1)) / 10 + phases)
         * output_ds = (output * (2 ** 14)).astype(np.int16)
         * output_ds.tofile('/tmp/foo.dat')
         *
         * It's intentionally a file that's fairly compressible, since there's high correlations in the data.
         */
        std::filesystem::path test_file_path(__FILE__);
        auto test_data = test_file_path.parent_path() / "resources" / "compressor_input_sines.dat";
        std::ifstream in(test_data, std::ios::binary);

        std::vector<char> raw(std::istreambuf_iterator<char>(in), {});

        auto num_elements = raw.size() / sizeof(int16_t);
        return {
            (int16_t *) raw.data(),
            ((int16_t *) raw.data()) + num_elements};
    }

    static std::vector<float> ReadInputSinesFloat() {
        auto ret_int16 = ReadInputSinesInt16();
        std::vector<float> ret(ret_int16.size());
        std::transform(ret_int16.begin(), ret_int16.end(), ret.begin(),
                       [](int16_t element) { return (float) element; });
        return ret;
    }

    static std::vector<double> ReadInputSinesDouble() {
        auto ret_int16 = ReadInputSinesInt16();
        std::vector<double> ret(ret_int16.size());
        std::transform(ret_int16.begin(), ret_int16.end(), ret.begin(),
                       [](int16_t element) { return (double) element; });
        return ret;
    }

    template<typename T>
    double mean(const std::vector<T> &vec) {
        // From https://stackoverflow.com/questions/33268513/calculating-standard-deviation-variance-in-c
        const size_t sz = vec.size();
        if (sz <= 1) {
            return 0.0;
        }

        // Calculate the mean
        return std::accumulate(vec.begin(), vec.end(), 0.0) / ((double) sz);
    }

    template<typename T>
    double demeaned_norm2(const std::vector<T> &vec) {
        // From https://stackoverflow.com/questions/33268513/calculating-standard-deviation-variance-in-c
        const size_t sz = vec.size();
        if (sz <= 1) {
            return 0.0;
        }

        // Calculate the mean
        const double vec_mean = mean(vec);

        // Now calculate the variance
        auto variance_func = [&vec_mean](double accumulator, const T &val_raw) {
            auto val = (double) val_raw;
            return accumulator + ((val - vec_mean) * (val - vec_mean));
        };

        return std::accumulate(vec.begin(), vec.end(), 0.0, variance_func);
    }

    template <class T>
    std::tuple<std::vector<T>, std::vector<T>, size_t> RoundTripConvert(
        river::Compressor *compressor, river::Decompressor *decompressor) {
        std::vector<T> buffer;
        if constexpr (std::is_same_v<T, int16_t>) {
            buffer = ReadInputSinesInt16();
        } else if constexpr (std::is_same_v<T, float>) {
            buffer = ReadInputSinesFloat();
        } else if constexpr (std::is_same_v<T, double>) {
            buffer = ReadInputSinesDouble();
        } else {
            throw std::invalid_argument("??");
        }

        auto compressed = compressor->compress((char *) buffer.data(), sizeof(T) * buffer.size());

        auto round_tripped_raw = decompressor->decompress(compressed.data(), compressed.size());
        auto num_elements = round_tripped_raw.size() / sizeof(T);
        return {
            buffer,
            {(T *) round_tripped_raw.data(), ((T *) round_tripped_raw.data()) + num_elements},
            compressed.size()};
    }

    template <class T>
    void AssertRoundTripCorrectness(river::Compressor *compressor, river::Decompressor *decompressor) {
        auto [buffer, round_tripped, compressed_size_bytes] = RoundTripConvert<T>(compressor, decompressor);

        // It compressed at least somewhat (< 90%)
        ASSERT_LE(compressed_size_bytes, (size_t) (0.9 * buffer.size() * sizeof(T)));
        ASSERT_EQ(buffer.size(), round_tripped.size());

        for (size_t i = 0; i < buffer.size(); i++) {
            ASSERT_EQ(buffer[i], round_tripped[i]);
        }
    }
};

TEST_F(CompressorTest, TestZfpLossless_Int16) {
    river::ZfpCompressor<int16_t> compressor(4096, -1);
    river::ZfpDecompressor<int16_t> decompressor;
    AssertRoundTripCorrectness<int16_t>(&compressor, &decompressor);
}

TEST_F(CompressorTest, TestZfpLossless_Float) {
    river::ZfpCompressor<float> compressor(4096, -1);
    river::ZfpDecompressor<float> decompressor;
    AssertRoundTripCorrectness<float>(&compressor, &decompressor);
}

TEST_F(CompressorTest, TestZfpLossless_Double) {
    river::ZfpCompressor<double> compressor(4096, -1);
    river::ZfpDecompressor<double> decompressor;
    AssertRoundTripCorrectness<double>(&compressor, &decompressor);
}

TEST_F(CompressorTest, TestZfpLossy_Float) {
    river::ZfpCompressor<float> compressor(4096, 20);
    river::ZfpDecompressor<float> decompressor;
    auto [buffer, round_tripped, compressed_size_bytes] = RoundTripConvert<float>(&compressor, &decompressor);

    // It compressed at least somewhat (< 90%)
    ASSERT_LE(compressed_size_bytes, (size_t) (0.90 * buffer.size() * sizeof(float)));

    // Compute the correlation between the two; should be high:
    double running_corr = 0.0;

    double buffer_mean = mean(buffer);
    double round_tripped_mean = mean(round_tripped);
    for (size_t i = 0; i < buffer.size(); i++) {
        auto buffer_val = (double) buffer[i];
        auto round_tripped_val = (double) round_tripped[i];
        running_corr += ((buffer_val - buffer_mean) * (round_tripped_val - round_tripped_mean));
    }
    double corr = running_corr / sqrt(demeaned_norm2(buffer)) / sqrt(demeaned_norm2(round_tripped));
    ASSERT_GE(corr, 0.9);
}
