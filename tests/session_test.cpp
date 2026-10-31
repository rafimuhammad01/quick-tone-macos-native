#include "device/DeviceSession.h"
#include "proto/Codec.h"
#include "support/FakeTransport.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace
{
using qtn::Bytes;
using qtn::Command;
using qtn::CommandKind;
using qtn::DeviceSession;
using qtn::SendResult;
using qtn::WritePolicy;
using qtn::test::FakeTransport;

qtn::Allowlist testAllowlist()
{
    return qtn::Allowlist::fromJson (
        R"(["handshake", "getOnePreset", "liveEdit", "savePreset", "renamePreset", "swapPreset", "bulkWrite",
            "selectPreset", "globalChange"])");
}

/** A clock that only moves when told to. */
struct FakeClock
{
    juce::int64 ms = 1'000'000;
    DeviceSession::Clock fn()
    {
        return [this]
        { return juce::Time (ms); };
    }
};

// Hand-built write frames (no write builders exist in the app). Layouts: R7ab §2a items 6 / 10,
// R7cd §4 item 5. Offsets count from F0.

/** `P 70 0E <slot> <103 preset bytes> F7` (113 bytes). */
Bytes saveFrame (int slot)
{
    Bytes args (1 + qtn::proto::presetSize, 0x00);
    args[0] = static_cast<uint8_t> (slot);
    return qtn::proto::frame (0x0E, args);
}

/** `P 70 23 <slot> <len> <10 codes> F7` (21 bytes). */
Bytes renameFrame (int slot)
{
    Bytes args (12, 0x00);
    args[0] = static_cast<uint8_t> (slot);
    args[1] = 4;
    return qtn::proto::frame (0x23, args);
}

/** `P 70 2C <source> <target> F7` (11 bytes). */
Bytes swapFrame (int source, int target)
{
    return qtn::proto::frame (0x2C, {static_cast<uint8_t> (source), static_cast<uint8_t> (target)});
}

/** Read/Handshake commands carry a real read frame (the gate checks their bytes); save, rename
    and swap carry a frame whose slot bytes match `slot` (swap: from slot + 1 onto slot); every
    other kind carries a placeholder sysex. */
Command cmd (std::string name, CommandKind kind, int slot = -1)
{
    switch (kind)
    {
    case CommandKind::Handshake:
        return {std::move (name), kind, slot, qtn::proto::versionRequestFrame()};
    case CommandKind::Read:
        return {std::move (name), kind, slot, qtn::proto::getOnePreset (0)->bytes};
    case CommandKind::SavePreset:
        return {std::move (name), kind, slot, saveFrame (slot)};
    case CommandKind::RenamePreset:
        return {std::move (name), kind, slot, renameFrame (slot)};
    case CommandKind::SwapPreset:
        return {std::move (name), kind, slot, swapFrame (slot + 1, slot)};
    default:
        return {std::move (name), kind, slot, {0xF0, 0x01, 0xF7}};
    }
}

const Command readCmd = cmd ("getOnePreset", CommandKind::Read);
const Command liveCmd = cmd ("liveEdit", CommandKind::LiveEdit);
const Command bulkCmd = cmd ("bulkWrite", CommandKind::BulkWrite);

// Short Control Change messages (R7ab §2d): preset select is a live edit, master volume a global.
const Command ccSelectCmd{"selectPreset", CommandKind::LiveEdit, -1, {0xB0, 0x3C, 0x05}};
const Command ccGlobalCmd{"globalChange", CommandKind::SystemWrite, -1, {0xB0, 0x40, 0x32}};

/** A session with the backup already done, so only the policy decides. */
struct Fixture
{
    FakeTransport transport;
    FakeClock clock;
    DeviceSession session{transport, testAllowlist(), clock.fn()};
    Fixture() { session.markBackupDone (true); }
};
} // namespace

TEST_CASE ("default_policy_is_readonly")
{
    Fixture f;
    CHECK (f.session.policy() == WritePolicy::ReadOnly);
}

TEST_CASE ("readonly_allows_read")
{
    Fixture f;
    CHECK (f.session.submit (readCmd) == SendResult::Sent);
    CHECK (f.session.submit (cmd ("handshake", CommandKind::Handshake)) == SendResult::Sent);
    REQUIRE (f.transport.sent.size() == 2);
    CHECK (f.transport.sent[0] == readCmd.bytes);
}

