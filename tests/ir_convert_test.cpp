#include "ir/IrConvert.h"
#include "support/TempDir.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <complex>
#include <cstring>
#include <limits>
#include <numbers>
#include <vector>

namespace
{
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using qtn::Bytes;
using qtn::IrData;
using qtn::IrError;
using qtn::test::TempDir;

/** A decaying two-tone signal: energy in the normalisation band, no silent bins. */
std::vector<float> testSignal (size_t length)
{
    std::vector<float> out (length);
    for (size_t t = 0; t < length; ++t)
    {
        const auto x = static_cast<double> (t);
        out[t] = static_cast<float> (0.8 * std::exp (-x / 300.0) * (std::sin (2.0 * std::numbers::pi * 1000.0 * x / 48000.0) + 0.3 * std::sin (2.0 * std::numbers::pi * 5000.0 * x / 48000.0)));
    }
    return out;
}

IrData requireIr (const std::variant<IrData, IrError>& result)
{
    if (const auto* error = std::get_if<IrError> (&result))
        FAIL (error->reason);
    return std::get<IrData> (result);
}

/** Reference gain straight from the documented formula (R7cd §2 step 6), independent of the
    production FFT: DFT of the time-order samples zero-padded to 16384 points, bins 136-1023,
    p = re^2 + im^2, g = 1 / sqrt(sqrt(pmax) * sqrt(pmin)). */
double referenceGain (const std::vector<float>& timeOrder)
{
    constexpr int fftSize = 16384;
    double pmax = 0.0;
    double pmin = std::numeric_limits<double>::max();
    for (int k = 136; k < 136 + 888; ++k)
    {
        const auto step = std::polar (1.0, -2.0 * std::numbers::pi * k / fftSize);
        std::complex<double> w{1.0, 0.0};
        std::complex<double> sum{};
        for (const float x : timeOrder)
        {
            sum += static_cast<double> (x) * w;
            w *= step;
        }
        const double p = std::norm (sum);
        pmax = std::max (pmax, p);
        pmin = std::min (pmin, p);
    }
    return 1.0 / std::sqrt (std::sqrt (pmax) * std::sqrt (pmin));
}

std::array<float, IrData::sampleCount> deviceOrder (const std::vector<float>& timeOrder)
{
    std::array<float, IrData::sampleCount> out{};
    for (size_t i = 0; i < timeOrder.size() && i < out.size(); ++i)
        out[IrData::sampleCount - 1 - i] = timeOrder[i];
    return out;
}

uint32_t u32le (const Bytes& b, size_t at)
{
    return static_cast<uint32_t> (b[at]) | (static_cast<uint32_t> (b[at + 1]) << 8) | (static_cast<uint32_t> (b[at + 2]) << 16) | (static_cast<uint32_t> (b[at + 3]) << 24);
}

uint16_t u16le (const Bytes& b, size_t at) { return static_cast<uint16_t> (b[at] | (b[at + 1] << 8)); }

int32_t s32le (const Bytes& b, size_t at) { return static_cast<int32_t> (u32le (b, at)); }

/** A 16-bit PCM WAV file (canonical 44-byte header). */
Bytes pcm16Wav (int sampleRate, int channels, const std::vector<int16_t>& interleaved)
{
    Bytes out;
    auto put32 = [&] (uint32_t v)
    { for (int i = 0; i < 4; ++i) out.push_back (static_cast<uint8_t> (v >> (8 * i))); };
    auto put16 = [&] (uint16_t v)
    { out.push_back (static_cast<uint8_t> (v)); out.push_back (static_cast<uint8_t> (v >> 8)); };
    auto tag = [&] (const char* t)
    { out.insert (out.end(), t, t + 4); };
    const auto dataSize = static_cast<uint32_t> (interleaved.size() * 2);
    tag ("RIFF");
    put32 (36 + dataSize);
    tag ("WAVE");
    tag ("fmt ");
    put32 (16);
    put16 (1);
    put16 (static_cast<uint16_t> (channels));
    put32 (static_cast<uint32_t> (sampleRate));
    put32 (static_cast<uint32_t> (sampleRate * channels * 2));
    put16 (static_cast<uint16_t> (channels * 2));
    put16 (16);
    tag ("data");
    put32 (dataSize);
    for (const auto s : interleaved)
        put16 (static_cast<uint16_t> (s));
    return out;
}

juce::File writeTemp (const TempDir& dir, const Bytes& bytes, const char* name)
{
    auto file = dir.get().getChildFile (name);
    REQUIRE (file.replaceWithData (bytes.data(), bytes.size()));
    return file;
}
} // namespace

