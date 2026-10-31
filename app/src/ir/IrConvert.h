#pragma once

#include "Bytes.h"
#include "ir/IrData.h"

#include <juce_core/juce_core.h>

#include <span>
#include <variant>
#include <vector>

/** Audio file -> IR image, and IR image -> WAV file, as QuickTone's cabinet popup does it
    (R7cd §2, export in §4a #1). */
namespace qtn::IrConvert
{
inline constexpr double deviceSampleRate = 48000.0;

/** Stage 1 of the import (AudioFormatHelper::convertAudioToWaveFormat, R7cd §2 step 2): the first
    channel resampled to 48 kHz with juce::ResamplingAudioSource (ratio rate / 48000), cut to
    length / ratio samples and passed through a 24-bit mono WAV, as the original's temp file.
    `interleaved` holds `numChannels` interleaved channels. Only the first 2048 output samples
    are produced (later ones are never read). Empty for a rate <= 0 or channel count < 1. */
[[nodiscard]] std::vector<float> toMono48k (double sampleRate, int numChannels, std::span<const float> interleaved);

/** Gain of the original's FFT normalisation (R7cd §2 step 6) for samples in device order. */
[[nodiscard]] double normalisationGain (std::span<const float, IrData::sampleCount> deviceOrder);

/** Stages 2-4: first 2048 samples of 48 kHz mono audio (zero-padded), reversed, normalised.
    An error when the result is not finite (silent input). */
[[nodiscard]] std::variant<IrData, IrError> fromMono48k (std::span<const float> mono48k);

/** The whole import for decoded audio: toMono48k, then fromMono48k. */
[[nodiscard]] std::variant<IrData, IrError> fromSamples (double sampleRate, int numChannels, std::span<const float> interleaved);

/** The whole import for a file: any format juce::AudioFormatManager::registerBasicFormats reads.
    A 48 kHz mono 24-bit WAV is used unchanged (the original copies it); anything else goes
    through toMono48k. */
[[nodiscard]] std::variant<IrData, IrError> fromAudioFile (const juce::File& file);

/** QuickTone's IR export: 44-byte header (RIFF 0x2024, PCM, mono, 48000 Hz, 32 bit, data
    0x2000) and 2048 int32 samples (int)(sample * 2^31), in playing order. */
[[nodiscard]] Bytes toWav (const IrData& ir);
} // namespace qtn::IrConvert
