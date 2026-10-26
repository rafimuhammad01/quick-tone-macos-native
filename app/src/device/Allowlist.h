#pragma once

#include <juce_core/juce_core.h>

#include <set>
#include <string>

namespace qtn
{
/** The set of command names whose bytes have been verified against a capture.
    DeviceSession refuses any command whose name is not in it. */
class Allowlist
{
  public:
    /** Parses a JSON array of strings, e.g. ["getOnePreset"]. Anything that is not an array
        gives an empty allowlist; non-string entries are ignored. */
    static Allowlist fromJson (const juce::String& json);

    bool contains (const std::string& name) const;

  private:
    std::set<std::string> names;
};
} // namespace qtn
