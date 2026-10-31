#pragma once

#include <complex>
#include <span>
#include <vector>

namespace qtn
{
/** Forward FFT of real samples, X[k] = sum_n x[n] e^{-2 pi i k n / N}, the role of NuxFFT
    (decomp:NuxFFT/fft_spectrum@1000602c0.c: real input zero-padded into a complex buffer, iterative
    radix-2 butterflies with a twiddle table, then bit-reversal reordering).

    On the NME-300 path NuxFFT is used only by IR import normalisation (R7cd step 6: size 0x4000,
    2048 input samples). Input longer than the size is truncated, shorter input zero-padded.

    The size is fixed at construction; twiddles and the bit-reverse table are precomputed, so
    transform() allocates only its result.
*/
class FFT
{
  public:
    [[nodiscard]] static constexpr bool isValidSize (int n) noexcept { return n >= 2 && (n & (n - 1)) == 0; }

    /** size must satisfy isValidSize (asserted). */
    explicit FFT (int size);

    [[nodiscard]] int size() const noexcept { return static_cast<int> (bitReverse.size()); }

    [[nodiscard]] std::vector<std::complex<float>> transform (std::span<const float> samples) const;

  private:
    std::vector<std::complex<double>> twiddles; // e^{-2 pi i k / N}, k < N/2
    std::vector<int> bitReverse;
};
} // namespace qtn
