#pragma once

#include "fx/FxGraph.h"
#include "InstrumentHost.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace andor::analog::fxpresets
{

/**
    FX presets (docs/FX_EFFECTS_LIBRARY.md): one effect's settings by name. A preset holds only
    that effect's own parameters (`analog.fx.<type>.<n>.<name>` by name, so it loads into any slot
    of its type); it never touches the instrument, the route, ON or another effect.

    Factory presets ship in code; user presets are files (`<user data>/ANDOR/ANDOR Analog/FX
    Presets/<TYPE>/<name>.xml`). A CHAIN preset holds a route and the settings of every effect in
    it (`.../FX Chains/<name>.xml`): loading one sets the route and those effects only.
*/
struct Preset
{
    juce::String name;
    std::vector<std::pair<juce::String, float>> values;   ///< parameter name -> value (physical units)
};

const std::vector<Preset>& factory (fx::Type type);
juce::File userFolder (fx::Type type);
/** Where user presets and chains live, for an instrument that keeps its own (`<folder>/FX
    Presets/<TYPE>`, `<folder>/FX Chains`). An empty file: ANDOR ANALOG's, the default. Set once,
    before the first rack is shown (message thread). */
void setUserRoot (const juce::File& folder);
std::vector<Preset> user (fx::Type type);

/** Sets the slot's parameters to the preset (each with a host gesture; unnamed ones untouched). */
void apply (osp::plugin::InstrumentHost& host, fx::InstanceId id, const Preset& preset);
/** The slot's current settings as a preset (every parameter but ON). */
Preset capture (osp::plugin::InstrumentHost& host, fx::InstanceId id, const juce::String& name);
bool saveUser (fx::Type type, const Preset& preset);
bool removeUser (fx::Type type, const juce::String& name);
/** Whether the slot's parameters are still the preset's (a modified preset shows "*"). */
bool matches (osp::plugin::InstrumentHost& host, fx::InstanceId id, const Preset& preset);

/** A chain: the route and its effects' settings, to or from a file. */
juce::File chainFolder();
std::unique_ptr<juce::XmlElement> chainToXml (osp::plugin::InstrumentHost& host, const fx::Graph& route, const juce::String& name);
/** Reads a chain: the route (returned through `route`) and sets its effects' parameters. */
bool chainFromXml (osp::plugin::InstrumentHost& host, const juce::XmlElement& xml, fx::Graph& route);

} // namespace andor::analog::fxpresets