TEST_CASE ("readonly_blocks_live_edit")
{
    Fixture f;
    REQUIRE (f.session.setPolicy (WritePolicy::ReadOnly));
    CHECK (f.session.submit (liveCmd) == SendResult::BlockedByPolicy);
    CHECK (f.transport.sent.empty());
}

TEST_CASE ("live_edits_policy_allows_live_edit")
{
    Fixture f;
    REQUIRE (f.session.setPolicy (WritePolicy::LiveEditsOnly));
    CHECK (f.session.policy() == WritePolicy::LiveEditsOnly);
    CHECK (f.session.submit (liveCmd) == SendResult::Sent);
}

TEST_CASE ("live_edits_policy_blocks_save")
{
    Fixture f;
    REQUIRE (f.session.setPolicy (WritePolicy::LiveEditsOnly, 7));
    CHECK (f.session.submit (cmd ("savePreset", CommandKind::SavePreset, 7)) == SendResult::BlockedByPolicy);
    CHECK (f.transport.sent.empty());
}

TEST_CASE ("scratch_policy_allows_only_scratch_slot")
{
    Fixture f;
    REQUIRE (f.session.setPolicy (WritePolicy::ScratchSlotOnly, 7));
    CHECK (f.session.submit (cmd ("savePreset", CommandKind::SavePreset, 7)) == SendResult::Sent);
    CHECK (f.session.submit (cmd ("renamePreset", CommandKind::RenamePreset, 7)) == SendResult::Sent);
    CHECK (f.session.submit (cmd ("savePreset", CommandKind::SavePreset, 8)) == SendResult::BlockedByPolicy);
    CHECK (f.session.submit (cmd ("renamePreset", CommandKind::RenamePreset, 8)) == SendResult::BlockedByPolicy);
    CHECK (f.session.submit (liveCmd) == SendResult::Sent);
    CHECK (f.session.submit (bulkCmd) == SendResult::BlockedByPolicy);
    CHECK (f.transport.sent.size() == 3);
}

TEST_CASE ("full_allows_bulk")
{
    Fixture f;
    REQUIRE (f.session.setPolicy (WritePolicy::Full));
    CHECK (f.session.submit (bulkCmd) == SendResult::Sent);
    CHECK (f.transport.sent.size() == 1);
}

TEST_CASE ("unlisted_command_rejected")
{
    Fixture f;
    REQUIRE (f.session.setPolicy (WritePolicy::Full));
    CHECK (f.session.submit (cmd ("notVerified", CommandKind::Read)) == SendResult::NotAllowlisted);
    CHECK (f.transport.sent.empty());
}

TEST_CASE ("unlisted_check_comes_before_backup_check")
{
    FakeTransport transport;
    DeviceSession session (transport, testAllowlist());
    CHECK (session.submit (cmd ("notVerified", CommandKind::BulkWrite)) == SendResult::NotAllowlisted);
}

TEST_CASE ("writes_blocked_until_backup_done")
{
    FakeTransport transport;
    FakeClock clock;
    DeviceSession session (transport, testAllowlist(), clock.fn());
    REQUIRE (session.setPolicy (WritePolicy::Full));

    CHECK (session.submit (readCmd) == SendResult::Sent);
    CHECK (session.submit (liveCmd) == SendResult::BackupMissing);
    CHECK (session.submit (bulkCmd) == SendResult::BackupMissing);
    CHECK (transport.sent.size() == 1);

    session.markBackupDone (true);
    CHECK (session.submit (bulkCmd) == SendResult::Sent);

    session.markBackupDone (false);
    CHECK (session.submit (liveCmd) == SendResult::BackupMissing);
    CHECK (transport.sent.size() == 2);
}

TEST_CASE ("backup_check_comes_before_policy_check")
{
    FakeTransport transport;
    DeviceSession session (transport, testAllowlist());
    CHECK (session.submit (liveCmd) == SendResult::BackupMissing);
}

