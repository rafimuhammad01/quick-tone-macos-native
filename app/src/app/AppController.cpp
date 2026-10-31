#include "app/AppController.h"

#include "backup/ExportCompare.h"

#include <utility>

namespace qtn
{
AppController::AppController (Transport& t, Allowlist allowlist, juce::File backupRoot, Clock c)
    : transport (t),
      deviceSession (t, std::move (allowlist), c),
      store (std::move (backupRoot)),
      clock (std::move (c)),
      connector (deviceSession, store, state, clock)
{
    // The one disk scan: backups of earlier runs. After that the folder each read saved is kept.
    if (const auto backups = store.list(); ! backups.empty())
        lastBackup = backups.front();

    // Fixed for this build: only Handshake and Read commands can pass the gate.
    deviceSession.setPolicy (WritePolicy::ReadOnly);

    transport.onReceive = [this] (const Bytes& raw)
    { connector.handleMessage (raw); };
    transport.onConnectionChanged = [this] (bool connected)
    { handleConnectionChanged (connected); };

    connector.onProgress = [this] (int done, int total)
    {
        progress = juce::String (done) + "/" + juce::String (total);
        changed();
    };
    connector.onFailed = [this] (const juce::String& why)
    {
        result = why;
        changed();
    };
    connector.onConnected = [this] (const DeviceInfo& info, const juce::File& savedFolder)
    {
        device = "Firmware " + info.firmware + ", name " + info.name;
        // The folder that holds this read (a new backup, or the newest one when nothing changed).
        backupFolder = savedFolder;
        lastBackup = savedFolder;
        result = "Backup saved: " + savedFolder.getFullPathName();
        changed();
    };
}

AppController::~AppController()
{
    transport.onReceive = nullptr;
    transport.onConnectionChanged = nullptr;
}

void AppController::begin()
{
    if (transport.isConnected())
        handleConnectionChanged (true);
    else
        changed();
}

void AppController::handleConnectionChanged (bool connected)
{
    device.clear();
    result.clear();
    backupFolder.reset();
    progress = connected ? "0/" + juce::String (Connector::totalRequests) : juce::String();
    if (! connected)
        state.clear();

    // After the reset, so a failure the Connector reports (e.g. an aborted read) stays visible.
    connector.handleConnectionChanged (connected);
    if (! connected)
        progress.clear();
    changed();
}

void AppController::tick() { connector.tick(); }

StatusModel AppController::status() const
{
    StatusModel s;
    s.mode = "READ-ONLY";
    s.connection = transport.isConnected() ? "Connected" : "Not connected";
    s.device = device;
    s.progress = progress;
    s.result = result;
    s.backupFolder = backupFolder;
    s.compare = compare;

    if (backupFolder.has_value())
    {
        for (int slot = 0; slot < DeviceState::presetCount; ++slot)
        {
            const auto preset = state.preset (slot);
            const auto name = preset.has_value() ? preset->name() : std::nullopt;
            s.presetNames.push_back (juce::String (slot).paddedLeft ('0', 2) + "  " + (name.has_value() ? juce::String::fromUTF8 (name->c_str()) : juce::String ("(unreadable name)")));
        }
    }
    return s;
}

std::optional<juce::File> AppController::latestBackup() const { return lastBackup; }

void AppController::compareWith (const juce::File& nuxpresetFile)
{
    const auto backup = latestBackup();
    compare = backup.has_value() ? describe (compareWithExport (*backup, nuxpresetFile))
                                 : juce::String ("No backup to compare with yet");
    changed();
}

DeviceSession& AppController::session() noexcept { return deviceSession; }

void AppController::changed() const
{
    if (onStatusChanged)
        onStatusChanged();
}
} // namespace qtn
