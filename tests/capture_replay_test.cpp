// Replays the owner's capture of the original QuickTone connecting to the pedal
// (research/captures/001-connect.txt, R8-verification.md) against our builders, classifier and
// Connector. The capture is never copied into the repo; these tests SKIP without QTN_CAPTURES_DIR.

#include "backup/BackupStore.h"
#include "device/Allowlist.h"
#include "device/Connector.h"
#include "device/DeviceInfo.h"
#include "device/DeviceSession.h"
#include "model/DeviceState.h"
#include "model/PresetFile.h"
#include "proto/Codec.h"
#include "support/CaptureFile.h"
#include "support/FakeTransport.h"
#include "support/TempDir.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace
{
using qtn::Bytes;
using qtn::Command;
using qtn::CommandKind;
using qtn::SendResult;
using qtn::proto::ReplyKind;
using qtn::test::CaptureMessage;
using qtn::test::FakeTransport;

constexpr auto connectCapture = "001-connect.txt";

std::vector<Bytes> requests (const std::vector<CaptureMessage>& capture, bool toDevice)
{
    std::vector<Bytes> out;
    for (const auto& message : capture)
        if (message.toDevice == toDevice)
            out.push_back (message.bytes);
    return out;
}

/** The connect capture, or SKIPs the test when it is not available. */
std::vector<CaptureMessage> connectMessages()
{
    auto capture = qtn::test::loadCapture (QTN_CAPTURES_DIR, connectCapture);
    if (! capture.has_value())
        SKIP ("QTN_CAPTURES_DIR not set or 001-connect.txt missing");
    return *capture;
}

/** The request sequence our builders produce for the connect sequence (R3b §4). */
std::vector<Command> expectedConnectRequests()
{
    std::vector<Command> commands{qtn::proto::getVersion(), qtn::proto::getGlobalParameters()};
    for (int slot = 0; slot < qtn::proto::presetCount; ++slot)
        commands.push_back (*qtn::proto::getOnePreset (slot));
    commands.push_back (qtn::proto::getCurrentPreset());
    return commands;
}

juce::String readText (const juce::File& file) { return file.loadFileAsString(); }

qtn::Allowlist shippedAllowlist()
{
    return qtn::Allowlist::fromJson (readText (juce::File (juce::String (QTN_RESOURCES_DIR) + "/allowlist.json")));
}

struct FakeClock
{
    juce::int64 ms = juce::Time (2026, 9, 31, 12, 0, 0).toMilliseconds();

    std::function<juce::Time()> fn()
    {
        return [this]
        { return juce::Time (ms); };
    }
};

Bytes readWholeFile (const std::string& path)
{
    std::ifstream in (path, std::ios::binary);
    return {std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>()};
}
} // namespace

TEST_CASE ("capture_file_parses_tab_separated_lines")
{
    const auto parsed = qtn::test::parseCapture ("10\tTo NUX NME-300\t3\tf0 7f f7\n"
                                                 "\n"
                                                 "20\tFrom NUX NME-300\t2\tb0 3c\n");
    REQUIRE (parsed.has_value());
    REQUIRE (parsed->size() == 2);
    CHECK ((*parsed)[0].timeNanos == 10);
    CHECK ((*parsed)[0].toDevice);
    CHECK ((*parsed)[0].bytes == Bytes{0xF0, 0x7F, 0xF7});
    CHECK_FALSE ((*parsed)[1].toDevice);
    CHECK ((*parsed)[1].bytes == Bytes{0xB0, 0x3C});
}

TEST_CASE ("capture_file_rejects_malformed_lines")
{
    CHECK_FALSE (qtn::test::parseCapture ("10\tTo X\t3\tf0 7f\n").has_value());    // length mismatch
    CHECK_FALSE (qtn::test::parseCapture ("10\tSideways X\t1\tf0\n").has_value()); // endpoint
    CHECK_FALSE (qtn::test::parseCapture ("10\tTo X\t1\tzz\n").has_value());       // hex
    CHECK_FALSE (qtn::test::parseCapture ("10\tTo X\t1\n").has_value());           // fields
    CHECK_FALSE (qtn::test::loadCapture ("", connectCapture).has_value());         // no folder
}

TEST_CASE ("capture_requests_match_our_builders")
{
    const auto capture = connectMessages();
    const auto sent = requests (capture, true);
    const auto expected = expectedConnectRequests();

    REQUIRE (sent.size() == expected.size());
    for (size_t i = 0; i < sent.size(); ++i)
    {
        CAPTURE (i, expected[i].name);
        CHECK (sent[i] == expected[i].bytes);
    }
}

TEST_CASE ("capture_replies_classify_as_expected")
{
    const auto capture = connectMessages();
    REQUIRE (capture.size() == 2 * expectedConnectRequests().size());

    for (size_t i = 0; i + 1 < capture.size(); i += 2)
    {
        CAPTURE (i);
        const auto& request = capture[i];
        const auto& reply = capture[i + 1];
        REQUIRE (request.toDevice);
        REQUIRE_FALSE (reply.toDevice);

        const size_t index = i / 2;
        const auto kind = qtn::proto::classify (reply.bytes);
        if (index == 0)
            CHECK (kind == ReplyKind::Version);
        else if (index == 1)
            CHECK (kind == ReplyKind::GlobalParams);
        else if (index < 2 + qtn::proto::presetCount)
        {
            CHECK (kind == ReplyKind::Preset);
            CHECK (qtn::proto::presetSlot (reply.bytes) == static_cast<int> (request.bytes[8]));
        }
        else
            CHECK (kind == ReplyKind::EditBuffer);
    }
}