// ---- stage 2-4: 2048 samples, reversed, normalised ----

TEST_CASE ("normalisation_matches_reference")
{
    const auto signal = testSignal (IrData::sampleCount);
    const auto device = deviceOrder (signal);
    const double expected = referenceGain (signal);

    CHECK_THAT (qtn::IrConvert::normalisationGain (device), WithinRel (expected, 1e-4));

    const auto ir = requireIr (qtn::IrConvert::fromMono48k (signal));
    for (size_t i = 0; i < IrData::sampleCount; i += 97)
        CHECK_THAT (ir.deviceSample (i), WithinAbs (device[i] * expected, 1e-4 * std::abs (device[i] * expected) + 1e-9));
}

TEST_CASE ("convert_reverses_samples")
{
    std::vector<float> mono (IrData::sampleCount, 0.0f);
    mono[10] = 0.5f;
    mono[20] = 0.25f;
    mono[1500] = 0.125f;
    const auto ir = requireIr (qtn::IrConvert::fromMono48k (mono));

    const float a = ir.deviceSample (2047 - 10);
    REQUIRE (a != 0.0f);
    CHECK_THAT (ir.deviceSample (2047 - 20) / a, WithinRel (0.5, 1e-6));
    CHECK_THAT (ir.deviceSample (2047 - 1500) / a, WithinRel (0.25, 1e-6));
    CHECK (ir.deviceSample (10) == 0.0f);
    CHECK (ir.deviceSample (2047) == 0.0f);
    const auto played = ir.timeOrderSamples();
    CHECK (played[10] == a);
}

TEST_CASE ("convert_short_file_zero_pads")
{
    std::vector<float> mono (100);
    for (size_t i = 0; i < mono.size(); ++i)
        mono[i] = 0.01f * static_cast<float> (i + 1) * ((i % 2 == 0) ? 1.0f : -1.0f);
    const auto ir = requireIr (qtn::IrConvert::fromSamples (48000.0, 1, mono));
    const auto played = ir.timeOrderSamples();
    for (size_t i = 0; i < 100; ++i)
        CHECK (played[i] != 0.0f);
    for (size_t i = 100; i < IrData::sampleCount; ++i)
        CHECK (played[i] == 0.0f);
}

TEST_CASE ("convert_long_file_truncates")
{
    auto longSignal = testSignal (5000);
    for (size_t i = IrData::sampleCount; i < longSignal.size(); ++i)
        longSignal[i] = 0.99f; // must not reach the image or the gain
    const auto fromLong = requireIr (qtn::IrConvert::fromSamples (48000.0, 1, longSignal));
    const std::vector<float> head (longSignal.begin(), longSignal.begin() + IrData::sampleCount);
    const auto fromHead = requireIr (qtn::IrConvert::fromSamples (48000.0, 1, head));
    CHECK (fromLong.toDeviceBytes() == fromHead.toDeviceBytes());
}

TEST_CASE ("convert_silent_input_is_error")
{
    const std::vector<float> silence (3000, 0.0f);
    const auto result = qtn::IrConvert::fromSamples (48000.0, 1, silence);
    REQUIRE (std::holds_alternative<IrError> (result));
    CHECK (IrError::hint == "IR Sampling Rate Error");
    CHECK (std::holds_alternative<IrError> (qtn::IrConvert::fromSamples (48000.0, 1, {})));
}

// ---- stage 1: 48 kHz mono 24-bit ----

TEST_CASE ("convert_44k1_resamples_to_48k")
{
    const std::vector<float> dc (1000, 0.5f);
    const auto at48 = qtn::IrConvert::toMono48k (44100.0, 1, dc);
    // Length = 1000 / (44100 / 48000) = 1088.4 -> 1088 samples (R7cd §2 step 2).
    REQUIRE (at48.size() == 1088);
    for (size_t i = 200; i < 1000; i += 50)
        CHECK_THAT (at48[i], WithinAbs (0.5, 0.01)); // low-pass settled

    const auto same = qtn::IrConvert::toMono48k (48000.0, 1, dc);
    REQUIRE (same.size() == 1000);
    CHECK (same[0] == 0.5f);
    CHECK (same[999] == 0.5f);

    const std::vector<float> sine = testSignal (4000);
    const std::vector<float> down = qtn::IrConvert::toMono48k (96000.0, 1, sine);
    CHECK (down.size() == 2000);
}

