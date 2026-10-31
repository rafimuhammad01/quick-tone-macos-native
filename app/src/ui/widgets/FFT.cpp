#include "ui/widgets/FFT.h"

#include <juce_core/juce_core.h>

#include <numbers>

namespace qtn
{
FFT::FFT (int size)
{
    jassert (isValidSize (size));
    const auto n = static_cast<size_t> (size);

    twiddles.reserve (n / 2);
    for (size_t k = 0; k < n / 2; ++k)
        twiddles.push_back (std::polar (1.0, -2.0 * std::numbers::pi * static_cast<double> (k) / static_cast<double> (n)));

    int bits = 0;
    while ((1 << bits) < size)
        ++bits;
    bitReverse.resize (n);
    for (int i = 0; i < size; ++i)
    {
        int reversed = 0;
        for (int b = 0; b < bits; ++b)
            if ((i & (1 << b)) != 0)
                reversed |= 1 << (bits - 1 - b);
        bitReverse[static_cast<size_t> (i)] = reversed;
    }
}

std::vector<std::complex<float>> FFT::transform (std::span<const float> samples) const
{
    const auto n = bitReverse.size();

    // Bit-reversed load of the zero-padded / truncated real input, accumulated in double.
    std::vector<std::complex<double>> work (n);
    const auto count = std::min (n, samples.size());
    for (size_t i = 0; i < count; ++i)
        work[static_cast<size_t> (bitReverse[i])] = samples[i];

    // Iterative radix-2 decimation-in-time butterflies.
    for (size_t length = 2; length <= n; length *= 2)
    {
        const auto half = length / 2;
        const auto stride = n / length;
        for (size_t start = 0; start < n; start += length)
        {
            for (size_t j = 0; j < half; ++j)
            {
                const auto t = twiddles[j * stride] * work[start + j + half];
                const auto u = work[start + j];
                work[start + j] = u + t;
                work[start + j + half] = u - t;
            }
        }
    }

    std::vector<std::complex<float>> result (n);
    for (size_t i = 0; i < n; ++i)
        result[i] = std::complex<float> (static_cast<float> (work[i].real()), static_cast<float> (work[i].imag()));
    return result;
}
} // namespace qtn
