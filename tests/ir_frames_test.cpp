#include "ir/IrData.h"
#include "ir/IrFrames.h"
#include "model/PresetFile.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
using qtn::Bytes;
using qtn::IrData;
namespace IrFrames = qtn::IrFrames;

const Bytes prefix{0xF0, 0x00, 0x11, 0x22, 0x66, 0x00, 0x70};

/** A 128-byte frame `P 70 <cmd>` with `fields` written at their offsets (offsets from F0). */
Bytes frame128 (uint8_t cmd, std::initializer_list<std::pair<size_t, Bytes>> fields)
{
    Bytes raw (128, 0x00);
    std::copy (prefix.begin(), prefix.end(), raw.begin());
    raw[7] = cmd;
    raw[127] = 0xF7;
    for (const auto& [offset, bytes] : fields)
        std::copy (bytes.begin(), bytes.end(), raw.begin() + static_cast<std::ptrdiff_t> (offset));
    return raw;
}

/** R7cd §3 worked example A: image `81 02 FF 00 7F 80 10` then zeros. */
IrData exampleAImage()
{
    Bytes image (IrData::byteCount, 0x00);
    const Bytes head{0x81, 0x02, 0xFF, 0x00, 0x7F, 0x80, 0x10};
    std::copy (head.begin(), head.end(), image.begin());
    return *IrData::fromDeviceBytes (image);
}

/** A valid image with every byte position used: a decaying sine in device order. */
IrData syntheticImage()
{
    std::array<float, IrData::sampleCount> samples{};
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = static_cast<float> (0.9 * std::exp (-static_cast<double> (i) / 400.0) * std::sin (0.37 * static_cast<double> (i) + 0.1));
    return IrData::fromDeviceSamples (samples);
}

Bytes allFF() { return Bytes (IrData::byteCount, 0xFF); }

std::vector<Bytes> bytesOf (const std::vector<qtn::Command>& commands)
{
    std::vector<Bytes> out;
    for (const auto& command : commands)
        out.push_back (command.bytes);
    return out;
}

Bytes readWholeFile (const std::string& path)
{
    std::ifstream in (path, std::ios::binary);
    return {std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>()};
}

/** The owner's backup records, or empty when QTN_G0_BACKUP is not set or the file is missing. */
std::vector<qtn::PresetRecord> realRecords()
{
    const std::string path = QTN_G0_BACKUP;
    if (path.empty() || ! std::filesystem::exists (path))
        return {};
    const auto result = qtn::PresetFile::read (readWholeFile (path));
    return result.contents.has_value() ? result.contents->records : std::vector<qtn::PresetRecord>{};
}
} // namespace

// ---- IrData ----

TEST_CASE ("ir_data_device_bytes_roundtrip")
{
    Bytes image (IrData::byteCount);
    for (size_t i = 0; i < image.size(); ++i)
        image[i] = static_cast<uint8_t> ((i * 37 + 11) & 0x3F); // finite floats
    const auto ir = IrData::fromDeviceBytes (image);
    REQUIRE (ir.has_value());
    CHECK (ir->toDeviceBytes() == image);
}

TEST_CASE ("ir_data_rejects_wrong_size")
{
    CHECK_FALSE (IrData::fromDeviceBytes (Bytes (8191)).has_value());
    CHECK_FALSE (IrData::fromDeviceBytes (Bytes (8193)).has_value());
}

TEST_CASE ("ir_data_samples_are_float32_le_reversed")
{
    Bytes image (IrData::byteCount, 0x00);
    const auto half = std::bit_cast<std::array<uint8_t, 4>> (0.5f); // little-endian on arm64
    std::copy (half.begin(), half.end(), image.begin());            // device sample 0
    const auto ir = *IrData::fromDeviceBytes (image);
    CHECK (ir.deviceSample (0) == 0.5f);
    const auto played = ir.timeOrderSamples();
    REQUIRE (played.size() == IrData::sampleCount);
    CHECK (played[2047] == 0.5f);
    CHECK (played[0] == 0.0f);
    CHECK (ir.isValid());
}

TEST_CASE ("nan_ir_rejected")
{
    const auto erased = IrData::fromDeviceBytes (allFF());
    REQUIRE (erased.has_value());
    CHECK_FALSE (erased->isValid());
    const auto reason = erased->invalidReason();
    REQUIRE (reason.has_value());
    CHECK (reason->find ("erased") != std::string::npos);
    CHECK (erased->toDeviceBytes() == allFF()); // kept byte for byte

    std::array<float, IrData::sampleCount> samples{};
    samples[100] = std::numeric_limits<float>::infinity();
    const auto withInf = IrData::fromDeviceSamples (samples);
    CHECK_FALSE (withInf.isValid());
    CHECK (withInf.invalidReason()->find ("100") != std::string::npos);

    CHECK_FALSE (IrFrames::encodeUpload (*erased, 35).has_value());
    CHECK_FALSE (IrFrames::encodeUpload (withInf, 35).has_value());
}

