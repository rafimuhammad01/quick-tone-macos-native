#include "device/JuceMidiTransport.h"

#include <limits>

namespace qtn
{
namespace
{
std::vector<PortInfo> toPortInfos (const juce::Array<juce::MidiDeviceInfo>& devices)
{
    std::vector<PortInfo> result;
    result.reserve (static_cast<size_t> (devices.size()));

    for (const auto& d : devices)
        result.push_back ({d.name.toStdString(), d.identifier.toStdString()});

    return result;
}
} // namespace

std::vector<PortInfo> JuceMidiTransport::Ports::availableInputs() const
{
    return toPortInfos (juce::MidiInput::getAvailableDevices());
}

std::vector<PortInfo> JuceMidiTransport::Ports::availableOutputs() const
{
    return toPortInfos (juce::MidiOutput::getAvailableDevices());
}

bool JuceMidiTransport::Ports::open (const std::string& inputId, const std::string& outputId)
{
    close();
    input = juce::MidiInput::openDevice (juce::String::fromUTF8 (inputId.c_str()), &callback);
    output = juce::MidiOutput::openDevice (juce::String::fromUTF8 (outputId.c_str()));

    if (input == nullptr || output == nullptr)
    {
        close();
        return false;
    }

    input->start();
    return true;
}

void JuceMidiTransport::Ports::close()
{
    if (input != nullptr)
        input->stop();

    input.reset();
    output.reset();
}

bool JuceMidiTransport::Ports::sendSysexPayload (std::span<const uint8_t> payload)
{
    if (output == nullptr)
        return false;

    jassert (payload.size() <= static_cast<size_t> (std::numeric_limits<int>::max()));
    output->sendMessageNow (juce::MidiMessage::createSysExMessage (payload.data(), static_cast<int> (payload.size())));
    return true;
}

JuceMidiTransport::JuceMidiTransport (Matcher matcher)
    : connection (ports, [m = std::move (matcher)] (const std::string& name)
                  { return m (juce::String::fromUTF8 (name.c_str())); })
{
    JUCE_ASSERT_MESSAGE_THREAD

    connection.onConnectionChanged = [this] (bool connected)
    {
        if (onConnectionChanged)
            onConnectionChanged (connected);
    };

    // The broadcaster invokes this on the message thread
    // (juce_MidiDeviceListConnectionBroadcaster.cpp, endpointsChanged()).
    deviceListConnection = juce::MidiDeviceListConnection::make ([this]
                                                                 { connection.rescan(); });
    connection.rescan();
}

JuceMidiTransport::~JuceMidiTransport()
{
    JUCE_ASSERT_MESSAGE_THREAD
    deviceListConnection.reset();
    ports.close(); // stops the MidiInput: no more MIDI-thread callbacks
    cancelPendingUpdate();
}

bool JuceMidiTransport::isConnected() const
{
    return connection.isConnected();
}

bool JuceMidiTransport::send (const Bytes& sysex)
{
    JUCE_ASSERT_MESSAGE_THREAD
    return connection.send (sysex);
}

// MIDI thread: copy into the queue and hand off.
void JuceMidiTransport::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    const std::span<const uint8_t> raw{message.getRawData(), static_cast<size_t> (message.getRawDataSize())};

    if (incoming.push (raw))
        triggerAsyncUpdate();
}

// Message thread.
void JuceMidiTransport::handleAsyncUpdate()
{
    for (const auto& sysex : incoming.drain())
        if (onReceive)
            onReceive (sysex);
}
} // namespace qtn
