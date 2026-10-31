#pragma once

#include "Bytes.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace qtn::test
{
/** One message of a decoded MIDI Monitor capture (research/R8-verification.md). */
struct CaptureMessage
{
    int64_t timeNanos{};
    bool toDevice{}; // "To <device>" = host -> pedal; "From <device>" = pedal -> host
    Bytes bytes;
};

/** Parses the decoded capture text: one line per message,
    `timeStampNanos \t endpoint \t length \t hex bytes`. Empty lines are skipped. Any malformed
    line (wrong field count, unknown endpoint, bad hex, length mismatch) gives std::nullopt. */
inline std::optional<std::vector<CaptureMessage>> parseCapture (const std::string& text)
{
    std::vector<CaptureMessage> messages;
    std::istringstream lines (text);
    std::string line;

    while (std::getline (lines, line))
    {
        if (line.empty())
            continue;

        std::vector<std::string> fields;
        std::istringstream cells (line);
        for (std::string cell; std::getline (cells, cell, '\t');)
            fields.push_back (cell);
        if (fields.size() != 4)
            return std::nullopt;

        CaptureMessage message;
        try
        {
            message.timeNanos = std::stoll (fields[0]);
            if (fields[1].rfind ("To ", 0) == 0)
                message.toDevice = true;
            else if (fields[1].rfind ("From ", 0) != 0)
                return std::nullopt;

            const auto length = std::stoul (fields[2]);
            std::istringstream hex (fields[3]);
            for (std::string byte; hex >> byte;)
            {
                const auto value = std::stoul (byte, nullptr, 16);
                if (byte.size() != 2 || value > 0xFF)
                    return std::nullopt;
                message.bytes.push_back (static_cast<uint8_t> (value));
            }
            if (message.bytes.size() != length)
                return std::nullopt;
        }
        catch (const std::exception&)
        {
            return std::nullopt;
        }

        messages.push_back (std::move (message));
    }

    return messages;
}

/** The capture `<dir>/<name>`, or std::nullopt when `dir` is empty, the file is missing or it does
    not parse. */
inline std::optional<std::vector<CaptureMessage>> loadCapture (const std::string& dir, const std::string& name)
{
    if (dir.empty())
        return std::nullopt;

    const auto path = std::filesystem::path (dir) / name;
    if (! std::filesystem::exists (path))
        return std::nullopt;

    std::ifstream in (path);
    std::stringstream text;
    text << in.rdbuf();
    return parseCapture (text.str());
}
} // namespace qtn::test