// ---- encoding ----

TEST_CASE ("worked_example_a_data_frame_byte_exact")
{
    const auto frames = IrFrames::encodeUpload (exampleAImage(), 0x23);
    REQUIRE (frames.has_value());
    REQUIRE (frames->size() == IrFrames::uploadFrameCount);

    const auto expected = frame128 (0x58, {{11, {0x52}}, {25, {0x01, 0x02, 0x7F, 0x00, 0x7F, 0x00, 0x10}}, {123, {0x05, 0x11}}});
    CHECK ((*frames)[1].bytes == expected);
}

TEST_CASE ("worked_example_a_trailer_byte_exact")
{
    CHECK (IrFrames::transferTotal (exampleAImage().toDeviceBytes()) == 9198u);

    const auto frames = IrFrames::encodeUpload (exampleAImage(), 0x23);
    REQUIRE (frames.has_value());
    const auto expected = frame128 (0x58, {{8, {0x7F, 0x7F, 0x7F}}, {11, {0x00, 0x00, 0x47, 0x6E}}, {25, {0x23}}, {125, {0x00, 0x54}}});
    CHECK ((*frames)[85].bytes == expected);
}

TEST_CASE ("upload_start_end_and_later_data_frames")
{
    const auto frames = IrFrames::encodeUpload (exampleAImage(), 7);
    REQUIRE (frames.has_value());
    CHECK ((*frames)[0].bytes == frame128 (0x50, {{11, {0x07}}}));
    CHECK ((*frames)[86].bytes == frame128 (0x50, {{8, {0x7F, 0x7F, 0x7F}}}));
    // Data frame k = 83 (index 84): address 98 * 83 = 8134 = 00 3F 46, counter 00 53, zero data.
    CHECK ((*frames)[84].bytes == frame128 (0x58, {{8, {0x00, 0x3F, 0x46}}, {125, {0x00, 0x53}}}));
    // k = 2: address 196 = 00 01 44, counter 00 02.
    CHECK ((*frames)[3].bytes == frame128 (0x58, {{8, {0x00, 0x01, 0x44}}, {125, {0x00, 0x02}}}));

    for (const auto& command : *frames)
    {
        CHECK (command.name == "importIR");
        CHECK (command.kind == qtn::CommandKind::IRWrite);
        CHECK (command.targetSlot == 7);
        CHECK (command.bytes.size() == 128);
    }
}

TEST_CASE ("encode_rejects_slot_out_of_range")
{
    CHECK_FALSE (IrFrames::encodeUpload (exampleAImage(), -1).has_value());
    CHECK_FALSE (IrFrames::encodeUpload (exampleAImage(), 36).has_value());
    CHECK (IrFrames::encodeUpload (exampleAImage(), 0).has_value());
    CHECK (IrFrames::encodeUpload (exampleAImage(), 35).has_value());
}

TEST_CASE ("transfer_total_adds_running_sums_per_group")
{
    // One byte 0x01 in group 1 of frame 0: running sum is 0 after group 0, then 1 for groups
    // 1-13 -> T = 13. A plain sum would give 1.
    Bytes image (IrData::byteCount, 0x00);
    image[7] = 0x01;
    CHECK (IrFrames::transferTotal (image) == 13u);
    // Same byte in the last data frame (offset 98 * 83): group 0 of that frame -> 14.
    Bytes last (IrData::byteCount, 0x00);
    last[98 * 83] = 0x01;
    CHECK (IrFrames::transferTotal (last) == 14u);
}

// ---- decoding ----

TEST_CASE ("upload_frames_roundtrip_decode")
{
    const auto ir = syntheticImage();
    const auto frames = IrFrames::encodeUpload (ir, 35);
    REQUIRE (frames.has_value());
    const auto decoded = IrFrames::decodeDownload (bytesOf (*frames));
    REQUIRE (std::holds_alternative<IrData> (decoded));
    CHECK (std::get<IrData> (decoded).toDeviceBytes() == ir.toDeviceBytes());
}