TEST_CASE ("dry_run_sends_nothing_but_logs")
{
    Fixture f;
    REQUIRE (f.session.setPolicy (WritePolicy::Full));
    f.session.setDryRun (true);

    CHECK (f.session.submit (bulkCmd) == SendResult::DryRun);
    CHECK (f.transport.sent.empty());
    REQUIRE (f.session.log().size() == 1);
    CHECK (f.session.log()[0].cmd.name == "bulkWrite");
    CHECK (f.session.log()[0].result == SendResult::DryRun);

    f.session.setDryRun (false);
    CHECK (f.session.submit (bulkCmd) == SendResult::Sent);
}

TEST_CASE ("dry_run_does_not_bypass_policy")
{
    Fixture f;
    f.session.setDryRun (true);
    CHECK (f.session.submit (bulkCmd) == SendResult::BlockedByPolicy);
}

TEST_CASE ("log_records_every_submit_with_clock_time")
{
    Fixture f;
    CHECK (f.session.submit (readCmd) == SendResult::Sent);
    f.clock.ms += 250;
    CHECK (f.session.submit (liveCmd) == SendResult::BlockedByPolicy);

    const auto& log = f.session.log();
    REQUIRE (log.size() == 2);
    CHECK (log[0].at == juce::Time (1'000'000));
    CHECK (log[0].result == SendResult::Sent);
    CHECK (log[1].at == juce::Time (1'000'250));
    CHECK (log[1].cmd.name == "liveEdit");
    CHECK (log[1].result == SendResult::BlockedByPolicy);
}

TEST_CASE ("disconnected_returns_disconnected")
{
    FakeTransport transport (false);
    FakeClock clock;
    DeviceSession session (transport, testAllowlist(), clock.fn());

    // Disconnected wins over every other check, even for an unlisted command.
    CHECK (session.submit (readCmd) == SendResult::Disconnected);
    CHECK (session.submit (cmd ("notVerified", CommandKind::Read)) == SendResult::Disconnected);
    CHECK (transport.sent.empty());
    REQUIRE (session.log().size() == 2);
    CHECK (session.log()[0].result == SendResult::Disconnected);
}

TEST_CASE ("disconnect_mid_bulk_aborts_without_retry")
{
    Fixture f;
    REQUIRE (f.session.setPolicy (WritePolicy::Full));

    CHECK (f.session.submit (bulkCmd) == SendResult::Sent);
    CHECK (f.session.submit (bulkCmd) == SendResult::Sent);
    f.transport.setConnected (false);
    CHECK (f.session.submit (bulkCmd) == SendResult::Disconnected);
    CHECK (f.transport.sent.size() == 2);

    // Reconnecting does not replay the failed write, and the policy is unchanged.
    f.transport.setConnected (true);
    CHECK (f.transport.sent.size() == 2);
    CHECK (f.session.policy() == WritePolicy::Full);
}

TEST_CASE ("failed_send_reports_disconnected_and_is_not_retried")
{
    // The link drops between the connection check and the send.
    struct DroppingTransport final : qtn::Transport
    {
        int sendCalls = 0;
        bool isConnected() const override { return true; }
        bool send (const qtn::Bytes&) override
        {
            ++sendCalls;
            return false;
        }
    } transport;

    FakeClock clock;
    DeviceSession session (transport, testAllowlist(), clock.fn());
    session.markBackupDone (true);
    REQUIRE (session.setPolicy (WritePolicy::Full));

    CHECK (session.submit (bulkCmd) == SendResult::Disconnected);
    CHECK (transport.sendCalls == 1);
    REQUIRE (session.log().size() == 1);
    CHECK (session.log()[0].result == SendResult::Disconnected);
}

TEST_CASE ("allowlist_parses_json_names")
{
    const auto list = qtn::Allowlist::fromJson (R"(["getOnePreset", "liveEdit"])");
    CHECK (list.contains ("getOnePreset"));
    CHECK (list.contains ("liveEdit"));
    CHECK_FALSE (list.contains ("savePreset"));
    CHECK_FALSE (list.contains (""));
}

