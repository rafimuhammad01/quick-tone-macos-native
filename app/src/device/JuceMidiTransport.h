#pragma once

#include "device/PortConnection.h"
#include "device/SysexQueue.h"
#include "device/Transport.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <functional>
#include <memory>

namespace qtn
{
// CoreMIDI transport (via JUCE). Thin adapter: every decision lives in
// PortConnection and SysexQueue.
//
// Threads: construct, use and destroy on the message thread. onReceive and
// onConnectionChanged are invoked on the message thread.
// Scans once in the constructor (before callbacks can be set), so check
// isConnected() after construction; later plug/unplug events fire
// onConnectionChanged.
class JuceMidiTransport final : public Transport,
                                private juce::MidiInputCallback,
                                private juce::AsyncUpdater
{
  public:
    using Matcher = std::function<bool (const juce::String& portName)>;

    explicit JuceMidiTransport (Matcher matcher);
    ~JuceMidiTransport() override;

    bool isConnected() const override;
    bool send (const Bytes& message) override;

  private:
    class Ports final : public MidiPorts
    {
      public:
        explicit Ports (juce::MidiInputCallback& cb) : callback (cb) {}

        std::vector<PortInfo> availableInputs() const override;
        std::vector<PortInfo> availableOutputs() const override;
        bool open (const std::string& inputId, const std::string& outputId) override;
        void close() override;
        bool sendSysexPayload (std::span<const uint8_t> payload) override;
        bool sendShortMessage (std::span<const uint8_t> message) override;

      private:
        juce::MidiInputCallback& callback;
        std::unique_ptr<juce::MidiInput> input;
        std::unique_ptr<juce::MidiOutput> output;
    };

    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;
    void handleAsyncUpdate() override;

    // Declaration order matters: connection uses ports; the device-list
    // connection is destroyed first so no rescan runs during teardown.
    Ports ports{*this};
    SysexQueue incoming;
    PortConnection connection;
    juce::MidiDeviceListConnection deviceListConnection;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuceMidiTransport)
};
} // namespace qtn
