#pragma once

#include "device/PortConnection.h"

#include <string>
#include <utility>
#include <vector>

namespace qtn::test
{
// Fake MIDI port backend for PortConnection: tests edit `inputs`/`outputs`
// to simulate plug events and read what was opened, closed and sent.
class FakeMidiPorts final : public MidiPorts
{
  public:
    std::vector<PortInfo> inputs;
    std::vector<PortInfo> outputs;
    bool openSucceeds = true;
    bool sendSucceeds = true;

    std::vector<std::pair<std::string, std::string>> openCalls; // {inputId, outputId}
    int closeCount = 0;
    std::vector<Bytes> sentPayloads;

    void plug (PortInfo in, PortInfo out)
    {
        inputs.push_back (std::move (in));
        outputs.push_back (std::move (out));
    }

    void unplugAll()
    {
        inputs.clear();
        outputs.clear();
    }

    std::vector<PortInfo> availableInputs() const override { return inputs; }
    std::vector<PortInfo> availableOutputs() const override { return outputs; }

    bool open (const std::string& inputId, const std::string& outputId) override
    {
        openCalls.emplace_back (inputId, outputId);
        return openSucceeds;
    }

    void close() override { ++closeCount; }

    bool sendSysexPayload (std::span<const uint8_t> payload) override
    {
        sentPayloads.emplace_back (payload.begin(), payload.end());
        return sendSucceeds;
    }
};
} // namespace qtn::test
