#pragma once

// What every family file uses: the Spec, the design vocabulary, the engine's enums.

#include "Pack.h"

#include "Unison.h"

namespace ca2
{
using namespace andor::analog;
using namespace ca2::design;
using SubWave = andor::analog::SubSettings::Wave;
using osp::mod::Source;
using osp::mod::Dest;
using osp::SpaceType;
using osp::AbyssVariant;
using osp::VintageVariant;
using osp::BloomVariant;
using osp::EchoType;
using osp::EchoStereo;
using osp::DriveMode;

/** Echo divisions (ECHO's sync choices). */
namespace div
{
    constexpr int sixteenth = 0, eighthTriplet = 1, sixteenthDotted = 2, eighth = 3, quarterTriplet = 4, eighthDotted = 5, quarter = 6;
}
} // namespace ca2
