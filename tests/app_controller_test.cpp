#include "app/AppController.h"
#include "device/DeviceSession.h"
#include "support/FakeTransport.h"
#include "support/PedalReplies.h"
#include "support/TempDir.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>

namespace
{
using qtn::Bytes;
using qtn::test::FakeTransport;
using qtn::test::TempDir;
namespace replies = qtn::test::replies;

const Bytes versionRequest{0xF0, 0x00, 0x11, 0x22, 0x66, 0x00, 0x00, 0x00, 0x00, 0xF7};

/** The four read commands of the connect sequence plus liveEdit, so a test can prove the
    policy (not the allowlist) blocks a live edit. */
qtn::Allowlist readAllowlist()
{
    return qtn::Allowlist::fromJson (
        R"(["getVersion", "getGlobleParameter", "getOnePreset", "getCurrentPreset", "liveEdit"])");
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

struct Fixture
{
    TempDir dir;
    FakeTransport transport;
    FakeClock clock;
    qtn::AppController controller;
    int statusChanges = 0;

    explicit Fixture (qtn::Allowlist allowlist = readAllowlist(), bool startConnected = false)
        : transport (startConnected),
          controller (transport, std::move (allowlist), dir.get().getChildFile ("backups"), clock.fn())
    {
        controller.onStatusChanged = [this]
        { ++statusChanges; };
        controller.begin();
    }

    void plugIn() { transport.setConnected (true); }
    void readAll() { replies::answerAll (transport); }
};
} // namespace

TEST_CASE ("controller_starts_connect_sequence_on_connection")
{
    Fixture f;
    CHECK (f.transport.sent.empty());
    CHECK (f.controller.status().connection == "Not connected");

    f.plugIn();

    REQUIRE (f.transport.sent.size() == 1);
    CHECK (f.transport.sent[0] == versionRequest);
    const auto status = f.controller.status();
    CHECK (status.connection == "Connected");
    CHECK (status.progress == "0/75");
}

TEST_CASE ("controller_starts_when_pedal_already_connected_at_begin")
{
    Fixture f (readAllowlist(), true);
    REQUIRE (f.transport.sent.size() == 1);
    CHECK (f.transport.sent[0] == versionRequest);
}

TEST_CASE ("controller_shows_refusal_by_empty_allowlist_and_sends_nothing")
{
    Fixture f (qtn::Allowlist::fromJson ("[]"));
    f.plugIn();

    CHECK (f.transport.sent.empty());
    const auto status = f.controller.status();
    CHECK (status.result == "getVersion was not sent: not allowlisted");
    CHECK (status.presetNames.empty());
    CHECK_FALSE (status.backupFolder.has_value());
    CHECK_FALSE (f.controller.latestBackup().has_value());
}

TEST_CASE ("controller_success_shows_device_names_and_backup_path")
{
    Fixture f;
    f.plugIn();
    f.readAll();

    REQUIRE (f.transport.sent.size() == 75);
    const auto status = f.controller.status();
    CHECK (status.device == "Firmware V1.0.6, name MG-300");
    CHECK (status.progress == "75/75");

    REQUIRE (status.presetNames.size() == 72);
    CHECK (status.presetNames.front() == "00  Tone 0");
    CHECK (status.presetNames.back() == "71  Tone 71");

    REQUIRE (status.backupFolder.has_value());
    CHECK (status.backupFolder->isDirectory());
    CHECK (status.backupFolder->getParentDirectory() == f.dir.get().getChildFile ("backups"));
    CHECK (status.result == "Backup saved: " + status.backupFolder->getFullPathName());
    CHECK (f.controller.latestBackup() == status.backupFolder);
}

TEST_CASE ("controller_reports_progress_while_reading")
{
    Fixture f;
    f.plugIn();
    f.transport.deliver (replies::replyTo (f.transport.sent.back()));
    f.transport.deliver (replies::replyTo (f.transport.sent.back()));
    CHECK (f.controller.status().progress == "2/75");
    CHECK (f.controller.status().presetNames.empty());
}

TEST_CASE ("controller_unplug_clears_state")
{
    Fixture f;
    f.plugIn();
    f.readAll();
    REQUIRE (f.controller.status().presetNames.size() == 72);

    f.transport.setConnected (false);

    const auto status = f.controller.status();
    CHECK (status.connection == "Not connected");
    CHECK (status.device.isEmpty());
    CHECK (status.progress.isEmpty());
    CHECK (status.result.isEmpty());
    CHECK (status.presetNames.empty());
    CHECK_FALSE (status.backupFolder.has_value());
}

TEST_CASE ("controller_unplug_during_read_shows_failure")
{
    Fixture f;
    f.plugIn();
    f.transport.deliver (replies::replyTo (f.transport.sent.back()));

    f.transport.setConnected (false);

    const auto status = f.controller.status();
    CHECK (status.result == "Pedal disconnected while waiting for getGlobleParameter");
    CHECK (status.progress.isEmpty());
    CHECK (status.device.isEmpty());
}

TEST_CASE ("controller_replug_starts_a_fresh_read")
{
    Fixture f (qtn::Allowlist::fromJson ("[]"));
    f.plugIn();
    REQUIRE_FALSE (f.controller.status().result.isEmpty());
    f.transport.setConnected (false);
    f.plugIn();
    CHECK (f.controller.status().result == "getVersion was not sent: not allowlisted");
    CHECK (f.controller.status().progress == "0/75");
}

TEST_CASE ("controller_tick_reports_timeout")
{
    Fixture f;
    f.plugIn();
    f.clock.ms += 999;
    f.controller.tick();
    CHECK (f.controller.status().result.isEmpty());
    f.clock.ms += 1;
    f.controller.tick();
    CHECK (f.controller.status().result == "No reply to getVersion within 1000 ms");
    CHECK (f.transport.sent.size() == 1);
}

TEST_CASE ("controller_policy_is_read_only_and_blocks_live_edit_after_backup")
{
    Fixture f;
    CHECK (f.controller.session().policy() == qtn::WritePolicy::ReadOnly);
    f.plugIn();
    f.readAll();
    REQUIRE (f.controller.status().backupFolder.has_value());
    const auto sentBefore = f.transport.sent.size();

    const qtn::Command liveEdit{"liveEdit", qtn::CommandKind::LiveEdit, -1, {0xF0, 0x01, 0xF7}};
    CHECK (f.controller.session().submit (liveEdit) == qtn::SendResult::BlockedByPolicy);
    CHECK (f.controller.session().policy() == qtn::WritePolicy::ReadOnly);
    CHECK (f.transport.sent.size() == sentBefore);
}

TEST_CASE ("controller_status_always_shows_read_only_label")
{
    Fixture f;
    CHECK (f.controller.status().mode == "READ-ONLY");
    f.plugIn();
    f.readAll();
    CHECK (f.controller.status().mode == "READ-ONLY");
}

TEST_CASE ("controller_notifies_status_changes")
{
    Fixture f;
    const auto before = f.statusChanges;
    f.plugIn();
    CHECK (f.statusChanges > before);
    const auto afterPlug = f.statusChanges;
    f.transport.deliver (replies::replyTo (f.transport.sent.back()));
    CHECK (f.statusChanges > afterPlug);
}

TEST_CASE ("controller_compare_without_backup_says_so")
{
    Fixture f;
    f.controller.compareWith (f.dir.get().getChildFile ("export.nuxpreset"));
    CHECK (f.controller.status().compare == "No backup to compare with yet");
}

TEST_CASE ("controller_compare_reports_result_for_latest_backup")
{
    Fixture f;
    f.plugIn();
    f.readAll();
    // A missing export file: the compare runs against the backup and reports the read error.
    f.controller.compareWith (f.dir.get().getChildFile ("missing.nuxpreset"));
    CHECK (f.controller.status().compare.startsWith ("Compare failed: "));
}