TEST_CASE ("allowlist_empty_or_malformed_json_contains_nothing")
{
    CHECK_FALSE (qtn::Allowlist::fromJson ("[]").contains ("getOnePreset"));
    CHECK_FALSE (qtn::Allowlist::fromJson ("not json").contains ("getOnePreset"));
    CHECK_FALSE (qtn::Allowlist::fromJson (R"({"getOnePreset": 1})").contains ("getOnePreset"));
    const auto mixed = qtn::Allowlist::fromJson (R"([1, "liveEdit", null])");
    CHECK (mixed.contains ("liveEdit"));
    CHECK_FALSE (mixed.contains ("1"));
}

// ---- Short (Control Change) messages ----

TEST_CASE ("cc_live_edit_blocked_in_readonly")
{
    Fixture f;
    CHECK (f.session.submit (ccSelectCmd) == SendResult::BlockedByPolicy);
    CHECK (f.transport.sent.empty());
}

TEST_CASE ("cc_live_edit_sent_in_live_edits_policy")
{
    Fixture f;
    REQUIRE (f.session.setPolicy (WritePolicy::LiveEditsOnly));
    CHECK (f.session.submit (ccSelectCmd) == SendResult::Sent);
    CHECK (f.transport.sent == std::vector<qtn::Bytes>{{0xB0, 0x3C, 0x05}});
    REQUIRE (f.session.log().size() == 1);
    CHECK (f.session.log()[0].cmd.bytes == qtn::Bytes{0xB0, 0x3C, 0x05});
    CHECK (f.session.log()[0].result == SendResult::Sent);
}

TEST_CASE ("cc_global_requires_full_policy")
{
    Fixture f;
    for (const auto p : {WritePolicy::ReadOnly, WritePolicy::LiveEditsOnly, WritePolicy::ScratchSlotOnly})
    {
        REQUIRE (f.session.setPolicy (p, 7));
        CHECK (f.session.submit (ccGlobalCmd) == SendResult::BlockedByPolicy);
    }
    CHECK (f.transport.sent.empty());

    REQUIRE (f.session.setPolicy (WritePolicy::Full));
    CHECK (f.session.submit (ccGlobalCmd) == SendResult::Sent);
    CHECK (f.transport.sent == std::vector<qtn::Bytes>{{0xB0, 0x40, 0x32}});
}

TEST_CASE ("cc_blocked_until_backup_done")
{
    FakeTransport transport;
    FakeClock clock;
    DeviceSession session (transport, testAllowlist(), clock.fn());
    REQUIRE (session.setPolicy (WritePolicy::Full));

    CHECK (session.submit (ccSelectCmd) == SendResult::BackupMissing);
    CHECK (session.submit (ccGlobalCmd) == SendResult::BackupMissing);
    CHECK (transport.sent.empty());

    session.markBackupDone (true);
    CHECK (session.submit (ccSelectCmd) == SendResult::Sent);
}

TEST_CASE ("cc_not_allowlisted_rejected")
{
    Fixture f;
    REQUIRE (f.session.setPolicy (WritePolicy::Full));
    const Command unlisted{"paramChange", CommandKind::LiveEdit, -1, {0xB0, 0x0B, 0x10}};
    CHECK (f.session.submit (unlisted) == SendResult::NotAllowlisted);
    CHECK (f.transport.sent.empty());
}

TEST_CASE ("session_rejects_malformed_bytes")
{
    Fixture f;
    REQUIRE (f.session.setPolicy (WritePolicy::Full));

    const std::vector<qtn::Bytes> bad{{}, {0xB0, 0x3C}, {0xB0, 0x3C, 0x05, 0x00}, {0xB0, 0x3C, 0x80}, {0xF8}, {0xF0, 0x01}};
    for (const auto& b : bad)
        CHECK (f.session.submit ({"selectPreset", CommandKind::LiveEdit, -1, b}) == SendResult::Malformed);

    // Malformed even in dry-run mode, and logged like every other submit.
    f.session.setDryRun (true);
    CHECK (f.session.submit ({"selectPreset", CommandKind::LiveEdit, -1, {0xB0}}) == SendResult::Malformed);
    CHECK (f.transport.sent.empty());
    CHECK (f.session.log().size() == bad.size() + 1);
}

