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
    // State version 8: three engines (docs/THREE_ENGINE_ARCHITECTURE.md).
    inline const juce::String osc1Fine = "analog.osc1.fine";
    inline const juce::String hybridType = "analog.hybrid.type";
    inline const juce::String osc3Model = "analog.osc3.model";
    inline const juce::String osc3Wave = "analog.osc3.wave";
    inline const juce::String osc3Octave = "analog.osc3.octave";
    inline const juce::String osc3Coarse = "analog.osc3.coarse";
    inline const juce::String osc3Fine = "analog.osc3.fine";
    inline const juce::String osc3Width = "analog.osc3.width";
    inline const juce::String lfoWidth3 = "analog.lfo.width3";
    inline const juce::String osc3DcoSaw = "analog.osc3.dco.saw";
    inline const juce::String osc3DcoPulse = "analog.osc3.dco.pulse";
    inline const juce::String osc3DcoSub = "analog.osc3.dco.subLevel";
    inline const juce::String osc3DcoPwmSource = "analog.osc3.dco.pwmSource";
    inline const juce::String osc3DcoPwmDepth = "analog.osc3.dco.pwmDepth";
    inline const juce::String osc3WideWave = "analog.osc3.wide.wave";
    inline const juce::String osc3WideShape = "analog.osc3.wide.shape";
    inline const juce::String osc3WideSpread = "analog.osc3.wide.spread";
    inline const juce::String hybridNoiseColor = "analog.hybrid.noise.color";
    inline const juce::String hybridNoiseTone = "analog.hybrid.noise.tone";
    inline const juce::String hybridNoiseMovement = "analog.hybrid.noise.movement";
    inline const juce::String hybridOctave = "analog.hybrid.octave";
    inline const juce::String hybridFine = "analog.hybrid.fine";
    inline const juce::String osc2Velo = "analog.osc2.velo";
    inline const juce::String hybridVelo = "analog.hybrid.velo";
    inline const juce::String hybridNoiseKeys = "analog.hybrid.noise.keys";
    /** State version 8's own parameters (appended), for its migration and tests. */
    const juce::StringArray& version8Ids();
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
    /** State version 10: the mod wheel brings in LFO 1's depths (off: it moves only its routes). */
    inline const juce::String lfoWheel = "analog.lfo.wheel";
    /** State version 11: MOVEMENT's lanes. Lane A is MOVEMENT itself ("movement.*") with its
        own ON, LEVEL, SYNC, CYCLE and PHASE here; lane B has all of MOVEMENT's settings under
        its prefix ("analog.movement.b." + "mode", "drift.speed", ... "shaper.custom") and the
        same five. */
    inline const juce::String movementAPrefix = "analog.movement.a.";
    inline const juce::String movementBPrefix = "analog.movement.b.";
    /** A lane's own (0 A, 1 B): "on", "level", "sync", "pulse.division", "phase"; for lane B
        also every MOVEMENT setting's name. */
    /** State version 15 (Cinematic V6 gate 2): SUB, an independent polyphonic sub oscillator on
        the musician's own keys, joined after the effects (docs/CINEMATIC_V6_GATE2.md). */
    inline const juce::String subEnabled = "analog.sub.enabled";
    inline const juce::String subWave = "analog.sub.wave";
    inline const juce::String subOctave = "analog.sub.octave";
    inline const juce::String subGrit = "analog.sub.grit";
    inline const juce::String subLevel = "analog.sub.level";
    inline const juce::String subLowKey = "analog.sub.lowKey";
    inline const juce::String subHighKey = "analog.sub.highKey";
    inline const juce::String subResponse = "analog.sub.response";
    inline const juce::String subTaperStart = "analog.sub.taperStart";
    inline const juce::String subTaperSlope = "analog.sub.taperSlope";
    /** State version 16 (Cinematic V6 gate 3): card C's second layer (C2), summed into card C's
        part (Patch::hybrid2). Its own source (a file or an instrument patch) is the engine's
        Hybrid2Source state child. */
    inline const juce::String hybrid2Enabled = "analog.hybrid2.enabled";
    inline const juce::String hybrid2Type = "analog.hybrid2.type";
    inline const juce::String hybrid2Level = "analog.hybrid2.level";
    inline const juce::String hybrid2Octave = "analog.hybrid2.octave";
    inline const juce::String hybrid2Coarse = "analog.hybrid2.coarse";
    inline const juce::String hybrid2Fine = "analog.hybrid2.fine";
    inline const juce::String hybrid2Velo = "analog.hybrid2.velo";
    inline const juce::String hybrid2Model = "analog.hybrid2.osc.model";
    inline const juce::String hybrid2Wave = "analog.hybrid2.osc.wave";
    inline const juce::String hybrid2Width = "analog.hybrid2.osc.width";
    inline const juce::String hybrid2DcoSaw = "analog.hybrid2.osc.dco.saw";
    inline const juce::String hybrid2DcoPulse = "analog.hybrid2.osc.dco.pulse";
    inline const juce::String hybrid2DcoSub = "analog.hybrid2.osc.dco.subLevel";
    inline const juce::String hybrid2DcoPwmSource = "analog.hybrid2.osc.dco.pwmSource";
    inline const juce::String hybrid2DcoPwmDepth = "analog.hybrid2.osc.dco.pwmDepth";
    inline const juce::String hybrid2WideWave = "analog.hybrid2.osc.wide.wave";
    inline const juce::String hybrid2WideShape = "analog.hybrid2.osc.wide.shape";
    inline const juce::String hybrid2WideSpread = "analog.hybrid2.osc.wide.spread";
    inline const juce::String hybrid2NoiseColor = "analog.hybrid2.noise.color";
    inline const juce::String hybrid2NoiseTone = "analog.hybrid2.noise.tone";
    inline const juce::String hybrid2NoiseMovement = "analog.hybrid2.noise.movement";
    inline const juce::String hybrid2NoiseKeys = "analog.hybrid2.noise.keys";
    /** State version 17 (Cinematic V6 gate 4): each card's SEND, heard only with a MAIN + SEND
        route (card C's: both its layers'; SUB has none, by design). */
    inline const juce::String sendA = "analog.send.a";
    inline const juce::String sendB = "analog.send.b";
    inline const juce::String sendC = "analog.send.c";
    void addSendsTo (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
    /** State version 18 (SHAPER DNA V1, docs/SHAPER_DNA_V1.md): MOVEMENT lane A's SHAPER per note.
        OFF in a new instrument and in every session and preset from before. */
    inline const juce::String dnaEnabled = "analog.movement.shaperDna.enabled";
    inline const juce::String dnaAssignment = "analog.movement.shaperDna.assignment";
    inline const juce::String dnaProfiles = "analog.movement.shaperDna.profiles";
    inline const juce::String dnaNotesPerProfile = "analog.movement.shaperDna.notesPerProfile";
    inline const juce::String dnaSplitAB = "analog.movement.shaperDna.splitAB";
    inline const juce::String dnaSplitBC = "analog.movement.shaperDna.splitBC";
    /** A profile's (0 A, 1 B, 2 C) pattern or rate: "analog.movement.shaperDna.a.pattern" ... */
    inline juce::String dnaProfileId (int profile, const char* name) { return juce::String ("analog.movement.shaperDna.") + static_cast<char> ('a' + profile) + "." + name; }
    /** Appends SHAPER DNA's parameters (after the sends). */
    void addShaperDnaTo (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
    /** State version 19 (USER MACROS V1.1, docs/USER_MACROS_V1_1.md): the six USER macros,
        0..100 %, at 0 in a new instrument and every session from before. Their IDs never change
        when a macro is renamed (the name is the patch's, the host sees "User Macro n"). */
    inline juce::String userMacroId (int macro) { return "analog.userMacro." + juce::String (macro + 1); }
    /** Appends the USER macros' parameters (after SHAPER DNA's). */
    void addUserMacrosTo (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
    /** The reader's index of a parameter ID (message thread), or -1: what a USER macro's
        assignment compiles to. */
    int readerIndexOf (const juce::String& id);
    /** The reader's index of USER macro `macro`'s own value. */
    int userMacroReaderIndex (int macro) noexcept;
    /** State version 20 (SATELLITE, docs/features/SATELLITE_RHYTHM_ARCHITECTURE.md): each card's
        own stereo motion, card 0 A, 1 B, 2 C: "analog.satellite.a.enabled" ... Off in a new
        instrument and every session from before. */
    inline juce::String satelliteId (int card, const char* name) { return juce::String ("analog.satellite.") + static_cast<char> ('a' + card) + "." + name; }
    inline constexpr const char* satelliteNames[] = { "enabled", "mode", "sync", "division", "rate", "depth", "width", "phase", "offset", "retrigger" };
    inline constexpr int satelliteFields = 10;
    /** State version 21 (RHYTHMIC MORPH): card C's recording as a host-synced loop and C RHYTHM. */
    inline const juce::String rhythmEnabled = "analog.rhythm.enabled";
    inline const juce::String rhythmAudio = "analog.rhythm.audio";
    inline const juce::String rhythmSync = "analog.rhythm.sync";
    inline const juce::String rhythmTempo = "analog.rhythm.tempo";
    inline const juce::String rhythmShape = "analog.rhythm.shape";
    inline const juce::String rhythmAttack = "analog.rhythm.attack";
    inline const juce::String rhythmRelease = "analog.rhythm.release";
    inline const juce::String rhythmSensitivity = "analog.rhythm.sensitivity";
    inline const juce::String rhythmDepth = "analog.rhythm.depth";
    // State version 23: D / AUX (docs/aux). Append-only.
    inline const juce::String auxEnabled = "analog.aux.enabled";
    inline const juce::String auxMode = "analog.aux.mode";
    inline const juce::String auxLevel = "analog.aux.level";
    inline const juce::String auxMute = "analog.aux.mute";
    inline const juce::String auxSolo = "analog.aux.solo";
    inline const juce::String auxFilterFollow = "analog.aux.filterFollow";
    inline const juce::String auxSync = "analog.aux.rhythm.sync";
    inline const juce::String auxRate = "analog.aux.rhythm.rate";
    inline const juce::String auxModAmount = "analog.aux.rhythm.modAmount";
    inline const juce::String auxModTarget = "analog.aux.rhythm.modTarget";
    inline const juce::String auxSensitivity = "analog.aux.rhythm.sensitivity";
    inline const juce::String auxAttack = "analog.aux.rhythm.attack";
    inline const juce::String auxRelease = "analog.aux.rhythm.release";
    inline const juce::String auxNoiseColor = "analog.aux.noise.color";
    inline const juce::String auxNoiseTone = "analog.aux.noise.tone";
    inline const juce::String auxNoiseGate = "analog.aux.noise.gate";
    inline const juce::String auxNoiseAttack = "analog.aux.noise.attack";
    inline const juce::String auxNoiseRelease = "analog.aux.noise.release";
    inline const juce::String auxNoiseMovement = "analog.aux.noise.movement";
    inline const juce::String auxNoiseWidth = "analog.aux.noise.width";
    // AUX Addendum 03: D RHYTHM's transposition (new IDs; nothing reused). auxFilterFollow stays,
    // hidden and unread (state recall only): D always goes through the FILTER now.
    inline const juce::String auxPitch = "analog.aux.rhythm.pitch";     ///< -12..12 semitones
    inline const juce::String auxOctave = "analog.aux.rhythm.octave";   ///< -2..2 octaves
    /** D MORPH (docs/aux/MORPH_DECISION.md, Option D): the second, global MORPH stage, A/B/C
        (after the FILTER) x D; 0 %: off (D layered). Its own control, never the MORPH macro. */
    inline const juce::String auxMorph = "analog.aux.morph";
    /** D RHYTHM's GATE: KEYS (a key starts the loop from its first beat; it plays while a key
        is held, then fades with the AMP's release) or FREE (always playing, on the host's
        grid). Appended; KEYS by default. */
    inline const juce::String auxRhythmGate = "analog.aux.rhythm.gate";
    /** MORPH TO (Addendum 05): which MORPH the MORPH macro drives - C HYBRID (the per-voice
        A + B <-> C morph, as ever; the default) or D AUX (the global stage: A/B/C's bus after the
        FILTER x D, whatever D plays - LIVE INPUT included). One processor at a time; the other's
        settings stay as they are. Appended. */
    inline const juce::String morphTarget = "analog.morph.target";
    /** RATE's choices (its musical speed against the loop's detected tempo). */
    inline const std::array<double, 5> auxRates { 0.25, 0.5, 1.0, 2.0, 4.0 };
    /** Appends RHYTHMIC MORPH's parameters (after SATELLITE's). */
    void addRhythmTo (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
    /** Appends D / AUX's parameters (after card C's GRANULAR is read; state version 23). */
    void addAuxTo (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
    /** Appends SATELLITE's parameters (after the USER macros'). */
    void addSatelliteTo (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
    /** Appends card C's second layer's parameters (after SUB's). */
    void addHybrid2To (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
    /** Appends SUB's parameters (after the FX library's: Product's addParameters). */
    void addSubTo (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
    inline juce::String movementLaneId (int lane, const char* name) { return (lane == 0 ? movementAPrefix : movementBPrefix) + name; }
    /** MOVEMENT's settings' names (each lane has them all: lane A as "movement." + name). */
    const juce::StringArray& movementSettingNames();
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
        /** KEY DNA before state version 6 (the analog.keyDna.* parameters): for migration. */
        osp::keydna::Settings keyDna (std::uint32_t seed) const noexcept;
        /** KEY DNA's AMOUNT (layerA.keyDna.amount), 0..1. */
        double keyDnaCardAmount() const noexcept;

        // Modulation (real-time safe).
        static constexpr int maxValues = 512;
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
        /** Moves the reader's value `index` by `offset` (normalised, clamped at its ends): what a
            USER macro's assignments add (real-time safe). */
        void moveIndex (Values& raw, int index, double offset) const noexcept;

    private:
        float value (int index) const noexcept;
        Patch readWith (const float* raw) const noexcept;
        std::vector<std::atomic<float>*> values;
        std::vector<juce::NormalisableRange<float>> ranges;
        std::vector<int> destinationIndex;   ///< product destination k -> the reader's index
    };

    /** A patch as parameter values (unnormalised), by ID: the factory presets. */
    std::map<juce::String, float> valuesOf (const Patch& patch);
}
