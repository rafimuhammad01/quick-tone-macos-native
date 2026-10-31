#pragma once

#include "Bytes.h"

#include <deque>
#include <mutex>
#include <span>
#include <vector>

namespace qtn
{
// Hand-off of incoming messages from the MIDI thread to the message thread.
// push() is called on the MIDI thread, drain() on the message thread. Bounded: when the
// message thread falls behind, the oldest queued message is dropped for each new one.
class SysexQueue
{
  public:
    static constexpr size_t defaultCapacity = 4096;

    // Holds at most `capacity` messages (at least 1).
    explicit SysexQueue (size_t capacity = defaultCapacity);

    // Queues a copy if `raw` is one complete sysex (F0 ... F7) or one short
    // channel message such as a CC (see qtn::messageForm); returns whether it
    // was queued. Realtime bytes and fragments are dropped.
    bool push (std::span<const uint8_t> raw);

    // Removes and returns everything queued, oldest first.
    [[nodiscard]] std::vector<Bytes> drain();

    // Messages dropped because the queue was full.
    [[nodiscard]] size_t droppedCount();

  private:
    std::mutex mutex;
    std::deque<Bytes> pending;
    size_t capacity;
    size_t dropped = 0;
};
} // namespace qtn
