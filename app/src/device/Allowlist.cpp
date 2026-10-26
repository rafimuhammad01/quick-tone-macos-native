#include "device/Allowlist.h"

namespace qtn
{
Allowlist Allowlist::fromJson (const juce::String& json)
{
    Allowlist list;
    juce::var parsed;

    if (juce::JSON::parse (json, parsed).failed())
        return list;

    if (const auto* entries = parsed.getArray(); entries != nullptr)
        for (const auto& entry : *entries)
            if (entry.isString())
                list.names.insert (entry.toString().toStdString());

    return list;
}

bool Allowlist::contains (const std::string& name) const
{
    return names.contains (name);
}
} // namespace qtn
