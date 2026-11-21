#pragma once

// CINEMATIC AFTERIMAGES VOL. 2 - 100 presets made with ANDOR ANALOG's own engine (README.md in
// this folder). Each preset is a Spec: its catalogue entry (ID, name, family, description, how to
// play it, its range) and its sound - a Patch (the engine's settings, checked by the compiler, so
// no parameter ID is guessed) plus the shell's own parameters a Patch does not hold (modulation
// routes, the arpeggiator, card C's level). Files are written through ShellProcessor::savePreset,
// the same path as a preset a musician saves.

#include "AnalogParameters.h"
#include "ShellProcessor.h"
#include "engine/Modulation.h"

#include <functional>
#include <string>
#include <vector>

namespace ca2
{

using andor::analog::Patch;
using andor::shell::ShellProcessor;

/** The ten families, in the collection's order (their names are the folders' and the browser's). */
enum class Family : int { weight, corrosion, monumental, distance, voltage, slowLight, orchestra, machines, darkMatter, fragile, count };
const char* familyName (Family f);       ///< "Fundamental Weight"
const char* familySubtitle (Family f);   ///< "Fat, musical analog basses"

/** What the previews play (every preset also gets the notes at three velocities and a long note). */
enum class Phrase : int { bassLine, lead, chords, arpChord, drone, keys };

struct Spec
{
    int number = 0;                  ///< 1..100: "CA2-001"
    std::string name;                ///< the public name (and the file's)
    Family family = Family::weight;
    std::string description;         ///< for musicians: its character (no parameter inventory)
    std::string play;                ///< how to play it
    std::string role;                ///< its musical job
    std::string identity;            ///< sound designer: what sets it apart from its neighbours
    int lowKey = 36, highKey = 84;   ///< the range it was voiced for (MIDI notes)
    Phrase phrase = Phrase::lead;
    std::function<void (Patch&)> patch;
    std::function<void (ShellProcessor&)> extra;   ///< routes, arpeggiator (after the patch)

    std::string id() const;          ///< "CA2-001"
};

/** The hundred, in order (family A's ten first). */
const std::vector<Spec>& collection();

/** Sets a fresh processor to the preset (every parameter: the preset's, defaults elsewhere), its
    loudness trim included (trims.inc: one gain for every source, the master VOLUME untouched). */
void configure (ShellProcessor& p, const Spec& spec, bool withTrim = true);
/** The trim (dB) the preset's calibration chose (0 before the first calibration). */
double trimOf (const Spec& spec);

juce::File contentFolder();                      ///< content/cinematic-afterimages-v2
juce::File presetFolder();                       ///< .../presets/Cinematic Afterimages Vol. 2
juce::File fileFor (const Spec& spec);           ///< .../<Family>/<Name>.andoranalog

// -- the definitions' vocabulary (Pack.cpp) -------------------------------------------------------
namespace design
{
    /** The neutral start: two saws, the oscillators at a modest level, no MORPH, no REIMAGINED, no
        SPACE / ECHO / MOVEMENT / DRIVE, a little analog drift, LFO depths 0. */
    Patch canvas();
    double hz (double cutoffHz);   ///< a cutoff in Hz as TONE (0..1)
    void filterEnv (Patch& p, double a, double d, double s, double r, double depthSemitones);
    void ampEnv (Patch& p, double a, double d, double s, double r);
    void mono (Patch& p, bool legato, double glideSeconds = 0.0);
    void vibratoOnWheel (Patch& p, double cents, double rateHz, double delaySeconds = 0.25, double riseSeconds = 0.5);
    void vibratoAlways (Patch& p, double cents, double rateHz, double delaySeconds, double riseSeconds);
    void unisonCard (Patch& p, int card, int voices, double detune, double blend, double width,
                     andor::analog::UnisonSpread spread = andor::analog::UnisonSpread::linear);
    void sub (Patch& p, andor::analog::SubSettings::Wave wave, int octave, double levelDb, int lowKey = 24, int highKey = 72, double grit = 0.0);
    /** Card C as a third oscillator (its level through cardCLevel in `extra`'s place: configure). */
    void osc3 (Patch& p, andor::analog::Osc2Wave wave, int octave, double coarse, double fine, double levelDb);
    void noiseC (Patch& p, andor::analog::NoiseColor color, double tone, double movement, double levelDb);

    // Effects: SPACE (rooms, ABYSS, VINTAGE, BLOOM), ECHO (tape, BBD, ASCEND), DRIVE, MOVEMENT.
    void room (Patch& p, osp::SpaceType type, double amount, double decaySeconds, double preDelayMs = 12.0, double highCutHz = 6000.0,
               double lowCutHz = 120.0);
    void abyss (Patch& p, osp::AbyssVariant variant, double amount, double decaySeconds, double preDelayMs = 30.0, double highCutHz = 5000.0);
    void vintage (Patch& p, osp::VintageVariant variant, double amount, double decaySeconds, double preDelayMs = 20.0, double highCutHz = 7000.0);
    void bloom (Patch& p, osp::BloomVariant variant, double amount, double decaySeconds, int division = 6);
    void echo (Patch& p, osp::EchoType type, double amount, int division, double feedback, double tone = 0.45,
               osp::EchoStereo stereo = osp::EchoStereo::pingPong);
    void drive (Patch& p, osp::DriveMode mode, double amount, double tone = 0.5, double body = 0.5);
    void drift (Patch& p, double amount, double speed = 0.6, double pitch = 0.3, double tone = 0.4);
    void tape (Patch& p, double amount, double wow = 0.5, double flutter = 0.35, double wear = 0.35);
    void chorusMovement (Patch& p, double amount, double rate = 0.45, double width = 0.6, double stereo = 0.7);
    void pulse (Patch& p, double amount, double rate, double shape = 0.3, double stereo = 0.3, bool sync = false, int division = 4);

    // The shell's own (in Spec::extra).
    void route (ShellProcessor& p, int slot, osp::mod::Source source, osp::mod::Dest dest, float depthPercent);
    void choose (ShellProcessor& p, const juce::String& id, const juce::String& choice);
    void set (ShellProcessor& p, const juce::String& id, float plainValue);
    /** The arpeggiator: on, pattern and rate by their names ("UP", "1/16"), octaves, gate %. */
    void arp (ShellProcessor& p, const juce::String& pattern, const juce::String& rate, int octaves, float gatePercent);
}

} // namespace ca2