TEST_CASE ("short_message_with_read_kind_rejected")
{
    // A CC changes pedal state, so it may never ride on the Read/Handshake exemption from the
    // backup and policy checks.
    FakeTransport transport;
    DeviceSession session (transport, testAllowlist());
    CHECK (session.submit ({"getOnePreset", CommandKind::Read, -1, {0xB0, 0x3C, 0x05}}) == SendResult::Malformed);
    CHECK (session.submit ({"handshake", CommandKind::Handshake, -1, {0xB0, 0x3C, 0x05}}) == SendResult::Malformed);
    CHECK (transport.sent.empty());
}

// --- Byte-level cross-check of the Read/Handshake label --------------------------------------

TEST_CASE ("mislabelled_save_frame_as_read_is_malformed")
{
    // A `0E` frame (preset save, R3b §3b) labelled as the allowlisted read getOnePreset.
    FakeTransport transport;
    DeviceSession session (transport, testAllowlist());
    Bytes args (1 + qtn::proto::presetSize, 0x00);
    const Command mislabelled{"getOnePreset", CommandKind::Read, -1, qtn::proto::frame (0x0E, args)};

    CHECK (session.submit (mislabelled) == SendResult::Malformed);
    CHECK (transport.sent.empty());
    REQUIRE (session.log().size() == 1);
    CHECK (session.log()[0].result == SendResult::Malformed);
}

TEST_CASE ("read_label_on_unknown_sysex_is_malformed")
{
    FakeTransport transport;
    DeviceSession session (transport, testAllowlist());
    CHECK (session.submit ({"getOnePreset", CommandKind::Read, -1, {0xF0, 0x01, 0xF7}}) == SendResult::Malformed);
    CHECK (session.submit ({"handshake", CommandKind::Handshake, -1, qtn::proto::frame (0x07, {0x00})}) == SendResult::Malformed);
    // A version-request type byte with another command byte is not the version request.
    CHECK (session.submit ({"handshake", CommandKind::Handshake, -1, {0xF0, 0x00, 0x11, 0x22, 0x66, 0x00, 0x00, 0x0E, 0x00, 0xF7}}) == SendResult::Malformed);
    CHECK (transport.sent.empty());
}

TEST_CASE ("mislabel_check_comes_before_connection_check")
{
    FakeTransport transport (false);
    DeviceSession session (transport, testAllowlist());
    CHECK (session.submit ({"getOnePreset", CommandKind::Read, -1, qtn::proto::frame (0x0E, {0x00})}) == SendResult::Malformed);
}

TEST_CASE ("correctly_labelled_reads_are_sent_before_backup")
{
    FakeTransport transport;
    DeviceSession session (transport, qtn::Allowlist::fromJson (R"(["getVersion", "getGlobleParameter", "getOnePreset", "getCurrentPreset"])"));

    CHECK (session.submit (qtn::proto::getVersion()) == SendResult::Sent);
    CHECK (session.submit (qtn::proto::getGlobalParameters()) == SendResult::Sent);
    CHECK (session.submit (*qtn::proto::getOnePreset (71)) == SendResult::Sent);
    CHECK (session.submit (qtn::proto::getCurrentPreset()) == SendResult::Sent);
    CHECK (transport.sent.size() == 4);
}

// --- Scratch slot range and log cap -------------------------------------------------------------

TEST_CASE ("scratch_policy_rejects_slot_out_of_range")
{
    Fixture f;
    REQUIRE (f.session.setPolicy (WritePolicy::LiveEditsOnly));

    CHECK_FALSE (f.session.setPolicy (WritePolicy::ScratchSlotOnly, -1));
    CHECK_FALSE (f.session.setPolicy (WritePolicy::ScratchSlotOnly, 36));
    CHECK (f.session.policy() == WritePolicy::LiveEditsOnly); // unchanged

    CHECK (f.session.setPolicy (WritePolicy::ScratchSlotOnly, 0));
    CHECK (f.session.setPolicy (WritePolicy::ScratchSlotOnly, 35));
    CHECK (f.session.policy() == WritePolicy::ScratchSlotOnly);
    CHECK (f.session.submit (cmd ("savePreset", CommandKind::SavePreset, 35)) == SendResult::Sent);
}

