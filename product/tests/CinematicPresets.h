#pragma once

// CINEMATIC AFTERIMAGES VOL. 1 (sound-design branch, not installed: docs/sound-design/
// cinematic-afterimages-vol1.md). Ten original presets made with ANDOR ANALOG's own engine, each
// after the timbral character of a film or television score's synthesizers - no recordings, no
// melodies, no samples. Each preset is a Patch (the engine's settings) plus the shell's own
// parameters a Patch does not hold (modulation routes, the arpeggiator).

#include "AnalogParameters.h"
#include "ShellProcessor.h"

#include <functional>
#include <string>
#include <vector>

namespace cinematic
{

struct Spec
{
    std::string name;          ///< the public name (and the file's)
    std::string inspiration;   ///< internal: the reference it is after (never shipped in the preset)
    int lowKey = 36, highKey = 84;   ///< the playable range it was voiced for (MIDI notes)
    int voices = 8;            ///< the polyphony it expects
    bool arpeggiated = false;  ///< plays through the arpeggiator (hold a chord)
    std::function<void (andor::analog::Patch&)> patch;
    std::function<void (andor::shell::ShellProcessor&)> extra;   ///< routes, arpeggiator (after the patch)
};

/** The ten, in the collection's order (the pilot three first). */
const std::vector<Spec>& collection();

/** The preset as ANDOR ANALOG's preset files hold it (the factory installer's form: every
    parameter - the preset's values, defaults elsewhere - and KEY DNA's tree). */
std::unique_ptr<juce::XmlElement> presetXml (const Spec& spec);

/** Where the collection's files are kept (presets/Cinematic Afterimages Vol. 1). */
juce::File collectionFolder();
juce::File fileFor (const Spec& spec);

} // namespace cinematic
