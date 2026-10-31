#include "device/SysexQueue.h"

#include "device/MidiBytes.h"

#include <algorithm>
#include <iterator>

namespace qtn
{
SysexQueue::SysexQueue (size_t maxMessages) : capacity (std::max<size_t> (maxMessages, 1)) {}

size_t SysexQueue::droppedCount()
{
    const std::scoped_lock lock (mutex);
    return dropped;
}

bool SysexQueue::push (std::span<const uint8_t> raw)
{
    if (! messageForm (raw).has_value())
        return false;

    const std::scoped_lock lock (mutex);
    if (pending.size() >= capacity)
    {
        pending.pop_front();
        ++dropped;
    }
    pending.emplace_back (raw.begin(), raw.end());
    return true;
}

std::vector<Bytes> SysexQueue::drain()
{
    std::deque<Bytes> taken;
    {
        const std::scoped_lock lock (mutex);
        taken.swap (pending);
    }
    return {std::make_move_iterator (taken.begin()), std::make_move_iterator (taken.end())};
}
} // namespace qtn
