#pragma once

#include "Patch.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <map>

/**
    ANDOR ANALOG's parameters. Two kinds:

      - its own, in its namespace ("analog.*"), appended after the ANDOR schema
        (ProductInfo::addParameters). Pinned by product/golden/parameter-schema.txt. Never
        rename, re-range or reorder one once released (sessions and automation keep them).
      - ANDOR OSP's own where the meaning is exactly the same (the amplitude envelope, the
        master volume, bend range, glide, mono, fine tune, variation seed, velocity range,
        DRIVE, SPACE and ECHO with their settings, layer A's TUNE / PAN / LEVEL / MUTE): the
        shared interface's controls for them then work as they do in ANDOR OSP.

    docs/PARAMETER_REGISTRY.md lists every one with its range, default and use.
*/
namespace andor::analog::params
{
    // The product's own (IDs frozen once released).
    inline const juce::String blend = "analog.blend";
    inline const juce::String tone = "analog.tone";
    inline const juce::String motion = "analog.motion";
    inline const juce::String osc1Wave = "analog.osc1.wave";
    inline const juce::String osc1Octave = "analog.osc1.octave";
    inline const juce::String osc1Coarse = "analog.osc1.coarse";
    inline const juce::String osc1Width = "analog.osc1.width";
    inline const juce::String osc1Level = "analog.osc1.level";
    inline const juce::String osc2Wave = "analog.osc2.wave";
    inline const juce::String osc2Octave = "analog.osc2.octave";
    inline const juce::String osc2Coarse = "analog.osc2.coarse";
    inline const juce::String osc2Fine = "analog.osc2.fine";
    inline const juce::String osc2Width = "analog.osc2.width";
    inline const juce::String osc2Level = "analog.osc2.level";
    inline const juce::String osc2KeyTrack = "analog.osc2.keyTrack";
    inline const juce::String osc2LowFreq = "analog.osc2.lowFreq";
    inline const juce::String noise = "analog.noise";
    inline const juce::String sync = "analog.sync";
    inline const juce::String polyphony = "analog.polyphony";
    inline const juce::String unison = "analog.unison";
    inline const juce::String unisonDetune = "analog.unison.detune";
    inline const juce::String spread = "analog.spread";
    inline const juce::String resonance = "analog.resonance";
    inline const juce::String keyTrack = "analog.keyTrack";
    inline const juce::String envDepth = "analog.envDepth";
    inline const juce::String velocityToFilter = "analog.velocityToFilter";
    inline const juce::String driveBias = "analog.drive.bias";
    inline const juce::String driveTrim = "analog.drive.trim";
    inline const juce::String filterAttack = "analog.filterEnv.attack";
    inline const juce::String filterDecay = "analog.filterEnv.decay";
    inline const juce::String filterSustain = "analog.filterEnv.sustain";
    inline const juce::String filterRelease = "analog.filterEnv.release";
    inline const juce::String lfoWave = "analog.lfo.wave";
    inline const juce::String lfoRate = "analog.lfo.rate";
    inline const juce::String lfoSync = "analog.lfo.sync";
    inline const juce::String lfoDivision = "analog.lfo.division";
    inline const juce::String lfoPitch1 = "analog.lfo.pitch1";
    inline const juce::String lfoPitch2 = "analog.lfo.pitch2";
    inline const juce::String lfoWidth1 = "analog.lfo.width1";
    inline const juce::String lfoWidth2 = "analog.lfo.width2";
    inline const juce::String lfoCutoff = "analog.lfo.cutoff";
    inline const juce::String polyModEnv = "analog.polyMod.env";
    inline const juce::String polyModOsc2 = "analog.polyMod.osc2";
    inline const juce::String polyModToPitch1 = "analog.polyMod.toPitch1";
    inline const juce::String polyModToWidth1 = "analog.polyMod.toWidth1";
    inline const juce::String polyModToCutoff = "analog.polyMod.toCutoff";
    inline const juce::String analogAmount = "analog.analog";
    inline const juce::String phaseMode = "analog.phase";

    /** Every parameter the engine reads (its own and ANDOR OSP's), for Engine::boundParameters. */
    const juce::StringArray& engineParameterIds();

    /** Appends the product's own parameters (ProductInfo::addParameters). */
    void addTo (juce::AudioProcessorValueTreeState::ParameterLayout& layout);

    /** The engine's view: raw parameter values (cached in prepare(), read on the audio thread). */
    class Reader
    {
    public:
        void attach (juce::AudioProcessorValueTreeState& state);
        /** The patch the parameters describe now (real-time safe). */
        Patch read() const noexcept;

    private:
        float value (int index) const noexcept;
        std::vector<std::atomic<float>*> values;
    };

    /** A patch as parameter values (unnormalised), by ID: the factory presets. */
    std::map<juce::String, float> valuesOf (const Patch& patch);
}
