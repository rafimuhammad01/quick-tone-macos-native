// T7b: of the graph classes only NuxFFT is on the NME-300 path (IR import normalisation,
// R7cd step 6). See app/src/ui/widgets/FFT.h.
#include "ui/widgets/FFT.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace
{
std::vector<float> sine (int length, int fftSize, double bin, double phase = 0.0)
{
    std::vector<float> samples (static_cast<size_t> (length));
    for (int n = 0; n < length; ++n)
        samples[static_cast<size_t> (n)] = static_cast<float> (std::sin (2.0 * std::numbers::pi * bin * n / fftSize + phase));
    return samples;
}

int peakBin (const std::vector<std::complex<float>>& bins)
{
    int best = 0;
    for (int k = 1; k < static_cast<int> (bins.size()) / 2; ++k)
        if (std::norm (bins[static_cast<size_t> (k)]) > std::norm (bins[static_cast<size_t> (best)]))
            best = k;
    return best;
}
} // namespace

TEST_CASE ("fft_of_sine_peaks_at_bin")
{
    qtn::FFT fft (1024);
    const auto bins = fft.transform (sine (1024, 1024, 37.0));

    REQUIRE (bins.size() == 1024);
    CHECK (peakBin (bins) == 37);
    // A full-scale sine of N samples has |X[k]| = N / 2.
    CHECK_THAT (std::abs (bins[37]), Catch::Matchers::WithinRel (512.0f, 1e-3f));
    CHECK (std::abs (bins[36]) < 1e-2f);
}

TEST_CASE ("fft_matches_direct_dft")
{
    // X[k] = sum_n x[n] e^{-2 pi i k n / N}, computed directly.
    const int n = 16;
    std::vector<float> x (n);
    for (int i = 0; i < n; ++i)
        x[static_cast<size_t> (i)] = static_cast<float> ((i * 7) % 5) - 2.0f;

    qtn::FFT fft (n);
    const auto bins = fft.transform (x);

    for (int k = 0; k < n; ++k)
    {
        std::complex<double> expected{};
        for (int i = 0; i < n; ++i)
            expected += static_cast<double> (x[static_cast<size_t> (i)]) * std::polar (1.0, -2.0 * std::numbers::pi * k * i / n);
        INFO ("bin " << k);
        CHECK_THAT (bins[static_cast<size_t> (k)].real(), Catch::Matchers::WithinAbs (expected.real(), 1e-4));
        CHECK_THAT (bins[static_cast<size_t> (k)].imag(), Catch::Matchers::WithinAbs (expected.imag(), 1e-4));
    }
}

TEST_CASE ("fft_zero_pads_short_input")
{
    // NuxFFT is built with size 0x4000 and fed 0x800 IR samples (R7cd step 6).
    qtn::FFT fft (64);
    const std::vector<float> impulse{1.0f};
    const auto bins = fft.transform (impulse);

    REQUIRE (bins.size() == 64);
    for (const auto& bin : bins)
        CHECK_THAT (std::abs (bin), Catch::Matchers::WithinAbs (1.0f, 1e-6f));
}

TEST_CASE ("fft_ignores_input_beyond_size")
{
    qtn::FFT fft (8);
    std::vector<float> x (12, 0.0f);
    x[0] = 1.0f;
    x[10] = 5.0f; // past the end: dropped
    const auto bins = fft.transform (x);
    CHECK_THAT (std::abs (bins[3]), Catch::Matchers::WithinAbs (1.0f, 1e-6f));
}

TEST_CASE ("fft_size_must_be_power_of_two")
{
    CHECK (qtn::FFT::isValidSize (2));
    CHECK (qtn::FFT::isValidSize (16384));
    CHECK_FALSE (qtn::FFT::isValidSize (0));
    CHECK_FALSE (qtn::FFT::isValidSize (1));
    CHECK_FALSE (qtn::FFT::isValidSize (1000));
}
