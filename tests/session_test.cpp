#include "device/DeviceSession.h"
#include "support/FakeTransport.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
using qtn::Command;
using qtn::CommandKind;
using qtn::DeviceSession;
using qtn::SendResult;
using qtn::WritePolicy;
using qtn::test::FakeTransport;

qtn::Allowlist testAllowlist()
{
    return qtn::Allowlist::fromJson (
        R"(["handshake", "getOnePreset", "liveEdit", "savePreset", "renamePreset", "bulkWrite"])");
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

Command cmd (std::string name, CommandKind kind, int slot = -1)
{
    return {std::move (name), kind, slot, {0xF0, 0x01, 0xF7}};
}

const Command readCmd = cmd ("getOnePreset", CommandKind::Read);
const Command liveCmd = cmd ("liveEdit", CommandKind::LiveEdit);
const Command bulkCmd = cmd ("bulkWrite", CommandKind::BulkWrite);

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
    CHECK (f.transport.sent[0] == readCmd.sysex);
}

TEST_CASE ("readonly_blocks_live_edit")
{
    Fixture f;
    f.session.setPolicy (WritePolicy::ReadOnly);
    CHECK (f.session.submit (liveCmd) == SendResult::BlockedByPolicy);
    CHECK (f.transport.sent.empty());
}

TEST_CASE ("live_edits_policy_allows_live_edit")
{
    Fixture f;
    f.session.setPolicy (WritePolicy::LiveEditsOnly);
    CHECK (f.session.policy() == WritePolicy::LiveEditsOnly);
    CHECK (f.session.submit (liveCmd) == SendResult::Sent);
}

TEST_CASE ("live_edits_policy_blocks_save")
{
    Fixture f;
    f.session.setPolicy (WritePolicy::LiveEditsOnly, 7);
    CHECK (f.session.submit (cmd ("savePreset", CommandKind::SavePreset, 7)) == SendResult::BlockedByPolicy);
    CHECK (f.transport.sent.empty());
}

TEST_CASE ("scratch_policy_allows_only_scratch_slot")
{
    Fixture f;
    f.session.setPolicy (WritePolicy::ScratchSlotOnly, 7);
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
    f.session.setPolicy (WritePolicy::Full);
    CHECK (f.session.submit (bulkCmd) == SendResult::Sent);
    CHECK (f.transport.sent.size() == 1);
}

TEST_CASE ("unlisted_command_rejected")
{
    Fixture f;
    f.session.setPolicy (WritePolicy::Full);
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
    session.setPolicy (WritePolicy::Full);

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
    f.session.setPolicy (WritePolicy::Full);
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
    f.session.setPolicy (WritePolicy::Full);

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
    session.setPolicy (WritePolicy::Full);

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
