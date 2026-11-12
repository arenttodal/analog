#include "FxParameters.h"
#include "ParameterFactory.h"

#include <cmath>
#include "fx/FxParameterTable.h"

namespace andor::analog::fxparams
{

using Kind = Spec::Kind;

namespace
{
    /** The engine's table (dsp/fx/FxParameterTable: the one definition of every effect's
        settings, their ranges and how each is written) as host parameters describe it. */
    std::vector<Spec> build (fx::Type type)
    {
        std::vector<Spec> o;
        for (const auto& t : fx::parameterSpecs (type))
        {
            Spec s;
            s.name = juce::String::fromUTF8 (t.name.c_str());
            s.title = juce::String::fromUTF8 (t.title.c_str());
            s.kind = static_cast<Kind> (t.kind);
            s.low = t.low;
            s.high = t.high;
            s.fallback = t.fallback;
            s.centre = t.centre;
            s.unit = juce::String::fromUTF8 (t.unit.c_str());
            for (const auto& c : t.choices)
                s.choices.add (juce::String::fromUTF8 (c.c_str()));
            s.write = t.write;
            o.push_back (std::move (s));
        }
        return o;
    }
}

const std::vector<Spec>& specsOf (fx::Type type)
{
    static const std::array<std::vector<Spec>, fx::typeCount> all = [] {
        std::array<std::vector<Spec>, fx::typeCount> a;
        for (int t = 0; t < fx::typeCount; ++t)
            a[static_cast<std::size_t> (t)] = build (static_cast<fx::Type> (t));
        return a;
    }();
    return all[static_cast<std::size_t> (type)];
}

juce::String idOf (fx::InstanceId id, const juce::String& name)
{
    return juce::String ("analog.fx.") + fx::typeKey (id.type) + "." + juce::String (id.slot + 1) + "." + name;
}

namespace
{
    /** `decimals` places; 0 rounds to a whole number (juce::String (v, 0) would print every digit). */
    juce::String fixed (float v, int decimals)
    {
        return decimals <= 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
    }
}

juce::String format (const Spec& spec, float v)
{
    // (A value that rounds to zero reads as 0, never "-0.0".)
    if (spec.kind == Kind::real && std::abs (v) < 0.005f)
        v = 0.0f;
    if (spec.kind == Kind::choice)
        return spec.choices[juce::jlimit (0, spec.choices.size() - 1, juce::roundToInt (v))];
    if (spec.kind == Kind::toggle)
        return v >= 0.5f ? "On" : "Off";
    const auto& u = spec.unit;
    // How many decimals: decided on the value as it will read (9.99999 reads "10", as 10 does),
    // so a value a float's width from a threshold reads the same on every platform.
    auto under = [] (float x, float limit, int decimals) {
        const double scale = std::pow (10.0, decimals);
        return std::round (std::abs (static_cast<double> (x)) * scale) / scale < static_cast<double> (limit);
    };
    if (u == "Hz")
        return ! under (v, 1000.0f, 0) ? fixed (v / 1000.0f, under (v / 1000.0f, 10.0f, 2) ? 2 : 1) + " kHz" : fixed (v, under (v, 10.0f, 2) ? 2 : 0) + " Hz";
    if (u == "dB")
        return (v >= 0.05f ? "+" : "") + fixed (std::abs (v) < 0.05f ? 0.0f : v, 1) + " dB";
    if (u == "ms")
        return ! under (v, 1000.0f, 1) ? fixed (v / 1000.0f, 2) + " s" : fixed (v, under (v, 10.0f, 1) ? 1 : 0) + " ms";
    if (u == "us")
        return juce::String (juce::roundToInt (v)) + juce::String::fromUTF8 (" \xc2\xb5s");
    if (u == "s")
        return fixed (v, under (v, 10.0f, 2) ? 2 : 1) + " s";
    if (u == "%")
        return juce::String (juce::roundToInt (v)) + " %";
    if (u == "x")
        return fixed (v, under (v, 10.0f, 1) ? 1 : 0) + (spec.name.endsWith ("q") ? "" : (spec.name.startsWith ("compress.") ? juce::String::fromUTF8 (" \xc3\x97") : ":1"));
    if (u == "deg")
        return juce::String (juce::roundToInt (v)) + juce::String::fromUTF8 ("\xc2\xb0");
    if (u == "st")
        return (v >= 0.05f ? "+" : "") + fixed (std::abs (v) < 0.05f ? 0.0f : v, 1) + " st";
    if (u == "cents")
        return fixed (v, 1) + " ct";
    if (u == "/s")
        return fixed (v, under (v, 10.0f, 1) ? 1 : 0) + " /s";
    if (u == "bit")
        return fixed (v, 1) + " bit";
    if (u == "kHz")
        return fixed (v, under (v, 10.0f, 1) ? 1 : 0) + " kHz";
    if (u == "#")
        return juce::String (juce::roundToInt (v));
    return fixed (v, 2);
}

void addTo (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    for (int t = 4; t < fx::typeCount; ++t)
    {
        const auto type = static_cast<fx::Type> (t);
        for (int slot = 0; slot < fx::capacity[static_cast<std::size_t> (t)]; ++slot)
        {
            const juce::String prefix = juce::String ("FX ") + fx::typeName (type) + (fx::capacity[static_cast<std::size_t> (t)] > 1 ? " " + juce::String (slot + 1) : juce::String()) + " ";
            for (const auto& spec : specsOf (type))
            {
                // (Version hints: 28 for the V1 effects; RETRO, appended in V2, 29.)
                const juce::ParameterID id { idOf ({ type, slot }, spec.name), type == fx::Type::retro ? 29 : 28 };
                const auto title = prefix + spec.title;
                if (spec.kind == Kind::toggle)
                    layout.add (osp::plugin::parameters::make<juce::AudioParameterBool> (id, title, spec.fallback >= 0.5f));
                else if (spec.kind == Kind::choice)
                    layout.add (osp::plugin::parameters::make<juce::AudioParameterChoice> (id, title, spec.choices, juce::roundToInt (spec.fallback)));
                else
                {
                    // Skewed only when the table gives a centre. ("No centre" was the value -1
                    // until 12 Nov 2026, so a range holding -1 was skewed about it: THRESHOLD
                    // -60..0 dB had its top 6 dB on nearly the whole knob. docs/STATUS.md.)
                    juce::NormalisableRange<float> range (spec.low, spec.high);
                    if (spec.centre && *spec.centre > spec.low && *spec.centre < spec.high)
                        range.setSkewForCentre (*spec.centre);
                    const auto* s = &spec;
                    layout.add (osp::plugin::parameters::make<juce::AudioParameterFloat> (
                        id, title, range, spec.fallback,
                        juce::AudioParameterFloatAttributes().withLabel (spec.unit == "%" ? "%" : spec.unit).withStringFromValueFunction ([s] (float v, int) { return format (*s, v); })));
                }
            }
        }
    }
}

void Reader::attach (juce::AudioProcessorValueTreeState& state)
{
    entries.clear();
    for (int t = 4; t < fx::typeCount; ++t)
    {
        const auto type = static_cast<fx::Type> (t);
        for (int slot = 0; slot < fx::capacity[static_cast<std::size_t> (t)]; ++slot)
            for (const auto& spec : specsOf (type))
                if (auto* raw = state.getRawParameterValue (idOf ({ type, slot }, spec.name)))
                    entries.push_back ({ raw, &spec, slot });
    }
}

void Reader::read (fx::Settings& settings) const noexcept
{
    for (const auto& e : entries)
        e.spec->write (settings, e.slot, e.value->load (std::memory_order_relaxed));
}

} // namespace andor::analog::fxparams
