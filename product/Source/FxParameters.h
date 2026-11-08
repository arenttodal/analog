#pragma once

#include "fx/FxGraph.h"
#include "fx/FxSettings.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace andor::analog::fxparams
{

/**
    The FX library's host parameters (state version 12; version hint 28; appended after every
    older parameter, never reordered): one fixed set per pool slot, `analog.fx.<type>.<n>.<name>`
    (docs/FX_EFFECTS_LIBRARY.md). They exist whether or not their effect is in the route, so a
    host's automation and a session always find them; a slot out of the route keeps its values.
*/
struct Spec
{
    juce::String name;          ///< "mix", "band2.gain"
    juce::String title;         ///< "Mix", "Band 2 Gain"
    enum class Kind { real, choice, toggle } kind = Kind::real;
    float low = 0.0f, high = 1.0f, fallback = 0.0f, centre = -1.0f;   ///< centre: the skew's mid value (-1: linear)
    juce::String unit;          ///< "%", "dB", "Hz", "ms", "us", "s", "st", "deg", "x", "cents"
    juce::StringArray choices;
    std::function<void (fx::Settings&, int slot, float value)> write;
};

/** A type's parameters (empty for the original four: theirs are the instrument's own). */
const std::vector<Spec>& specsOf (fx::Type type);
/** The ID of a slot's parameter. */
juce::String idOf (fx::InstanceId id, const juce::String& name);
/** Appends them all (Product's addParameters). */
void addTo (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
/** A value as the parameter shows it ("-6.0 dB", "1.20 kHz", "350 ms"). */
juce::String format (const Spec& spec, float value);

/** Reads every slot's settings from the parameters (real-time safe once attached). */
class Reader
{
public:
    void attach (juce::AudioProcessorValueTreeState& state);
    void read (fx::Settings& settings) const noexcept;

private:
    struct Entry
    {
        std::atomic<float>* value = nullptr;
        const Spec* spec = nullptr;
        int slot = 0;
    };
    std::vector<Entry> entries;
};

} // namespace andor::analog::fxparams