TEST_CASE ("log_keeps_only_newest_entries")
{
    Fixture f;
    const auto cap = DeviceSession::maxLogEntries;
    for (size_t i = 0; i < cap + 5; ++i)
    {
        f.clock.ms = static_cast<juce::int64> (i);
        CHECK (f.session.submit (liveCmd) == SendResult::BlockedByPolicy);
    }

    const auto& log = f.session.log();
    REQUIRE (log.size() == cap);
    CHECK (log.front().at == juce::Time (5));
    CHECK (log.back().at == juce::Time (static_cast<juce::int64> (cap + 4)));
}

// --- Exact read frames ---------------------------------------------------------------------------

TEST_CASE ("read_frame_with_trailing_bytes_is_malformed")
{
    FakeTransport transport;
    DeviceSession session (transport, qtn::Allowlist::fromJson (R"(["getVersion", "getGlobleParameter", "getOnePreset", "getCurrentPreset"])"));

    CHECK (session.submit ({"getVersion", CommandKind::Handshake, -1, {0xF0, 0x00, 0x11, 0x22, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF7}}) == SendResult::Malformed);
    CHECK (session.submit ({"getGlobleParameter", CommandKind::Read, -1, qtn::proto::frame (0x19, {0x00, 0x00})}) == SendResult::Malformed);
    CHECK (session.submit ({"getOnePreset", CommandKind::Read, -1, qtn::proto::frame (0x07, {0x05, 0x00})}) == SendResult::Malformed);
    CHECK (session.submit ({"getCurrentPreset", CommandKind::Read, -1, qtn::proto::frame (0x13, {0x00, 0x0E})}) == SendResult::Malformed);
    CHECK (transport.sent.empty());
}

TEST_CASE ("read_frame_with_wrong_argument_is_malformed")
{
    FakeTransport transport;
    DeviceSession session (transport, qtn::Allowlist::fromJson (R"(["getVersion", "getGlobleParameter", "getOnePreset", "getCurrentPreset"])"));

    CHECK (session.submit ({"getVersion", CommandKind::Handshake, -1, {0xF0, 0x00, 0x11, 0x22, 0x66, 0x00, 0x00, 0x00, 0x01, 0xF7}}) == SendResult::Malformed);
    CHECK (session.submit ({"getGlobleParameter", CommandKind::Read, -1, qtn::proto::frame (0x19, {0x01})}) == SendResult::Malformed);
    CHECK (session.submit ({"getCurrentPreset", CommandKind::Read, -1, qtn::proto::frame (0x13, {0x23})}) == SendResult::Malformed);
    // Too short: no argument byte at all.
    CHECK (session.submit ({"getGlobleParameter", CommandKind::Read, -1, qtn::proto::frame (0x19, {})}) == SendResult::Malformed);
    CHECK (session.submit ({"getOnePreset", CommandKind::Read, -1, qtn::proto::frame (0x07, {})}) == SendResult::Malformed);
    CHECK (transport.sent.empty());
}

TEST_CASE ("read_preset_slot_72_is_malformed")
{
    FakeTransport transport;
    DeviceSession session (transport, testAllowlist());

    CHECK (session.submit ({"getOnePreset", CommandKind::Read, -1, qtn::proto::frame (0x07, {72})}) == SendResult::Malformed);
    CHECK (session.submit ({"getOnePreset", CommandKind::Read, -1, qtn::proto::frame (0x07, {0x7F})}) == SendResult::Malformed);
    CHECK (transport.sent.empty());

    CHECK (session.submit ({"getOnePreset", CommandKind::Read, -1, qtn::proto::frame (0x07, {0})}) == SendResult::Sent);
    CHECK (session.submit ({"getOnePreset", CommandKind::Read, -1, qtn::proto::frame (0x07, {71})}) == SendResult::Sent);
    CHECK (transport.sent.size() == 2);
}

// --- Slot bytes of save / rename / swap ----------------------------------------------------------

