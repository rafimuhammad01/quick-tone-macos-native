#include "backup/BackupStore.h"
#include "device/Connector.h"
#include "device/DeviceInfo.h"
#include "device/DeviceSession.h"
#include "model/DeviceState.h"
#include "model/PresetFile.h"
#include "proto/Codec.h"
#include "support/FakeTransport.h"
#include "support/TempDir.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;

using qtn::Bytes;
using qtn::Command;
using qtn::CommandKind;
using qtn::SendResult;
using qtn::test::FakeTransport;
using qtn::test::TempDir;

constexpr int totalRequests = 75; // version + globals + 72 presets + edit buffer (R3b §4)

/** A clock that only moves when told to. */
struct FakeClock
{
    juce::int64 ms = juce::Time (2026, 9, 31, 12, 0, 0).toMilliseconds();

    std::function<juce::Time()> fn()
    {
        return [this]
        { return juce::Time (ms); };
    }
};

Bytes header (uint8_t type, uint8_t cmd) { return {0xF0, 0x00, 0x11, 0x22, 0x66, 0x00, type, cmd}; }

Bytes versionReply (const std::string& version, const std::string& name)
{
    auto raw = header (0x10, 0x00);
    raw.pop_back(); // raw[7] is the first version character
    for (size_t i = 0; i < 32; ++i)
        raw.push_back (i < version.size() ? static_cast<uint8_t> (version[i]) : 0x00);
    for (size_t i = 0; i < 8; ++i)
        raw.push_back (i < name.size() ? static_cast<uint8_t> (name[i]) : 0x00);
    raw.push_back (0xF7);
    return raw;
}