TEST_CASE ("convert_takes_first_channel_and_quantises_to_24_bit")
{
    std::vector<float> stereo;
    for (int i = 0; i < 300; ++i)
    {
        stereo.push_back (0.1f + 1.0e-9f); // left, not a 24-bit value
        stereo.push_back (-0.7f);          // right, ignored
    }
    const auto mono = qtn::IrConvert::toMono48k (48000.0, 2, stereo);
    REQUIRE (mono.size() == 300);
    const double scaled = static_cast<double> (mono[0]) * 8388608.0; // 2^23
    CHECK (scaled == std::round (scaled));
    CHECK_THAT (mono[0], WithinAbs (0.1, 1e-6));
    CHECK (qtn::IrConvert::toMono48k (0.0, 1, stereo).empty());
    CHECK (qtn::IrConvert::toMono48k (48000.0, 0, stereo).empty());
}

// ---- files ----

TEST_CASE ("convert_file_matches_samples_path")
{
    std::vector<int16_t> pcm (2500);
    const auto signal = testSignal (pcm.size());
    for (size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = static_cast<int16_t> (std::lround (signal[i] * 32767.0f));
    const TempDir dir;
    const auto file = writeTemp (dir, pcm16Wav (48000, 1, pcm), "ir.wav");

    std::vector<float> asFloat (pcm.size());
    for (size_t i = 0; i < pcm.size(); ++i)
        asFloat[i] = static_cast<float> (pcm[i]) / 32768.0f;

    const auto fromFile = requireIr (qtn::IrConvert::fromAudioFile (file));
    const auto fromSamples = requireIr (qtn::IrConvert::fromSamples (48000.0, 1, asFloat));
    CHECK (fromFile.toDeviceBytes() == fromSamples.toDeviceBytes());
}

TEST_CASE ("convert_unreadable_file_is_error")
{
    const TempDir dir;
    const auto junk = writeTemp (dir, Bytes (500, 0x5A), "junk.wav");
    CHECK (std::holds_alternative<IrError> (qtn::IrConvert::fromAudioFile (junk)));
    CHECK (std::holds_alternative<IrError> (qtn::IrConvert::fromAudioFile (dir.get().getChildFile ("missing-ir.wav"))));
}

// ---- export ----

TEST_CASE ("wav_export_header_and_samples")
{
    std::array<float, IrData::sampleCount> device{};
    // Time order t = device index 2047 - t.
    device[2047 - 0] = 0.5f;
    device[2047 - 1] = -0.25f;
    device[2047 - 2] = 1.0f; // 2^31 overflows: x86 cvttsd2si gives INT_MIN
    device[2047 - 3] = -1.0f;
    device[2047 - 4] = 1.25e-9f;  // 2.68 -> 2 (truncation, not rounding)
    device[2047 - 5] = -1.25e-9f; // -2.68 -> -2
    device[0] = 0.75f;            // last sample in time order
    const auto wav = qtn::IrConvert::toWav (IrData::fromDeviceSamples (device));

    REQUIRE (wav.size() == 44 + 8192);
    CHECK (std::memcmp (wav.data(), "RIFF", 4) == 0);
    CHECK (u32le (wav, 4) == 0x2024u);
    CHECK (std::memcmp (wav.data() + 8, "WAVEfmt ", 8) == 0);
    CHECK (u32le (wav, 16) == 16u);
    CHECK (u16le (wav, 20) == 1);      // PCM
    CHECK (u16le (wav, 22) == 1);      // mono
    CHECK (u32le (wav, 24) == 48000u); // rate
    CHECK (u32le (wav, 28) == 192000u);
    CHECK (u16le (wav, 32) == 4);
    CHECK (u16le (wav, 34) == 32);
    CHECK (std::memcmp (wav.data() + 36, "data", 4) == 0);
    CHECK (u32le (wav, 40) == 0x2000u);

    CHECK (s32le (wav, 44 + 0) == 0x40000000);
    CHECK (s32le (wav, 44 + 4) == -0x20000000);
    CHECK (s32le (wav, 44 + 8) == std::numeric_limits<int32_t>::min());
    CHECK (s32le (wav, 44 + 12) == std::numeric_limits<int32_t>::min());
    CHECK (s32le (wav, 44 + 16) == 2);
    CHECK (s32le (wav, 44 + 20) == -2);
    CHECK (s32le (wav, 44 + 24) == 0);
    CHECK (s32le (wav, 44 + 4 * 2047) == 0x60000000);
}