TEST_CASE ("save_bytes_slot_must_match_target_slot")
{
    Fixture f;
    REQUIRE (f.session.setPolicy (WritePolicy::Full));

    CHECK (f.session.submit ({"savePreset", CommandKind::SavePreset, 7, saveFrame (8)}) == SendResult::Malformed);
    CHECK (f.session.submit ({"savePreset", CommandKind::SavePreset, -1, saveFrame (8)}) == SendResult::Malformed);
    CHECK (f.transport.sent.empty());

    CHECK (f.session.submit ({"savePreset", CommandKind::SavePreset, 8, saveFrame (8)}) == SendResult::Sent);
    CHECK (f.transport.sent == std::vector<Bytes>{saveFrame (8)});
}

TEST_CASE ("scratch_policy_never_sends_save_to_other_slot")
{
    // Labelled for the scratch slot, but the bytes write slot 8.
    Fixture f;
    REQUIRE (f.session.setPolicy (WritePolicy::ScratchSlotOnly, 7));

    CHECK (f.session.submit ({"savePreset", CommandKind::SavePreset, 7, saveFrame (8)}) == SendResult::Malformed);
    CHECK (f.session.submit ({"renamePreset", CommandKind::RenamePreset, 7, renameFrame (8)}) == SendResult::Malformed);
    CHECK (f.transport.sent.empty());

    CHECK (f.session.submit ({"savePreset", CommandKind::SavePreset, 7, saveFrame (7)}) == SendResult::Sent);
    CHECK (f.transport.sent == std::vector<Bytes>{saveFrame (7)});
}

TEST_CASE ("rename_bytes_slot_must_match_target_slot")
{
    Fixture f;
    REQUIRE (f.session.setPolicy (WritePolicy::Full));

    CHECK (f.session.submit ({"renamePreset", CommandKind::RenamePreset, 3, renameFrame (4)}) == SendResult::Malformed);
    CHECK (f.transport.sent.empty());
    CHECK (f.session.submit ({"renamePreset", CommandKind::RenamePreset, 4, renameFrame (4)}) == SendResult::Sent);
}

TEST_CASE ("swap_target_byte_must_match_target_slot")
{
    // targetSlot of a swap is the drop target raw[9] (it becomes the current preset, R7ab item 10).
    Fixture f;
    REQUIRE (f.session.setPolicy (WritePolicy::Full));

    CHECK (f.session.submit ({"swapPreset", CommandKind::SwapPreset, 7, swapFrame (7, 8)}) == SendResult::Malformed);
    CHECK (f.transport.sent.empty());
    CHECK (f.session.submit ({"swapPreset", CommandKind::SwapPreset, 7, swapFrame (8, 7)}) == SendResult::Sent);
}

TEST_CASE ("swap_source_must_be_another_user_slot")
{
    Fixture f;
    REQUIRE (f.session.setPolicy (WritePolicy::Full));

    CHECK (f.session.submit ({"swapPreset", CommandKind::SwapPreset, 7, swapFrame (7, 7)}) == SendResult::Malformed);
    CHECK (f.session.submit ({"swapPreset", CommandKind::SwapPreset, 7, swapFrame (36, 7)}) == SendResult::Malformed);
    CHECK (f.session.submit ({"swapPreset", CommandKind::SwapPreset, 7, swapFrame (71, 7)}) == SendResult::Malformed);
    CHECK (f.transport.sent.empty());
    CHECK (f.session.submit ({"swapPreset", CommandKind::SwapPreset, 0, swapFrame (35, 0)}) == SendResult::Sent);
}

TEST_CASE ("slot_writes_to_factory_slots_are_malformed")
{
    // QuickTone never writes slots 36-71 (R7ab §1, Q9: never tried on the pedal).
    Fixture f;
    REQUIRE (f.session.setPolicy (WritePolicy::Full));

    CHECK (f.session.submit ({"savePreset", CommandKind::SavePreset, 36, saveFrame (36)}) == SendResult::Malformed);
    CHECK (f.session.submit ({"renamePreset", CommandKind::RenamePreset, 40, renameFrame (40)}) == SendResult::Malformed);
    CHECK (f.session.submit ({"swapPreset", CommandKind::SwapPreset, 36, swapFrame (0, 36)}) == SendResult::Malformed);
    CHECK (f.transport.sent.empty());
    CHECK (f.session.submit ({"savePreset", CommandKind::SavePreset, 35, saveFrame (35)}) == SendResult::Sent);
}