TEST_CASE ("decode_accepts_download_without_start_and_end")
{
    const auto ir = syntheticImage();
    auto frames = bytesOf (*IrFrames::encodeUpload (ir, 3));
    const std::vector<Bytes> download (frames.begin() + 1, frames.end() - 1); // 84 data + trailer
    const auto decoded = IrFrames::decodeDownload (download);
    REQUIRE (std::holds_alternative<IrData> (decoded));
    CHECK (std::get<IrData> (decoded) == ir);
}

TEST_CASE ("decode_rejects_bad_frame_checksum")
{
    auto frames = bytesOf (*IrFrames::encodeUpload (syntheticImage(), 3));
    frames[10][124] = static_cast<uint8_t> (frames[10][124] ^ 0x01);
    const auto decoded = IrFrames::decodeDownload (frames);
    REQUIRE (std::holds_alternative<qtn::IrError> (decoded));
    CHECK (std::get<qtn::IrError> (decoded).reason.find ("checksum") != std::string::npos);
}

TEST_CASE ("decode_rejects_bad_transfer_total")
{
    auto frames = bytesOf (*IrFrames::encodeUpload (syntheticImage(), 3));
    frames[85][14] = static_cast<uint8_t> (frames[85][14] ^ 0x01);
    const auto decoded = IrFrames::decodeDownload (frames);
    REQUIRE (std::holds_alternative<qtn::IrError> (decoded));
    CHECK (std::get<qtn::IrError> (decoded).reason.find ("total") != std::string::npos);
}

TEST_CASE ("decode_rejects_missing_or_reordered_frames")
{
    const auto frames = bytesOf (*IrFrames::encodeUpload (syntheticImage(), 3));

    auto missing = frames;
    missing.erase (missing.begin() + 40);
    CHECK (std::holds_alternative<qtn::IrError> (IrFrames::decodeDownload (missing)));

    auto swapped = frames;
    std::swap (swapped[5], swapped[6]);
    CHECK (std::holds_alternative<qtn::IrError> (IrFrames::decodeDownload (swapped)));

    auto noTrailer = frames;
    noTrailer.erase (noTrailer.begin() + 85);
    CHECK (std::holds_alternative<qtn::IrError> (IrFrames::decodeDownload (noTrailer)));

    auto shortFrame = frames;
    shortFrame[20].pop_back();
    CHECK (std::holds_alternative<qtn::IrError> (IrFrames::decodeDownload (shortFrame)));
}

// ---- real data (owner's backup) ----

TEST_CASE ("real_slot0_ir_encode_decode_roundtrip")
{
    const auto records = realRecords();
    if (records.empty())
        SKIP ("QTN_G0_BACKUP not set or file missing");

    REQUIRE (records[0].hasIr());
    const auto ir = IrData::fromDeviceBytes (records[0].irSamples);
    REQUIRE (ir.has_value());
    CHECK (ir->isValid());

    const auto frames = IrFrames::encodeUpload (*ir, 0);
    REQUIRE (frames.has_value());
    const auto decoded = IrFrames::decodeDownload (bytesOf (*frames));
    REQUIRE (std::holds_alternative<IrData> (decoded));
    CHECK (std::get<IrData> (decoded).toDeviceBytes() == records[0].irSamples);
}

TEST_CASE ("worked_example_b_real_slot0_checksums")
{
    const auto records = realRecords();
    if (records.empty())
        SKIP ("QTN_G0_BACKUP not set or file missing");

    const auto ir = *IrData::fromDeviceBytes (records[0].irSamples);
    CHECK (IrFrames::transferTotal (records[0].irSamples) == 8094860u);
    const auto frames = *IrFrames::encodeUpload (ir, 0);
    const auto& first = frames[1].bytes;
    CHECK (Bytes (first.end() - 5, first.end()) == Bytes{0x5F, 0x67, 0x00, 0x00, 0xF7});
    const auto& trailer = frames[85].bytes;
    CHECK (Bytes (trailer.begin() + 11, trailer.begin() + 15) == Bytes{0x03, 0x6E, 0x09, 0x0C});
    CHECK (Bytes (trailer.begin() + 125, trailer.begin() + 127) == Bytes{0x00, 0x54});
}

TEST_CASE ("real_erased_slots_rejected")
{
    const auto records = realRecords();
    if (records.empty())
        SKIP ("QTN_G0_BACKUP not set or file missing");

    for (const size_t slot : {24u, 25u, 30u, 31u})
    {
        CAPTURE (slot);
        const auto ir = IrData::fromDeviceBytes (records[slot].irSamples);
        REQUIRE (ir.has_value());
        CHECK_FALSE (ir->isValid());
        CHECK_FALSE (IrFrames::encodeUpload (*ir, static_cast<int> (slot)).has_value());
    }
}