TEST_CASE ("connector_replays_capture_end_to_end")
{
    const auto capture = connectMessages();
    const auto replies = requests (capture, false);

    qtn::test::TempDir dir;
    FakeTransport transport;
    FakeClock clock;
    qtn::DeviceSession session (transport, shippedAllowlist(), clock.fn());
    qtn::BackupStore store (dir.get());
    qtn::DeviceState state;
    qtn::Connector connector (session, store, state, clock.fn());

    std::optional<qtn::DeviceInfo> info;
    std::vector<juce::String> failures;
    connector.onConnected = [&] (const qtn::DeviceInfo& i)
    { info = i; };
    connector.onFailed = [&] (const juce::String& why)
    { failures.push_back (why); };
    transport.onReceive = [&] (const Bytes& raw)
    { connector.handleMessage (raw); };

    connector.start();
    for (const auto& message : capture)
    {
        clock.ms += 3;
        connector.tick();
        if (message.toDevice)
        {
            REQUIRE_FALSE (transport.sent.empty());
            CHECK (transport.sent.back() == message.bytes);
        }
        else
            transport.deliver (message.bytes);
    }

    CHECK (transport.sent == requests (capture, true));
    REQUIRE (failures.empty());
    REQUIRE (info.has_value());
    CHECK (info->firmware == "V01_20191106");
    CHECK (info->name == "NUX_MG1X");
    CHECK_FALSE (connector.isRunning());

    const auto backups = store.list();
    REQUIRE (backups.size() == 1);
    const auto blobs = store.load (backups.front());
    for (int slot = 0; slot < qtn::proto::presetCount; ++slot)
    {
        CAPTURE (slot);
        const auto blob = std::find_if (blobs.begin(), blobs.end(), [slot] (const auto& b)
                                        { return b.slot == slot; });
        REQUIRE (blob != blobs.end());
        CHECK (blob->raw == qtn::proto::presetData (replies[static_cast<size_t> (2 + slot)]));
    }

    const auto editSlot = qtn::proto::presetSlot (replies.back());
    REQUIRE (editSlot.has_value());
    CHECK (state.currentSlot() == *editSlot);
    REQUIRE (state.editBuffer().has_value());
    CHECK (state.editBuffer()->encode() == qtn::proto::presetData (replies.back()));
}

TEST_CASE ("capture_presets_equal_g0_export_user_slots")
{
    const auto capture = connectMessages();
    const std::string exportPath = QTN_G0_BACKUP;
    if (exportPath.empty() || ! std::filesystem::exists (exportPath))
        SKIP ("QTN_G0_BACKUP not set or file missing");

    const auto parsed = qtn::PresetFile::read (readWholeFile (exportPath));
    REQUIRE (parsed.contents.has_value());
    const auto& records = parsed.contents->records;
    REQUIRE (records.size() == qtn::PresetFile::allPresetsRecordCount);

    const auto replies = requests (capture, false);
    for (size_t slot = 0; slot < records.size(); ++slot)
    {
        CAPTURE (slot);
        CHECK (qtn::proto::presetData (replies[2 + slot]) == records[slot].preset.encode());
    }
}

TEST_CASE ("shipped_allowlist_contains_only_verified_reads")
{
    const auto json = juce::JSON::parse (readText (juce::File (juce::String (QTN_RESOURCES_DIR) + "/allowlist.json")));
    REQUIRE (json.isArray());
    std::vector<juce::String> names;
    for (const auto& entry : *json.getArray())
        names.push_back (entry.toString());
    CHECK (names == std::vector<juce::String>{"getVersion", "getGlobleParameter", "getOnePreset", "getCurrentPreset"});

    // Each name equals the name the builder produces.
    for (const auto& cmd : expectedConnectRequests())
        CHECK (shippedAllowlist().contains (cmd.name));

    FakeTransport transport;
    qtn::DeviceSession session (transport, shippedAllowlist());
    session.markBackupDone (true);
    session.setPolicy (qtn::WritePolicy::Full);

    const std::vector<Command> refused{
        *qtn::proto::paramChangeCommand (11, 64),
        *qtn::proto::selectPresetCommand (3),
        *qtn::proto::globalChangeCommand (0x40, 100),
        {"savePreset", CommandKind::SavePreset, 3, {0xF0, 0x00, 0xF7}},
        {"renamePreset", CommandKind::RenamePreset, 3, {0xF0, 0x00, 0xF7}},
        {"swapPreset", CommandKind::SwapPreset, 3, {0xF0, 0x00, 0xF7}},
        {"bulkWrite", CommandKind::BulkWrite, -1, {0xF0, 0x00, 0xF7}},
        {"irWrite", CommandKind::IRWrite, -1, {0xF0, 0x00, 0xF7}},
        {"irDelete", CommandKind::IRDelete, -1, {0xF0, 0x00, 0xF7}},
    };
    for (const auto& cmd : refused)
    {
        CAPTURE (cmd.name);
        CHECK (session.submit (cmd) == SendResult::NotAllowlisted);
    }
    CHECK (transport.sent.empty());
}
