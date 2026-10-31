#include "ir/IrConvert.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <numbers>

namespace qtn::IrConvert
{
namespace
{
constexpr int blockSize = 2048; // prepareToPlay(2048, 48000) and 2048-sample blocks (R7cd §2 step 2)
constexpr int intermediateBits = 24;

// NuxFFT at NuxMidiTransmission+0x4d730 is initialised with the 8-byte value 0x80000004000
// (decomp:NuxMidiTransmission/NuxMidiTransmission@10002eed0.c:L264): field +0 = 0x4000 = FFT size,
// field +4 = 0x800 = input length. fft_spectrum (decomp:NuxFFT/fft_spectrum@1000602c0.c) first
// correlates the input with a unit impulse (this+8 = {1, 0, 0...}), which writes
// re[i] = in[2047 - i] for i < 2048 and 0 up to 16384, im = 0, then runs an in-place radix-2
// FFT with a 7-bit split bit-reversal table (+0x18, 128 entries) and twiddles at +0x118.
// UNVERIFIED: that fft_spectrum returns its output buffer (this+0x10) - the caller reads the
// return value; and the twiddle values (constructor not decompiled; |X|^2 does not depend on
// the sign convention). Float rounding differs from the original's butterfly order, so the
// gain may differ in the last bits (R7cd §8 Q3).
constexpr size_t fftSize = 0x4000;
// Caller reads 888 complex values at result + 0x440 + 8k (decomp:NuxCabinetPopupMenu/
// buttonClicked@1002341e0.c:L809-826): bins 0x440 / 8 = 136 .. 1023 (398 Hz - 3 kHz at 48 kHz).
constexpr size_t firstBin = 0x440 / 8;
constexpr size_t binCount = 0x378;
// Start values of the max / min search (L810-811), bit patterns of the doubles.
const double pmaxStart = std::bit_cast<double> (uint64_t{0xc202a05f1ff80000});
const double pminStart = std::bit_cast<double> (uint64_t{0x4202a05f1ff80000});

/** In-place iterative radix-2 FFT (forward, e^{-i...}), single precision like NuxFFT. */
void fft (std::vector<std::complex<float>>& x)
{
    const size_t n = x.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (x[i], x[j]);
    }

    for (size_t len = 2; len <= n; len <<= 1)
    {
        const size_t half = len / 2;
        for (size_t k = 0; k < half; ++k)
        {
            const double angle = -2.0 * std::numbers::pi * static_cast<double> (k) / static_cast<double> (len);
            const std::complex<float> w{static_cast<float> (std::cos (angle)), static_cast<float> (std::sin (angle))};
            for (size_t start = 0; start < n; start += len)
            {
                const auto t = w * x[start + k + half];
                x[start + k + half] = x[start + k] - t;
                x[start + k] += t;
            }
        }
    }
}

/** Resampler + 24-bit WAV round trip of convertAudioToWaveFormat (R7cd §2 step 2): ratio
    srcRate / 48000 (asm@0x1005f6fb1-0x1005f6fe3), 24-bit mono writer (asm@0x1005f6f2f-0x1005f6f96),
    `outLength` = lengthInSamples / ratio. Only the first 2048 output samples are produced. */
std::vector<float> resampleThrough24BitWav (std::span<const float> channel0, double ratio, int64_t outLength)
{
    const auto count = static_cast<int> (std::min<int64_t> (outLength, blockSize));
    if (count <= 0)
        return {};

    // The resampler reads round(2048 * ratio) + 3 input samples for one block; more are never used.
    const auto inputNeeded = std::min (channel0.size(), static_cast<size_t> (std::ceil (blockSize * ratio)) + 64);
    juce::AudioBuffer<float> input (1, static_cast<int> (std::max<size_t> (inputNeeded, 1)));
    input.clear();
    std::copy_n (channel0.begin(), inputNeeded, input.getWritePointer (0));

    juce::MemoryAudioSource source (input, false);
    juce::ResamplingAudioSource resampler (&source, false, 1);
    resampler.setResamplingRatio (ratio);
    resampler.prepareToPlay (blockSize, deviceSampleRate);

    juce::MemoryBlock wav;
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream> (wav, false);
        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate (deviceSampleRate)
                                 .withChannelLayout (juce::AudioChannelSet::mono())
                                 .withBitsPerSample (intermediateBits);
        auto writer = juce::WavAudioFormat{}.createWriterFor (stream, options);
        if (writer == nullptr || ! writer->writeFromAudioSource (resampler, count, blockSize))
            return {};
    } // writer flushes the header here

    std::unique_ptr<juce::AudioFormatReader> reader (juce::WavAudioFormat{}.createReaderFor (new juce::MemoryInputStream (wav, false), true));
    if (reader == nullptr)
        return {};

    juce::AudioBuffer<float> output (1, count);
    output.clear();
    reader->read (&output, 0, count, 0, true, false);
    return {output.getReadPointer (0), output.getReadPointer (0) + count};
}

/** `(int)(sample * 2147483648.0)` (const@0x1006d7b78, ghidra:100032550). Out of range and NaN
    give INT_MIN as x86 cvttsd2si does. UNVERIFIED: that the compiler emitted cvttsd2si. */
int32_t exportSample (float sample) noexcept
{
    const double scaled = static_cast<double> (sample) * 2147483648.0;
    if (! (scaled > -2147483649.0 && scaled < 2147483648.0))
        return std::numeric_limits<int32_t>::min();
    return static_cast<int32_t> (scaled);
}

void putLE (Bytes& out, uint32_t value, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        out.push_back (static_cast<uint8_t> (value >> (8 * i)));
}

void putTag (Bytes& out, const char (&tag)[5]) { out.insert (out.end(), tag, tag + 4); }
} // namespace

