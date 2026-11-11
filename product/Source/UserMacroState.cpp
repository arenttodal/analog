#include "UserMacroState.h"

#include <algorithm>
#include <cmath>

namespace andor::analog
{

juce::String UserMacroState::displayName (int macro) const
{
    if (macro < 0 || macro >= usermacro::macroCount)
        return {};
    const auto& n = names[static_cast<std::size_t> (macro)];
    return (n.isNotEmpty() ? n : defaultName (macro)).toUpperCase();
}

juce::String UserMacroState::sanitisedName (const juce::String& typed)
{
    juce::String clean;
    for (auto p = typed.getCharPointer(); ! p.isEmpty(); ++p)
    {
        const auto c = *p;
        if (c >= 0x20 && c != 0x7f && ! (c >= 0x80 && c < 0xa0))   // (no control characters)
            clean += juce::String::charToString (c);
    }
    clean = clean.trim();
    return clean.length() > maxNameLength ? clean.substring (0, maxNameLength).trimEnd() : clean;
}

int UserMacroState::assignmentCount (int macro) const
{
    return macro >= 0 && macro < usermacro::macroCount ? static_cast<int> (maps[static_cast<std::size_t> (macro)].size()) : 0;
}

int UserMacroState::activeCount (int macro) const
{
    if (macro < 0 || macro >= usermacro::macroCount)
        return 0;
    const auto& list = maps[static_cast<std::size_t> (macro)];
    return static_cast<int> (std::count_if (list.begin(), list.end(), [] (const UserMacroAssignment& a) { return a.map.enabled && ! a.orphaned; }));
}

int UserMacroState::find (int macro, const juce::String& target) const
{
    if (macro < 0 || macro >= usermacro::macroCount)
        return -1;
    const auto& list = maps[static_cast<std::size_t> (macro)];
    for (std::size_t i = 0; i < list.size(); ++i)
        if (list[i].target == target)
            return static_cast<int> (i);
    return -1;
}

bool UserMacroState::isDefault() const
{
    for (int i = 0; i < usermacro::macroCount; ++i)
        if (names[static_cast<std::size_t> (i)].isNotEmpty() || ! maps[static_cast<std::size_t> (i)].empty())
            return false;
    return true;
}

std::unique_ptr<juce::XmlElement> UserMacroState::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("UserMacros");
    xml->setAttribute ("schema", schema);
    for (int i = 0; i < usermacro::macroCount; ++i)
    {
        const auto& name = names[static_cast<std::size_t> (i)];
        const auto& list = maps[static_cast<std::size_t> (i)];
        if (name.isEmpty() && list.empty())
            continue;
        auto* macro = xml->createNewChildElement ("Macro");
        macro->setAttribute ("index", i);
        if (name.isNotEmpty())
            macro->setAttribute ("name", name);
        for (const auto& a : list)
        {
            auto* m = macro->createNewChildElement ("Map");
            m->setAttribute ("target", a.target);
            m->setAttribute ("enabled", a.map.enabled ? 1 : 0);
            m->setAttribute ("anchorMacro", static_cast<double> (a.map.anchorMacro));
            m->setAttribute ("anchorTarget", static_cast<double> (a.map.anchorTarget));
            m->setAttribute ("low", static_cast<double> (a.map.low));
            m->setAttribute ("high", static_cast<double> (a.map.high));
            m->setAttribute ("curve", a.map.curve == usermacro::Curve::ease ? "ease" : "linear");
            if (a.orphaned)
            {
                m->setAttribute ("orphaned", 1);
                if (a.reason.isNotEmpty())
                    m->setAttribute ("reason", a.reason);
            }
        }
    }
    return xml;
}

UserMacroState UserMacroState::fromXml (const juce::XmlElement* xml)
{
    UserMacroState s;
    if (xml == nullptr || ! xml->hasTagName ("UserMacros"))
        return s;
    // A newer schema is read as far as this one understands it (its unknown fields ignored).
    for (auto* macro : xml->getChildWithTagNameIterator ("Macro"))
    {
        const int i = macro->getIntAttribute ("index", -1);
        if (i < 0 || i >= usermacro::macroCount)
            continue;
        const auto ui = static_cast<std::size_t> (i);
        s.names[ui] = sanitisedName (macro->getStringAttribute ("name"));
        auto& list = s.maps[ui];
        for (auto* m : macro->getChildWithTagNameIterator ("Map"))
        {
            if (static_cast<int> (list.size()) >= usermacro::mapsPerMacro)
                break;
            UserMacroAssignment a;
            a.target = m->getStringAttribute ("target").trim().substring (0, 128);
            if (a.target.isEmpty() || ! a.target.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-"))
                continue;   // (not a parameter ID: rejected)
            if (s.find (i, a.target) >= 0)
                continue;   // (one assignment per target in a macro)
            auto number = [m] (const char* name, double fallback) {
                const double v = m->getDoubleAttribute (name, fallback);
                return static_cast<float> (std::isfinite (v) ? v : fallback);
            };
            a.map.anchorMacro = number ("anchorMacro", 0.0);
            a.map.anchorTarget = number ("anchorTarget", 0.0);
            a.map.low = number ("low", a.map.anchorTarget);
            a.map.high = number ("high", a.map.anchorTarget);
            a.map.curve = m->getStringAttribute ("curve") == "ease" ? usermacro::Curve::ease : usermacro::Curve::linear;
            a.map.enabled = m->getIntAttribute ("enabled", 1) != 0;
            a.map = usermacro::sanitised (a.map);
            a.orphaned = m->getIntAttribute ("orphaned", 0) != 0;
            a.reason = sanitisedName (m->getStringAttribute ("reason"));
            list.push_back (a);
        }
    }
    return s;
}

} // namespace andor::analog