const Bytes globalBlock{0x05, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

Bytes globalReply()
{
    auto raw = header (0x70, 0x18);
    raw.push_back (0x00);
    raw.insert (raw.end(), globalBlock.begin(), globalBlock.end());
    raw.push_back (0xF7);
    return raw;
}

/** Synthetic preset data for a slot: every byte = slot + 1 (all 7-bit). */
Bytes presetBytes (int slot) { return Bytes (qtn::proto::presetSize, static_cast<uint8_t> (slot + 1)); }

Bytes presetMessage (uint8_t cmd, int slot, const Bytes& data)
{
    auto raw = header (0x70, cmd);
    raw.push_back (static_cast<uint8_t> (slot));
    raw.insert (raw.end(), data.begin(), data.end());
    raw.push_back (0xF7);
    return raw;
}

Bytes presetReply (int slot, const Bytes& data) { return presetMessage (0x06, slot, data); }

constexpr int editIndex = 5;
const Bytes editBytes (qtn::proto::presetSize, 0x33);

Bytes editReply() { return presetMessage (0x12, editIndex, editBytes); }

const Bytes versionRequest{0xF0, 0x00, 0x11, 0x22, 0x66, 0x00, 0x00, 0x00, 0x00, 0xF7};

Bytes request (uint8_t cmd, uint8_t arg) { return {0xF0, 0x00, 0x11, 0x22, 0x66, 0x00, 0x70, cmd, arg, 0xF7}; }

qtn::Allowlist readAllowlist()
{
    return qtn::Allowlist::fromJson (
        R"(["getVersion", "getGlobleParameter", "getOnePreset", "getCurrentPreset", "liveEdit"])");
}

const Command liveEdit{"liveEdit", CommandKind::LiveEdit, -1, {0xF0, 0x01, 0xF7}};

/** Everything a Connector needs, wired the way the app wires it. */
struct Fixture
{
    TempDir dir;
    FakeTransport transport;
    FakeClock clock;
    qtn::DeviceSession session;
    qtn::BackupStore store{dir.get()};
    qtn::DeviceState state;
    qtn::Connector connector{session, store, state, clock.fn()};

    int connectedCount = 0;
    std::optional<qtn::DeviceInfo> info;
    std::optional<juce::File> savedFolder;
    std::vector<juce::String> failures;
    std::vector<std::pair<int, int>> progress;

    /** Replies for the version, the globals and every preset slot; tests may change them. */
    Bytes version = versionReply ("V1.0.6", "MG-300");
    std::vector<Bytes> presets;
    int editSlot = editIndex;

    explicit Fixture (qtn::Allowlist allowlist = readAllowlist())
        : session (transport, std::move (allowlist), clock.fn())
    {
        for (int slot = 0; slot < qtn::proto::presetCount; ++slot)
            presets.push_back (presetBytes (slot));

        transport.onReceive = [this] (const Bytes& raw)
        { connector.handleMessage (raw); };
        transport.onConnectionChanged = [this] (bool connected)
        { connector.handleConnectionChanged (connected); };
        connector.onConnected = [this] (const qtn::DeviceInfo& i, const juce::File& folder)
        {
            ++connectedCount;
            info = i;
            savedFolder = folder;
        };
        connector.onFailed = [this] (const juce::String& why)
        { failures.push_back (why); };
        connector.onProgress = [this] (int done, int total)
        { progress.emplace_back (done, total); };
    }

    /** The valid reply to one of the four connect requests. */
    Bytes replyTo (const Bytes& sent) const
    {
        if (sent == versionRequest)
            return version;
        if (sent[7] == 0x19)
            return globalReply();
        if (sent[7] == 0x07)
            return presetReply (sent[8], presets[sent[8]]);
        return presetMessage (0x12, editSlot, editBytes);
    }

    /** Answers the newest request once. */
    void answer()
    {
        REQUIRE_FALSE (transport.sent.empty());
        transport.deliver (replyTo (transport.sent.back()));
    }

    /** Answers the newest request `count` times in a row, stopping early when an answer does
        not lead to a new request. After answerUntil (n) from a fresh start, n requests are
        answered and request n + 1 (if any) is waiting. */
    void answerUntil (size_t count)
    {
        for (size_t i = 0; i < count && ! transport.sent.empty(); ++i)
        {
            const auto before = transport.sent.size();
            answer();
            if (transport.sent.size() == before)
                return;
        }
    }

    void runToEnd() { answerUntil (totalRequests); }

    void advance (juce::int64 delta)
    {
        clock.ms += delta;
        connector.tick();
    }

    /** True when writes are still gated by the missing backup. Probes in dry-run so nothing is
        sent; needs a connected transport (the session checks the connection first). */
    bool writesBlocked()
    {
        REQUIRE (session.setPolicy (qtn::WritePolicy::LiveEditsOnly));
        session.setDryRun (true);
        const auto result = session.submit (liveEdit);
        session.setDryRun (false);
        return result == SendResult::BackupMissing;
    }

    /** Plugs the transport back in without the connector seeing it, to probe the session. */
    void reconnectSilently()
    {
        transport.onConnectionChanged = nullptr;
        transport.setConnected (true);
    }
};

qtn::Bytes readWholeFile (const std::string& path)
{
    std::ifstream in (path, std::ios::binary);
    return {std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>()};
}
} // namespace

// --- Version reply ---------------------------------------------------------------------------

TEST_CASE ("version_reply_parses_firmware_and_name")
{
    const auto info = qtn::parseVersionReply (versionReply ("V1.0.6", "MG-300"));
    REQUIRE (info.has_value());
    CHECK (info->firmware == "V1.0.6");
    CHECK (info->name == "MG-300");
    CHECK (info->raw.size() == 48);
}

TEST_CASE ("version_reply_rejects_other_messages")
{
    CHECK_FALSE (qtn::parseVersionReply (globalReply()).has_value());
    CHECK_FALSE (qtn::parseVersionReply (presetReply (0, presetBytes (0))).has_value());
    auto shortReply = versionReply ("V1", "MG");
    shortReply.erase (shortReply.begin() + 10);
    CHECK_FALSE (qtn::parseVersionReply (shortReply).has_value());
}

// --- Sequence order and bytes ----------------------------------------------------------------

