#pragma once

#include "Bytes.h"

#include <mutex>
#include <span>
#include <vector>

namespace qtn
{
// Hand-off from the MIDI thread to the message thread.
// push() is called on the MIDI thread, drain() on the message thread.
class SysexQueue
{
  public:
    // Queues a copy if `raw` is one complete sysex (F0 ... F7); returns
    // whether it was queued.
    bool push (std::span<const uint8_t> raw);

    // Removes and returns everything queued, oldest first.
    [[nodiscard]] std::vector<Bytes> drain();

  private:
    std::mutex mutex;
    std::vector<Bytes> pending;
};
} // namespace qtn
