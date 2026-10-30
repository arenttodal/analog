#pragma once

#include "Patch.h"

#include "engine/KeyDna.h"

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
        DRIVE, MOVEMENT, SPACE and ECHO with their settings, REIMAGINED and its modes, layer A's
        source mode and GRANULAR settings, LFO 1, layer A's TUNE / PAN / LEVEL / MUTE): the
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
    // Version 2.
    inline const juce::String filterDrive = "analog.filterDrive";
    inline const juce::String keyDna = "analog.keyDna";
    inline const juce::String keyDnaAmount = "analog.keyDna.amount";
    inline const juce::String keyDnaIndividuality = "analog.keyDna.individuality";
    inline const juce::String keyDnaPosition = "analog.keyDna.position";
    inline const juce::String keyDnaSize = "analog.keyDna.size";
    inline const juce::String keyDnaDensity = "analog.keyDna.density";
    inline const juce::String keyDnaSpread = "analog.keyDna.spread";
    inline const juce::String keyDnaTune = "analog.keyDna.tune";
    // State version 4 (one append-only set: oscillator models, OTA filter family, chorus,
    // ORGANIC, MORPH). docs/PARAMETER_REGISTRY.md section E.
    inline const juce::String osc1Model = "analog.osc1.model";
    inline const juce::String osc2Model = "analog.osc2.model";
    inline const juce::String osc1DcoSaw = "analog.osc1.dco.saw";
    inline const juce::String osc1DcoPulse = "analog.osc1.dco.pulse";
    inline const juce::String osc1DcoSub = "analog.osc1.dco.subLevel";
    inline const juce::String osc1DcoPwmSource = "analog.osc1.dco.pwmSource";
    inline const juce::String osc1DcoPwmDepth = "analog.osc1.dco.pwmDepth";
    inline const juce::String osc2DcoSaw = "analog.osc2.dco.saw";
    inline const juce::String osc2DcoPulse = "analog.osc2.dco.pulse";
    inline const juce::String osc2DcoSub = "analog.osc2.dco.subLevel";
    inline const juce::String osc2DcoPwmSource = "analog.osc2.dco.pwmSource";
    inline const juce::String osc2DcoPwmDepth = "analog.osc2.dco.pwmDepth";
    inline const juce::String osc1WideWave = "analog.osc1.wide.wave";
    inline const juce::String osc2WideWave = "analog.osc2.wide.wave";
    inline const juce::String osc1WideShape = "analog.osc1.wide.shape";
    inline const juce::String osc2WideShape = "analog.osc2.wide.shape";
    inline const juce::String osc1WideSpread = "analog.osc1.wide.spread";
    inline const juce::String osc2WideSpread = "analog.osc2.wide.spread";
    inline const juce::String ringMix = "analog.osc.pair.ringMix";
    inline const juce::String noiseColor = "analog.noise.color";
    inline const juce::String filterModel = "analog.filter.model";
    inline const juce::String otaCharacter = "analog.filter.ota.character";
    inline const juce::String otaSlope = "analog.filter.ota.slope";
    inline const juce::String otaHpf = "analog.filter.ota.hpf";
    inline const juce::String junoVariant = "analog.filter.juno.variant";
    inline const juce::String lfoDelay = "analog.juno.lfoDelay";
    inline const juce::String lfoRise = "analog.juno.lfoRise";
    inline const juce::String vcaMode = "analog.juno.vcaMode";
    inline const juce::String envLink = "analog.juno.envLink";
    inline const juce::String chorusMode = "analog.juno.chorusMode";
    inline const juce::String chorusNoise = "analog.juno.chorusNoise";
    inline const juce::String osc1Enabled = "analog.osc1.enabled";
    inline const juce::String osc2Enabled = "analog.osc2.enabled";
    // ORGANIC is the shell's layer B (ProductInfo::hybridSampleName): its card's own LEVEL,
    // PAN, TUNE, START, LOOP, FOLLOW, REVERSE, M and S (ANDOR OSP's layerB.*, the same
    // meaning) play it; only the root override is the instrument's.
    inline const juce::String organicRoot = "analog.organic.root";
    inline const juce::String morphEnabled = "analog.morph.enabled";
    inline const juce::String morphPosition = "analog.morph.position";
    inline const juce::String morphMethod = "analog.morph.method";
    inline const juce::String morphCarrier = "analog.morph.carrier";
    inline const juce::String morphTexture = "analog.morph.texture";
    inline const juce::String morphAttack = "analog.morph.attack";
    inline const juce::String morphFormants = "analog.morph.formants";
    inline const juce::String morphDetail = "analog.morph.detail";
    inline const juce::String morphLevelMatch = "analog.morph.levelMatch";
    inline const juce::String morphOutput = "analog.morph.output";
    /** Every parameter state version 4 added (the V3 -> V4 migration sets them to their defaults). */
    const juce::StringArray& version4Ids();

    /** How far each key's own grains differ, by default, once KEY DNA is on. */
    constexpr double keyDnaDefaultVariation = 0.35;

    /** Every parameter the engine reads (its own and ANDOR OSP's), for Engine::boundParameters. */
    const juce::StringArray& engineParameterIds();

    /** Appends the product's own parameters (ProductInfo::addParameters). */
    void addTo (juce::AudioProcessorValueTreeState::ParameterLayout& layout);

    /**
        The controls a modulation route can move (ProductInfo::modulationDestinations, in this
        order: mod::productDest (k) is the k-th; append only, sessions store the order).
        Voice-owned ones are moved per note (Reader::moveControl), global ones for the whole
        instrument (Reader::read with the moved values).
    */
    const std::vector<osp::mod::ProductDest>& modulationDestinations();

    /** The engine's view: raw parameter values (cached in prepare(), read on the audio thread). */
    class Reader
    {
    public:
        void attach (juce::AudioProcessorValueTreeState& state);
        /** The patch the parameters describe now (real-time safe). */
        Patch read() const noexcept;
        /** GRANULAR's KEY DNA as the parameters describe it (real-time safe; flat curves). */
        osp::keydna::Settings keyDna (std::uint32_t seed) const noexcept;

        // Modulation (real-time safe).
        static constexpr int maxValues = 256;
        using Values = std::array<float, maxValues>;
        /** Every raw value now, in the reader's order. */
        void snapshot (Values& raw) const noexcept;
        /** The patch `raw` describes. */
        Patch read (const Values& raw) const noexcept;
        /** Moves product destination k's raw value by `offset` (a share of its knob's travel,
            clamped at its ends); false: not one of the reader's. */
        bool move (Values& raw, int k, double offset) const noexcept;
        /** The same for one of the reader's parameters by ID (ANDOR OSP's macros). */
        void moveById (Values& raw, const char* id, double offset) const noexcept;
        /** Sets in `patch` what product destination k (a voice's control) sets, from `base` (raw
            values) moved by `offset`. */
        void moveControl (Patch& patch, const Values& base, int k, double offset) const noexcept;

    private:
        float value (int index) const noexcept;
        Patch readWith (const float* raw) const noexcept;
        void moveIndex (Values& raw, int index, double offset) const noexcept;
        std::vector<std::atomic<float>*> values;
        std::vector<juce::NormalisableRange<float>> ranges;
        std::vector<int> destinationIndex;   ///< product destination k -> the reader's index
    };

    /** A patch as parameter values (unnormalised), by ID: the factory presets. */
    std::map<juce::String, float> valuesOf (const Patch& patch);
}
