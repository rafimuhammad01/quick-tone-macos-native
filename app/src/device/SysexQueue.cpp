#include "device/SysexQueue.h"

#include "device/MidiBytes.h"

namespace qtn
{
bool SysexQueue::push (std::span<const uint8_t> raw)
{
    if (! messageForm (raw).has_value())
        return false;

    const std::scoped_lock lock (mutex);
    pending.emplace_back (raw.begin(), raw.end());
    return true;
}

std::vector<Bytes> SysexQueue::drain()
{
    std::vector<Bytes> out;
    const std::scoped_lock lock (mutex);
    out.swap (pending);
    return out;
}
} // namespace qtn