TEST_CASE ("swap_blocked_under_scratch_policy")
{
    // A swap rewrites two slots, so it can never be confined to the one scratch slot.
    Fixture f;
    REQUIRE (f.session.setPolicy (WritePolicy::ScratchSlotOnly, 7));

    CHECK (f.session.submit ({"swapPreset", CommandKind::SwapPreset, 7, swapFrame (8, 7)}) == SendResult::BlockedByPolicy);
    CHECK (f.session.submit ({"swapPreset", CommandKind::SwapPreset, 8, swapFrame (7, 8)}) == SendResult::BlockedByPolicy);
    CHECK (f.transport.sent.empty());
}

TEST_CASE ("slot_write_kind_needs_its_own_frame")
{
    Fixture f;
    REQUIRE (f.session.setPolicy (WritePolicy::Full));

    // Another command's frame, a placeholder, a CC, and frames of the wrong length.
    Bytes shortSave = saveFrame (7);
    shortSave.erase (shortSave.begin() + 20, shortSave.end() - 1);
    Bytes longRename = renameFrame (7);
    longRename.insert (longRename.end() - 1, 0x00);
    Bytes longSwap = swapFrame (8, 7);
    longSwap.insert (longSwap.end() - 1, 0x00);

    const std::vector<Command> bad{
        {"savePreset", CommandKind::SavePreset, 7, renameFrame (7)},
        {"savePreset", CommandKind::SavePreset, 7, {0xF0, 0x01, 0xF7}},
        {"savePreset", CommandKind::SavePreset, 7, {0xB0, 0x3C, 0x07}},
        {"savePreset", CommandKind::SavePreset, 7, shortSave},
        {"renamePreset", CommandKind::RenamePreset, 7, saveFrame (7)},
        {"renamePreset", CommandKind::RenamePreset, 7, longRename},
        {"swapPreset", CommandKind::SwapPreset, 7, saveFrame (7)},
        {"swapPreset", CommandKind::SwapPreset, 7, longSwap},
    };
    for (const auto& c : bad)
    {
        CAPTURE (c.name, c.bytes.size());
        CHECK (f.session.submit (c) == SendResult::Malformed);
    }
    CHECK (f.transport.sent.empty());
}

TEST_CASE ("slot_write_frame_with_other_label_is_malformed")
{
    // A save / rename / swap frame may not ride on a weaker label (e.g. LiveEdit under
    // LiveEditsOnly), which would skip the slot check.
    Fixture f;
    REQUIRE (f.session.setPolicy (WritePolicy::LiveEditsOnly));

    CHECK (f.session.submit ({"liveEdit", CommandKind::LiveEdit, -1, saveFrame (8)}) == SendResult::Malformed);
    CHECK (f.session.submit ({"liveEdit", CommandKind::LiveEdit, -1, renameFrame (8)}) == SendResult::Malformed);
    CHECK (f.session.submit ({"liveEdit", CommandKind::LiveEdit, -1, swapFrame (8, 7)}) == SendResult::Malformed);
    REQUIRE (f.session.setPolicy (WritePolicy::Full));
    CHECK (f.session.submit ({"globalChange", CommandKind::SystemWrite, -1, saveFrame (8)}) == SendResult::Malformed);
    CHECK (f.session.submit ({"renamePreset", CommandKind::RenamePreset, 8, saveFrame (8)}) == SendResult::Malformed);
    CHECK (f.transport.sent.empty());
}

TEST_CASE ("bulk_write_may_carry_save_frames_to_user_slots")
{
    // Import-all is a chain of `0E` frames (R7ab item 9); each still writes a user slot only.
    Fixture f;
    REQUIRE (f.session.setPolicy (WritePolicy::Full));

    CHECK (f.session.submit ({"bulkWrite", CommandKind::BulkWrite, -1, saveFrame (36)}) == SendResult::Malformed);
    CHECK (f.transport.sent.empty());
    CHECK (f.session.submit ({"bulkWrite", CommandKind::BulkWrite, -1, saveFrame (0)}) == SendResult::Sent);
    CHECK (f.session.submit ({"bulkWrite", CommandKind::BulkWrite, -1, saveFrame (35)}) == SendResult::Sent);
}
