#pragma once

#include "FxGraph.h"
#include "FxSettings.h"

#include <functional>
#include <string>
#include <vector>

namespace andor::analog::fx
{

/**
    One setting of a new effect as a parameter (docs/FX_EFFECTS_LIBRARY.md): its name within the
    effect ("mix", "band2.gain"), the range it is edited in (physical units: %, dB, Hz, ms...)
    and how a value is written into the settings the processors read.

    The one definition of the library's parameters, free of any plugin framework: ANDOR ANALOG's
    host parameters are made from it (product/Source/FxParameters), and another instrument that
    plays these processors offers the same settings from the same table.
*/
struct ParameterSpec
{
    std::string name;           ///< "mix", "band2.gain" (saved: never renamed)
    std::string title;          ///< "Mix", "Band 2 Gain"
    enum class Kind { real, choice, toggle } kind = Kind::real;
    float low = 0.0f, high = 1.0f, fallback = 0.0f, centre = -1.0f;   ///< centre: the skew's mid value (-1: linear)
    std::string unit;           ///< "%", "dB", "Hz", "ms", "us", "s", "st", "deg", "x", "cents"
    std::vector<std::string> choices;   ///< a choice's positions (UTF-8)
    /** Writes `value` (in the parameter's own units) into `slot`'s settings. Real-time safe. */
    std::function<void (Settings&, int slot, float value)> write;
};

/** A type's parameters, in their saved order (empty for the original four: theirs are the
    instrument's own). */
const std::vector<ParameterSpec>& parameterSpecs (Type type);

} // namespace andor::analog::fx