TEST_CASE ("connect_starts_with_version_request")
{
    Fixture f;
    f.connector.start();
    REQUIRE (f.transport.sent.size() == 1);
    CHECK (f.transport.sent[0] == versionRequest);
}

TEST_CASE ("connect_sends_one_request_at_a_time")
{
    Fixture f;
    f.connector.start();
    CHECK (f.transport.sent.size() == 1);
    f.answer();
    CHECK (f.transport.sent.size() == 2);
    f.answer();
    CHECK (f.transport.sent.size() == 3);
}

TEST_CASE ("connect_sequence_order_and_bytes")
{
    Fixture f;
    f.connector.start();
    f.runToEnd();

    REQUIRE (f.transport.sent.size() == totalRequests);
    CHECK (f.transport.sent[0] == versionRequest);
    CHECK (f.transport.sent[1] == request (0x19, 0x00));
    for (int slot = 0; slot < qtn::proto::presetCount; ++slot)
    {
        CAPTURE (slot);
        CHECK (f.transport.sent[static_cast<size_t> (2 + slot)] == request (0x07, static_cast<uint8_t> (slot)));
    }
    CHECK (f.transport.sent[74] == request (0x13, 0x00));
}

TEST_CASE ("connect_requests_go_through_device_session")
{
    Fixture f;
    f.connector.start();
    f.runToEnd();

    const auto& log = f.session.log();
    REQUIRE (log.size() == totalRequests);
    REQUIRE (f.transport.sent.size() == totalRequests);
    for (size_t i = 0; i < log.size(); ++i)
    {
        CAPTURE (i);
        CHECK (log[i].result == SendResult::Sent);
        CHECK (log[i].cmd.bytes == f.transport.sent[i]);
    }
    CHECK (log.front().cmd.name == "getVersion");
    CHECK (log[1].cmd.name == "getGlobleParameter");
    CHECK (log[2].cmd.name == "getOnePreset");
    CHECK (log.back().cmd.name == "getCurrentPreset");
}

TEST_CASE ("connect_fails_when_session_refuses_request")
{
    Fixture f (qtn::Allowlist::fromJson ("[]"));
    f.connector.start();
    CHECK (f.transport.sent.empty());
    CHECK (f.failures.size() == 1);
    f.advance (5000);
    CHECK (f.failures.size() == 1);
}

TEST_CASE ("connect_fails_at_once_in_dry_run")
{
    Fixture f;
    f.session.setDryRun (true);
    f.connector.start();
    CHECK (f.transport.sent.empty());
    CHECK (f.failures.size() == 1);
}

TEST_CASE ("connect_reports_progress_per_reply")
{
    Fixture f;
    f.connector.start();
    f.runToEnd();

    REQUIRE (f.progress.size() == totalRequests);
    CHECK (f.progress.front() == std::pair{1, totalRequests});
    CHECK (f.progress.back() == std::pair{totalRequests, totalRequests});
}

// --- Timeout: never resend --------------------------------------------------------------------

TEST_CASE ("request_times_out_without_reply")
{
    Fixture f;
    f.connector.start();

    f.advance (999);
    CHECK (f.failures.empty());
    f.advance (1);
    CHECK (f.failures.size() == 1);

    f.advance (1000);
    f.advance (5000);
    CHECK (f.failures.size() == 1);
    CHECK (f.transport.sent.size() == 1); // never resent
    CHECK (f.connectedCount == 0);
    CHECK (f.writesBlocked());
}

TEST_CASE ("timeout_restarts_for_each_request")
{
    Fixture f;
    f.connector.start();
    f.advance (900);
    f.answer();
    f.advance (999);
    CHECK (f.failures.empty());
    f.advance (1);
    CHECK (f.failures.size() == 1);
    CHECK (f.transport.sent.size() == 2);
}

