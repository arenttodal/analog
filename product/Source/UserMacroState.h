#pragma once

#include "UserMacroCore.h"

#include <juce_core/juce_core.h>

#include <array>
#include <memory>
#include <vector>

namespace andor::analog
{

/**
    USER MACROS V1.1 (docs/USER_MACROS_V1_1.md): the six USER macros' names and assignments,
    the sound's state (saved with the patch, the preset and the host's project). The bank shown
    (CORE / USER) is not here: it is the musician's interface preference.

    Saved as an optional <UserMacros schema="1"> child, written only when something differs from
    the default (no names, no assignments). Its absence is exactly the state from before: no
    assignment, the six macros at 0, the sound unchanged.

    Message thread only. The audio thread reads the compiled table (usermacro::Snapshot).
*/
struct UserMacroAssignment
{
    juce::String target;               ///< the destination's parameter ID (saved as is, never remapped)
    usermacro::Mapping map;
    bool orphaned = false;             ///< its target is gone (an effect removed): kept, silent, until reconnected
    juce::String reason;               ///< why it is silent ("EFFECT REMOVED", "Not available")
    bool operator== (const UserMacroAssignment&) const = default;
};

struct UserMacroState
{
    static constexpr int schema = 1;
    static constexpr int maxNameLength = 24;

    std::array<juce::String, usermacro::macroCount> names;   ///< empty: the default ("MACRO 1" ...)
    std::array<std::vector<UserMacroAssignment>, usermacro::macroCount> maps;

    /** The name shown (uppercase, the default when unnamed). */
    juce::String displayName (int macro) const;
    static juce::String defaultName (int macro) { return "MACRO " + juce::String (macro + 1); }
    /** A typed name made safe: no control characters, trimmed, at most maxNameLength characters. */
    static juce::String sanitisedName (const juce::String& typed);

    int assignmentCount (int macro) const;
    int activeCount (int macro) const;   ///< enabled and not orphaned
    /** The macro's assignment of `target`, or -1. */
    int find (int macro, const juce::String& target) const;
    bool isDefault() const;

    std::unique_ptr<juce::XmlElement> toXml() const;
    /** Bounded and sanitised: six macros at most, eight assignments each, no duplicate target in
        a macro, numbers clamped, NaNs neutral, unknown fields ignored. Null: the default. */
    static UserMacroState fromXml (const juce::XmlElement* xml);

    bool operator== (const UserMacroState&) const = default;
};

} // namespace andor::analog
