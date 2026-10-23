#pragma once

#include "Bytes.h"

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qtn
{
struct PortInfo
{
    std::string name;
    std::string identifier;
};

// The MIDI backend PortConnection drives. The JUCE adapter implements it;
// tests use a fake.
class MidiPorts
{
  public:
    virtual ~MidiPorts() = default;

    virtual std::vector<PortInfo> availableInputs() const = 0;
    virtual std::vector<PortInfo> availableOutputs() const = 0;

    // Opens both ports together; returns false (and leaves both closed) on failure.
    virtual bool open (const std::string& inputId, const std::string& outputId) = 0;
    virtual void close() = 0;

    // Sends a sysex payload without the F0/F7 framing bytes.
    virtual bool sendSysexPayload (std::span<const uint8_t> payload) = 0;
};

// Default port matcher until research R3 names the exact ports.
[[nodiscard]] bool isMg300PortName (std::string_view name);

// Decides which ports to open and tracks the connection state.
// Not thread-safe: call every method from one thread (the message thread).
class PortConnection
{
  public:
    using NameMatcher = std::function<bool (const std::string& portName)>;

    PortConnection (MidiPorts& ports, NameMatcher matcher);

    PortConnection (const PortConnection&) = delete;
    PortConnection& operator= (const PortConnection&) = delete;

    // Re-reads the port lists: closes the open pair if either port vanished,
    // then opens the first matching input + output if not connected.
    void rescan();

    [[nodiscard]] bool isConnected() const noexcept;

    // Sends a complete sysex (F0, 7-bit data bytes, F7). Returns false if
    // disconnected, malformed, or the port reports failure.
    [[nodiscard]] bool send (std::span<const uint8_t> sysex);

    std::function<void (bool connected)> onConnectionChanged;

  private:
    MidiPorts& ports;
    NameMatcher matcher;
    bool connected = false;
    std::string openInputId;
    std::string openOutputId;
};
} // namespace qtn