TEST_CASE ("timeout_mid_presets_sends_nothing_more")
{
    Fixture f;
    f.connector.start();
    f.answerUntil (20); // 20 answered, request 21 waiting
    REQUIRE (f.transport.sent.size() == 21);
    const auto sentBefore = f.transport.sent.size();

    f.advance (1000);
    REQUIRE (f.failures.size() == 1);
    f.transport.deliver (f.replyTo (f.transport.sent.back())); // late reply
    f.advance (10'000);

    CHECK (f.transport.sent.size() == sentBefore);
    CHECK (f.failures.size() == 1);
    CHECK (f.connectedCount == 0);
    CHECK (f.store.list().empty());
}

// --- Disconnect / reconnect ------------------------------------------------------------------

TEST_CASE ("dump_incomplete_on_disconnect")
{
    Fixture f;
    f.connector.start();
    f.answerUntil (12); // 12 answered, request 13 (preset slot 10) waiting
    const auto sentBefore = f.transport.sent.size();
    REQUIRE (sentBefore == 13);

    f.transport.setConnected (false);
    CHECK (f.failures.size() == 1);

    f.transport.deliver (f.replyTo (f.transport.sent.back()));
    f.advance (5000);
    CHECK (f.transport.sent.size() == sentBefore);
    CHECK (f.failures.size() == 1);
    CHECK (f.connectedCount == 0);
    CHECK (f.store.list().empty());
    f.reconnectSilently();
    CHECK (f.writesBlocked());
}

TEST_CASE ("disconnect_after_connect_clears_backup_done_silently")
{
    Fixture f;
    f.connector.start();
    f.runToEnd();
    REQUIRE (f.connectedCount == 1);

    f.transport.setConnected (false);
    CHECK (f.failures.empty());
    f.reconnectSilently();
    CHECK (f.writesBlocked());
}

TEST_CASE ("reconnect_triggers_handshake_again")
{
    Fixture f;
    f.connector.start();
    f.runToEnd();
    REQUIRE (f.connectedCount == 1);
    REQUIRE_FALSE (f.writesBlocked());

    f.transport.setConnected (false);
    f.clock.ms += 2000;              // a new backup folder name
    f.presets[0] = presetBytes (50); // new content, so a new backup is written
    f.transport.setConnected (true);

    REQUIRE (f.transport.sent.size() == totalRequests + 1);
    CHECK (f.transport.sent.back() == versionRequest);
    CHECK (f.writesBlocked());

    f.answerUntil (totalRequests);
    CHECK (f.transport.sent.size() == 2 * totalRequests);
    CHECK (f.connectedCount == 2);
    CHECK (f.store.list().size() == 2);
    CHECK_FALSE (f.writesBlocked());
}

TEST_CASE ("start_clears_backup_done")
{
    Fixture f;
    f.connector.start();
    f.runToEnd();
    REQUIRE_FALSE (f.writesBlocked());

    f.connector.start();
    CHECK (f.writesBlocked());
}

// --- Unsolicited / unexpected messages --------------------------------------------------------

TEST_CASE ("unsolicited_messages_are_ignored")
{
    Fixture f;
    f.connector.start();
    f.answerUntil (5); // waiting for preset slot 3
    const auto sentBefore = f.transport.sent.size();
    const auto progressBefore = f.progress.size();

    f.transport.deliver ({0xC0, 0x05});                                           // program change
    f.transport.deliver ({0xB0, 0x3C, 0x07});                                     // knob/CC
    f.transport.deliver (editReply());                                            // preset changed on pedal
    f.transport.deliver (presetReply (9, presetBytes (40)));                      // reply for another slot
    f.transport.deliver (versionReply ("X", "Y"));                                // wrong kind
    f.transport.deliver ({0xF0, 0x00, 0x11, 0x22, 0x66, 0x00, 0x70, 0x17, 0xF7}); // garbage
    f.transport.deliver ({});

    CHECK (f.transport.sent.size() == sentBefore);
    CHECK (f.progress.size() == progressBefore);
    CHECK (f.failures.empty());

    f.runToEnd();
    CHECK (f.connectedCount == 1);
    REQUIRE (f.state.preset (3).has_value());
    CHECK (f.state.preset (3)->encode() == presetBytes (3));
}

TEST_CASE ("preset_reply_with_8bit_data_fails")
{
    Fixture f;
    f.presets[3][10] = 0x80;
    f.connector.start();
    f.runToEnd();

    CHECK (f.failures.size() == 1);
    CHECK (f.connectedCount == 0);
    CHECK (f.transport.sent.size() == 2 + 4); // stopped at slot 3
    CHECK (f.store.list().empty());
}

// --- Success: state and backup ----------------------------------------------------------------

TEST_CASE ("connect_success_reports_device_info")
{
    Fixture f;
    f.connector.start();
    f.runToEnd();

    CHECK (f.failures.empty());
    REQUIRE (f.connectedCount == 1);
    CHECK (f.info->firmware == "V1.0.6");
    CHECK (f.info->name == "MG-300");
}

TEST_CASE ("connect_success_fills_device_state")
{
    Fixture f;
    int changes = 0;
    f.state.onChange = [&]
    { ++changes; };
    f.connector.start();
    f.runToEnd();

    for (int slot = 0; slot < qtn::proto::presetCount; ++slot)
    {
        CAPTURE (slot);
        REQUIRE (f.state.preset (slot).has_value());
        CHECK (f.state.preset (slot)->encode() == presetBytes (slot));
    }
    CHECK (f.state.globalParams() == globalReply());
    REQUIRE (f.state.editBuffer().has_value());
    CHECK (f.state.editBuffer()->encode() == editBytes);
    CHECK (f.state.currentSlot() == editIndex);
    CHECK (changes >= 1);
}

TEST_CASE ("device_state_cleared_when_sequence_restarts")
{
    Fixture f;
    f.connector.start();
    f.runToEnd();
    REQUIRE (f.state.preset (0).has_value());

    f.connector.start();
    CHECK_FALSE (f.state.preset (0).has_value());
    CHECK_FALSE (f.state.editBuffer().has_value());
    CHECK (f.state.globalParams().empty());
}

TEST_CASE ("connect_success_saves_backup_and_marks_done")
{
    Fixture f;
    f.connector.start();
    CHECK (f.writesBlocked());
    f.answerUntil (totalRequests - 1);
    CHECK (f.store.list().empty()); // nothing saved before the last reply
    f.runToEnd();

    const auto backups = f.store.list();
    REQUIRE (backups.size() == 1);
    const auto blobs = f.store.load (backups.front());
    REQUIRE (blobs.size() == static_cast<size_t> (qtn::proto::presetCount + 2));
    for (int slot = 0; slot < qtn::proto::presetCount; ++slot)
    {
        CAPTURE (slot);
        CHECK (blobs[static_cast<size_t> (slot)].slot == slot);
        CHECK (blobs[static_cast<size_t> (slot)].raw == presetBytes (slot));
    }
    CHECK (blobs[72].slot == qtn::Connector::globalParamsBackupSlot);
    CHECK (blobs[72].raw == globalReply());
    CHECK (blobs[73].slot == qtn::Connector::editBufferBackupSlot);
    CHECK (blobs[73].raw == editBytes);
    CHECK_FALSE (f.writesBlocked());
}

TEST_CASE ("backup_failure_keeps_writes_blocked")
{
    Fixture f;
    f.dir.makeUnwritable();
    f.connector.start();
    f.runToEnd();

    CHECK (f.connectedCount == 0);
    CHECK (f.failures.size() == 1);
    CHECK (f.writesBlocked());
}

TEST_CASE ("connect_happy_path_with_real_backup")
{
    const std::string path = QTN_G0_BACKUP;
    if (path.empty() || ! fs::exists (path))
        SKIP ("QTN_G0_BACKUP not set or file missing");

    const auto parsed = qtn::PresetFile::read (readWholeFile (path));
    REQUIRE (parsed.contents.has_value());
    const auto& records = parsed.contents->records;
    REQUIRE (records.size() == 36);

    Fixture f;
    // The export holds the 36 user slots; slots 36-71 reuse them (record slot % 36).
    for (int slot = 0; slot < qtn::proto::presetCount; ++slot)
        f.presets[static_cast<size_t> (slot)] = records[static_cast<size_t> (slot % 36)].preset.encode();

    f.connector.start();
    f.runToEnd();

    REQUIRE (f.failures.empty());
    REQUIRE (f.connectedCount == 1);
    const auto backups = f.store.list();
    REQUIRE (backups.size() == 1);
    const auto blobs = f.store.load (backups.front());
    REQUIRE (blobs.size() == 74);
    for (int slot = 0; slot < qtn::proto::presetCount; ++slot)
    {
        CAPTURE (slot);
        const auto& record = records[static_cast<size_t> (slot % 36)];
        CHECK (blobs[static_cast<size_t> (slot)].raw == record.preset.encode());
        CHECK (blobs[static_cast<size_t> (slot)].name == record.preset.name().value_or (""));
        REQUIRE (f.state.preset (slot).has_value());
        CHECK (f.state.preset (slot)->name() == record.preset.name());
    }
    CHECK_FALSE (f.writesBlocked());
}

// --- Review fixes (F1) --------------------------------------------------------------------------

TEST_CASE ("pedal_save_during_read_fails_once")
{
    // A preset saved on the pedal mid-read may change a slot already read: the backup would not
    // be one consistent snapshot.
    Fixture f;
    f.connector.start();
    f.answerUntil (5); // waiting for preset slot 3
    const auto sentBefore = f.transport.sent.size();

    f.transport.deliver (presetMessage (0x0E, 1, presetBytes (40)));

    CHECK (f.failures.size() == 1);
    CHECK_FALSE (f.connector.isRunning());
    f.transport.deliver (f.replyTo (f.transport.sent.back())); // late reply
    f.transport.deliver (presetMessage (0x0E, 1, presetBytes (40)));
    f.advance (5000);
    CHECK (f.failures.size() == 1);
    CHECK (f.transport.sent.size() == sentBefore); // nothing resent
    CHECK (f.connectedCount == 0);
    CHECK (f.store.list().empty());
    CHECK (f.writesBlocked());
}

TEST_CASE ("pedal_save_while_idle_is_ignored")
{
    Fixture f;
    f.connector.start();
    f.runToEnd();
    REQUIRE (f.connectedCount == 1);

    f.transport.deliver (presetMessage (0x0E, 1, presetBytes (40)));
    CHECK (f.failures.empty());
    CHECK_FALSE (f.writesBlocked());
}

TEST_CASE ("edit_buffer_with_slot_out_of_range_fails")
{
    Fixture f;
    f.editSlot = 72;
    f.connector.start();
    f.runToEnd();

    CHECK (f.failures.size() == 1);
    CHECK (f.connectedCount == 0);
    CHECK (f.store.list().empty());
    CHECK (f.state.currentSlot() == -1);
    CHECK (f.writesBlocked());
}

TEST_CASE ("edit_buffer_with_last_slot_is_accepted")
{
    Fixture f;
    f.editSlot = 71;
    f.connector.start();
    f.runToEnd();

    CHECK (f.failures.empty());
    CHECK (f.state.currentSlot() == 71);
}

TEST_CASE ("connected_reports_the_saved_backup_folder")
{
    Fixture f;
    f.connector.start();
    f.runToEnd();

    REQUIRE (f.savedFolder.has_value());
    const auto backups = f.store.list();
    REQUIRE (backups.size() == 1);
    CHECK (*f.savedFolder == backups.front());
}

TEST_CASE ("unchanged_pedal_reuses_backup_and_marks_done")
{
    Fixture f;
    f.connector.start();
    f.runToEnd();
    REQUIRE (f.savedFolder.has_value());
    const auto first = *f.savedFolder;

    f.transport.setConnected (false);
    f.clock.ms += 2000;
    f.transport.setConnected (true);
    f.runToEnd();

    CHECK (f.connectedCount == 2);
    CHECK (f.savedFolder == first);
    CHECK (f.store.list().size() == 1);
    CHECK_FALSE (f.writesBlocked());
}
