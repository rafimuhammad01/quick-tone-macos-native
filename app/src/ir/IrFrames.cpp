#include "ir/IrFrames.h"

#include "proto/Codec.h"

#include <algorithm>
#include <string>

// Frame layouts: R7cd §3 (decomp:IR_DataList/ChangeWaveData2Cabinet@10005eaf0.c, receiver
// decomp:NuxMidiTransmission/handleIncomingMidiMessage@10002d730.c:L126-238). Offsets in the
// comments count from F0 = 0; `args` index = frame offset - 8.
namespace qtn::IrFrames
{
namespace
{
constexpr uint8_t startEndCmd = 0x50;
constexpr uint8_t dataCmd = 0x58;
constexpr size_t argsSize = frameSize - 9; // F0 + 6-byte header + cmd ... F7
constexpr size_t groupsPerFrame = bytesPerDataFrame / 7;
constexpr uint32_t trailerAddress = 0x1FFFFF; // 7F 7F 7F
constexpr size_t paddedSize = dataFrameCount * bytesPerDataFrame;

static_assert (paddedSize >= IrData::byteCount && paddedSize - bytesPerDataFrame < IrData::byteCount);

/** The image zero-filled to whole frames (frame 83 carries 58 real bytes, L235). */
Bytes paddedImage (std::span<const uint8_t> image)
{
    Bytes out (paddedSize, 0x00);
    std::copy_n (image.begin(), std::min (image.size(), paddedSize), out.begin());
    return out;
}

uint32_t frameTotal (std::span<const uint8_t> chunk) noexcept
{
    uint32_t running = 0;
    uint32_t total = 0;
    for (size_t g = 0; g < groupsPerFrame; ++g)
    {
        for (size_t j = 0; j < 7; ++j)
            running += chunk[7 * g + j];
        total += running; // asm@0x10005f0d7-0x10005f0db: addl %edx,%ecx inside the group loop
    }
    return total;
}

void put (Bytes& args, size_t frameOffset, std::span<const uint8_t> bytes)
{
    std::copy (bytes.begin(), bytes.end(), args.begin() + static_cast<std::ptrdiff_t> (frameOffset - 8));
}

Command uploadCommand (int slot, uint8_t cmd, const Bytes& args)
{
    return Command{uploadName, CommandKind::IRWrite, slot, proto::frame (cmd, args)};
}

IrError fail (std::string reason) { return IrError{std::move (reason)}; }
} // namespace

uint32_t transferTotal (std::span<const uint8_t> image) noexcept
{
    uint32_t total = 0;
    for (size_t k = 0; k < dataFrameCount; ++k)
    {
        uint8_t chunk[bytesPerDataFrame]{};
        const auto start = k * bytesPerDataFrame;
        if (start < image.size())
            std::copy_n (image.begin() + static_cast<std::ptrdiff_t> (start), std::min (bytesPerDataFrame, image.size() - start), chunk);
        total += frameTotal (chunk);
    }
    return total & 0x0FFFFFFF;
}

std::optional<std::vector<Command>> encodeUpload (const IrData& ir, int slot)
{
    if (slot < firstSlot || slot > lastSlot || ! ir.isValid())
        return std::nullopt;

    const auto image = paddedImage (ir.toDeviceBytes());
    const auto slotByte = static_cast<uint8_t> (slot);
    std::vector<Command> frames;
    frames.reserve (uploadFrameCount);

    // Start: address 00 00 00, [11] = slot (L180-201).
    Bytes start (argsSize, 0x00);
    start[11 - 8] = slotByte;
    frames.push_back (uploadCommand (slot, startEndCmd, start));

    // Data frames k = 0..83 (L240-327).
    for (size_t k = 0; k < dataFrameCount; ++k)
    {
        const std::span<const uint8_t> chunk (image.data() + k * bytesPerDataFrame, bytesPerDataFrame);
        Bytes args (argsSize, 0x00);
        put (args, 8, *proto::split21 (static_cast<uint32_t> (k * bytesPerDataFrame)));
        put (args, 11, *proto::pack7 (chunk)); // 14 MSB bytes, then 98 low-7-bit bytes
        put (args, 123, *proto::split14 (proto::sum14 (chunk)));
        put (args, 125, *proto::split14 (static_cast<uint32_t> (k)));
        frames.push_back (uploadCommand (slot, dataCmd, args));
    }

    // Trailer: 7F 7F 7F, T at [11..14], slot at [25], frame count at [125..126] (L365-409).
    Bytes trailer (argsSize, 0x00);
    put (trailer, 8, *proto::split21 (trailerAddress));
    put (trailer, 11, *proto::split28 (transferTotal (image)));
    trailer[25 - 8] = slotByte;
    put (trailer, 125, *proto::split14 (static_cast<uint32_t> (dataFrameCount)));
    frames.push_back (uploadCommand (slot, dataCmd, trailer));

    // End: 50 7F 7F 7F, rest 0 (L412-431).
    Bytes end (argsSize, 0x00);
    put (end, 8, *proto::split21 (trailerAddress));
    frames.push_back (uploadCommand (slot, startEndCmd, end));

    return frames;
}

std::variant<IrData, IrError> decodeDownload (std::span<const Bytes> frames)
{
    Bytes image (paddedSize, 0x00);
    size_t dataFrames = 0;
    std::optional<uint32_t> trailerTotal;

    for (size_t i = 0; i < frames.size(); ++i)
    {
        const auto where = "frame " + std::to_string (i) + ": ";
        const auto& raw = frames[i];
        if (raw.size() != frameSize)
            return fail (where + "expected 128 bytes, got " + std::to_string (raw.size()));

        const auto parsed = proto::parse (raw);
        if (! parsed.has_value() || parsed->args.size() != argsSize)
            return fail (where + "not an NME-300 sysex frame");

        const auto& args = parsed->args;
        if (parsed->cmd == startEndCmd)
            continue; // start (00 00 00) / end (7F 7F 7F): no data

        if (parsed->cmd != dataCmd)
            return fail (where + "unexpected command");
        if (trailerTotal.has_value())
            return fail (where + "data after the trailer");

        const auto address = *proto::join7 (std::span (args).subspan (0, 3));
        if (address == trailerAddress)
        {
            trailerTotal = *proto::join7 (std::span (args).subspan (11 - 8, 4));
            continue;
        }

        // The receiver copies to +addr without an order check; we require frames in order.
        // The counter [125..126] is not checked: the pedal's download frames are not captured
        // yet (R7cd §8 Q2). UNVERIFIED
        if (address != dataFrames * bytesPerDataFrame || dataFrames >= dataFrameCount)
            return fail (where + "address " + std::to_string (address) + " out of order");

        const auto data = proto::unpack7 (std::span (args).subspan (11 - 8, 112));
        const auto checksum = *proto::join7 (std::span (args).subspan (123 - 8, 2));
        if (! data.has_value() || proto::sum14 (*data) != checksum) // receiver L229: & 0x3FFF
            return fail (where + "frame checksum mismatch");

        std::copy (data->begin(), data->end(), image.begin() + static_cast<std::ptrdiff_t> (address));
        ++dataFrames;
    }

    if (dataFrames != dataFrameCount)
        return fail ("expected 84 data frames, got " + std::to_string (dataFrames));
    if (! trailerTotal.has_value())
        return fail ("trailer frame (58 7F 7F 7F) missing");
    if (*trailerTotal != transferTotal (image)) // receiver L236-237
        return fail ("transfer total mismatch");

    return *IrData::fromDeviceBytes (std::span (image).first (IrData::byteCount));
}
} // namespace qtn::IrFrames