std::vector<float> toMono48k (double sampleRate, int numChannels, std::span<const float> interleaved)
{
    if (! (sampleRate > 0.0) || numChannels < 1)
        return {};

    const auto channels = static_cast<size_t> (numChannels);
    const auto frames = interleaved.size() / channels;
    std::vector<float> channel0 (frames);
    for (size_t i = 0; i < frames; ++i)
        channel0[i] = interleaved[i * channels]; // writer has one channel: only the first is read (UNVERIFIED, R7cd §2)

    const double ratio = sampleRate / deviceSampleRate;
    // UNVERIFIED: truncation of lengthInSamples / ratio to an integer sample count.
    const auto outLength = static_cast<int64_t> (static_cast<double> (frames) / ratio);
    return resampleThrough24BitWav (channel0, ratio, outLength);
}

double normalisationGain (std::span<const float, IrData::sampleCount> deviceOrder)
{
    std::vector<std::complex<float>> spectrum (fftSize);
    for (size_t i = 0; i < IrData::sampleCount; ++i)
        spectrum[i] = deviceOrder[IrData::sampleCount - 1 - i];
    fft (spectrum);

    double pmax = pmaxStart;
    double pmin = pminStart;
    for (size_t k = firstBin; k < firstBin + binCount; ++k)
    {
        const float re = spectrum[k].real();
        const float im = spectrum[k].imag();
        const auto p = static_cast<double> (re * re + im * im); // float sum, then widened (L815)
        pmin = std::min (pmin, p);
        pmax = std::max (pmax, p);
    }
    // g = 10^(-(log10 sqrt(pmax) + log10 sqrt(pmin)) / 2) (L827-829)
    return std::pow (10.0, (std::log10 (std::sqrt (pmax)) + std::log10 (std::sqrt (pmin))) * -0.5);
}

std::variant<IrData, IrError> fromMono48k (std::span<const float> mono48k)
{
    // First 2048 samples, zero-padded, reversed: dst[0x7FF - i] = src[i] (L582-702).
    std::array<float, IrData::sampleCount> device{};
    const auto count = std::min (mono48k.size(), IrData::sampleCount);
    for (size_t i = 0; i < count; ++i)
        device[IrData::sampleCount - 1 - i] = mono48k[i];

    const double gain = normalisationGain (device);
    // The original multiplies even when the gain is infinite (silent band); we refuse instead.
    if (! std::isfinite (gain) || ! (gain > 0.0))
        return IrError{"audio has no signal between 400 Hz and 3 kHz; cannot normalise it"};

    for (auto& sample : device)
        sample = static_cast<float> (static_cast<double> (sample) * gain); // L839-854

    auto ir = IrData::fromDeviceSamples (device);
    if (auto reason = ir.invalidReason())
        return IrError{std::move (*reason)};
    return ir;
}

std::variant<IrData, IrError> fromSamples (double sampleRate, int numChannels, std::span<const float> interleaved)
{
    if (! (sampleRate > 0.0) || numChannels < 1)
        return IrError{"invalid sample rate or channel count"};
    return fromMono48k (toMono48k (sampleRate, numChannels, interleaved));
}

std::variant<IrData, IrError> fromAudioFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return IrError{"file not found: " + file.getFullPathName().toStdString()};

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return IrError{"not a readable audio file"};

    // A reader whose format is not the one registered for the extension yields an empty temp file
    // in the original (ghidra:1005f6a00), i.e. an error.
    const auto* expected = formats.findFormatForFileExtension (file.getFileExtension());
    if (expected == nullptr || expected->getFormatName() != reader->getFormatName())
        return IrError{"file content does not match its extension"};

    const double rate = reader->sampleRate;
    const bool copiedUnchanged = reader->getFormatName() == juce::WavAudioFormat{}.getFormatName() && rate == deviceSampleRate && reader->numChannels == 1 && reader->bitsPerSample == static_cast<unsigned int> (intermediateBits); // asm@0x1005f76f0-0x1005f771a

    const double ratio = rate / deviceSampleRate;
    const auto needed = copiedUnchanged ? int64_t{blockSize} : static_cast<int64_t> (std::ceil (blockSize * ratio)) + 64;
    const auto toRead = static_cast<int> (std::min (reader->lengthInSamples, needed));

    juce::AudioBuffer<float> buffer (1, std::max (toRead, 1));
    buffer.clear();
    if (toRead > 0 && ! reader->read (&buffer, 0, toRead, 0, true, false))
        return IrError{"could not read the audio data"};
    const std::span<const float> channel0 (buffer.getReadPointer (0), static_cast<size_t> (toRead));

    if (copiedUnchanged)
        return fromMono48k (channel0);

    const auto outLength = static_cast<int64_t> (static_cast<double> (reader->lengthInSamples) / ratio);
    return fromMono48k (resampleThrough24BitWav (channel0, ratio, outLength));
}

Bytes toWav (const IrData& ir)
{
    // Header written by ghidra:100032550 (R7cd §4a #1).
    Bytes out;
    out.reserve (44 + IrData::byteCount);
    putTag (out, "RIFF");
    putLE (out, 0x2024, 4);
    putTag (out, "WAVE");
    putTag (out, "fmt ");
    putLE (out, 16, 4);
    putLE (out, 1, 2); // PCM
    putLE (out, 1, 2); // mono
    putLE (out, 48000, 4);
    putLE (out, 192000, 4);
    putLE (out, 4, 2);
    putLE (out, 32, 2);
    putTag (out, "data");
    putLE (out, 0x2000, 4);

    for (const float sample : ir.timeOrderSamples())
        putLE (out, static_cast<uint32_t> (exportSample (sample)), 4);
    return out;
}
} // namespace qtn::IrConvert
