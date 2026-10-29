#include "AnalogParameters.h"

#include "AnalogMath.h"

#include "engine/Shaping.h"

namespace andor::analog::params
{

namespace
{
    // The engine reads these, in this order (Reader's index). ANDOR OSP's IDs are its own.
    enum Index
    {
        iBlend, iTone, iMotion, iDrive, iSpace, iEcho,
        iOsc1Wave, iOsc1Octave, iOsc1Coarse, iOsc1Width, iOsc1Level,
        iOsc2Wave, iOsc2Octave, iOsc2Coarse, iOsc2Fine, iOsc2Width, iOsc2Level, iOsc2KeyTrack, iOsc2LowFreq,
        iNoise, iSync, iPolyphony, iUnison, iUnisonDetune, iSpread,
        iResonance, iKeyTrack, iEnvDepth, iVelocityToFilter, iDriveBias, iDriveTrim,
        iFilterAttack, iFilterDecay, iFilterSustain, iFilterRelease,
        iAmpAttack, iAmpDecay, iAmpSustain, iAmpRelease, iVelocityRange,
        iLfoPitch1, iLfoPitch2, iLfoWidth1, iLfoWidth2, iLfoCutoff,
        iPolyModEnv, iPolyModOsc2, iPolyModToPitch1, iPolyModToWidth1, iPolyModToCutoff,
        iAnalog, iPhase,
        iVoiceMode, iGlide, iBendRange, iFineTune, iSeed, iTune, iPan, iLevel, iMute, iOutput,
        iSpaceType, iSpaceDecay, iSpacePreDelay, iSpaceSize, iSpaceDamping, iSpaceModulation, iSpaceWidth, iSpaceLowCut, iSpaceHighCut,
        iEchoType, iEchoSync, iEchoDivision, iEchoTime, iEchoFeedback, iEchoTone, iEchoAge, iEchoStereo,
        // Version 2: ANDOR OSP's DRIVE, MOVEMENT, REIMAGINED, GRANULAR and LFO 1; FILTER DRIVE, KEY DNA.
        iFilterDrive, iDriveMode, iDriveTone, iDriveBody,
        iMovement, iMovementMode, iDriftSpeed, iDriftPitch, iDriftTone, iTapeWow, iTapeFlutter, iTapeWear,
        iChorusRate, iChorusWidth, iChorusStereo, iPulseRate, iPulseShape, iPulseStereo,
        iShaperPattern, iShaperRate, iShaperTarget, iShaperSmooth, iShaperCustom,
        iReimagined, iReimaginedMode, iKaleidoscopeFocus, iKaleidoscopeSpread, iTapeFrameAge, iTapeFrameStability, iTapeFrameLength,
        iToyboxMotion, iToyboxDigital, iToyboxPlay, iMirageClock, iMirageFilter, iMirageTone,
        iSourceMode, iGrainPosition, iGrainSize, iGrainDensity, iGrainTune, iGrainSpread,
        iLfoShape, iLfoRate, iLfoSync, iLfoDivision, iLfoPhase, iLfoPolarity, iLfoMode, iLfoScope,
        iKeyDna, iKeyDnaAmount, iKeyDnaIndividuality, iKeyDnaPosition, iKeyDnaSize, iKeyDnaDensity, iKeyDnaSpread, iKeyDnaTune,
        count
    };

    const juce::StringArray& ids()
    {
        static const juce::StringArray list {
            blend, tone, motion, "drive", "space", "echo",
            osc1Wave, osc1Octave, osc1Coarse, osc1Width, osc1Level,
            osc2Wave, osc2Octave, osc2Coarse, osc2Fine, osc2Width, osc2Level, osc2KeyTrack, osc2LowFreq,
            noise, sync, polyphony, unison, unisonDetune, spread,
            resonance, keyTrack, envDepth, velocityToFilter, driveBias, driveTrim,
            filterAttack, filterDecay, filterSustain, filterRelease,
            "attack", "decay", "sustainLevel", "release", "velocityRange",
            lfoPitch1, lfoPitch2, lfoWidth1, lfoWidth2, lfoCutoff,
            polyModEnv, polyModOsc2, polyModToPitch1, polyModToWidth1, polyModToCutoff,
            analogAmount, phaseMode,
            "voiceMode", "glide", "bendRange", "fineTune", "seed", "layerA.tune", "layerA.pan", "layerA.level", "layerA.mute", "gain",
            "space.type", "space.decay", "space.preDelay", "space.size", "space.damping", "space.modulation", "space.width", "space.lowCut", "space.highCut",
            "echo.type", "echo.sync", "echo.division", "echo.time", "echo.feedback", "echo.tone", "echo.age", "echo.stereo",
            filterDrive, "drive.mode", "drive.tone", "drive.body",
            "motion", "movement.mode", "movement.drift.speed", "movement.drift.pitch", "movement.drift.tone",
            "movement.tape.wow", "movement.tape.flutter", "movement.tape.wear",
            "movement.chorus.rate", "movement.chorus.width", "movement.chorus.stereo",
            "movement.pulse.rate", "movement.pulse.shape", "movement.pulse.stereo",
            "movement.shaper.pattern", "movement.shaper.rate", "movement.shaper.target", "movement.shaper.smooth", "movement.shaper.custom",
            "reimagined", "layerA.reimagined.mode", "layerA.reimagined.kaleidoscope.focus", "layerA.reimagined.kaleidoscope.spread",
            "layerA.reimagined.tapeFrame.age", "layerA.reimagined.tapeFrame.stability", "layerA.reimagined.tapeFrame.frame",
            "layerA.reimagined.toybox.motion", "layerA.reimagined.toybox.digital", "layerA.reimagined.toybox.play",
            "layerA.reimagined.mirage.clock", "layerA.reimagined.mirage.filter", "layerA.reimagined.mirage.tone",
            "layerA.sourceMode", "layerA.granular.position", "layerA.granular.size", "layerA.granular.density", "layerA.granular.tune", "layerA.granular.spread",
            "mod.lfo1.shape", "mod.lfo1.rate", "mod.lfo1.sync", "mod.lfo1.division", "mod.lfo1.phase", "mod.lfo1.polarity", "mod.lfo1.mode", "mod.lfo1.scope",
            keyDna, keyDnaAmount, keyDnaIndividuality, keyDnaPosition, keyDnaSize, keyDnaDensity, keyDnaSpread, keyDnaTune,
        };
        jassert (list.size() == count);
        return list;
    }

    // Value texts.
    juce::String percentText (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; }
    juce::String signedText (float v, const char* unit, int decimals)
    {
        // (juce::String (x, 0) means full precision, not no decimals: whole numbers are rounded
        // here, so a midpoint that floating point leaves at -2e-6 prints "0", not "2.2e-06".)
        const auto rounded = decimals == 0 ? juce::String (juce::roundToInt (std::abs (v))) : juce::String (std::abs (v), decimals);
        const bool zero = std::abs (rounded.getDoubleValue()) < 1.0e-12;
        return (zero ? juce::String() : (v > 0.0f ? "+" : "-")) + rounded + " " + unit;
    }
    juce::String secondsText (float seconds, int)
    {
        if (seconds < 0.995f)
            return juce::String (juce::roundToInt (seconds * 1000.0f)) + " ms";
        return juce::String (seconds, seconds < 9.95f ? 2 : 1) + " s";
    }
    juce::String hertzText (double hz)
    {
        return hz >= 1000.0 ? juce::String (hz / 1000.0, hz >= 10000.0 ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
    }

    juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi, 0.0f);
        r.setSkewForCentre (centre);
        return r;
    }
}

const juce::StringArray& engineParameterIds()
{
    static const juce::StringArray list = [] {
        auto l = ids();
        // The wheels are read from MIDI and from the on-screen wheels.
        l.add ("midi.pitchWheel");
        l.add ("midi.modWheel");
        // ANDOR OSP's arpeggiator: the shell plays it in front of the synth.
        l.add ("arp.*");
        // ANDOR OSP's modulation: LFO 2, ENV 1 and 2, VEL and NOTE, the 16 routes.
        l.add ("mod.*");
        return l;
    }();
    return list;
}

void addTo (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using Float = juce::AudioParameterFloat;
    using Attributes = juce::AudioParameterFloatAttributes;
    const Patch d;   // the INIT patch: the defaults
    auto id = [] (const juce::String& name) { return juce::ParameterID { name, 1 }; };
    auto percent = [&] (const juce::String& name, const char* title, double value) {
        layout.add (std::make_unique<Float> (id (name), title, juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), static_cast<float> (100.0 * value),
                                             Attributes().withLabel ("%").withStringFromValueFunction (percentText)));
    };
    auto onOff = [&] (const juce::String& name, const char* title, bool value) {
        layout.add (std::make_unique<juce::AudioParameterBool> (id (name), title, value));
    };
    auto choice = [&] (const juce::String& name, const char* title, juce::StringArray items, int value) {
        layout.add (std::make_unique<juce::AudioParameterChoice> (id (name), title, items, value));
    };
    auto integer = [&] (const juce::String& name, const char* title, int lo, int hi, int value, const char* unit) {
        layout.add (std::make_unique<juce::AudioParameterInt> (id (name), title, lo, hi, value,
                                                               juce::AudioParameterIntAttributes().withLabel (unit).withStringFromValueFunction ([unit] (int v, int) {
                                                                   return (v > 0 ? "+" : "") + juce::String (v) + " " + unit;
                                                               })));
    };
    auto time = [&] (const juce::String& name, const char* title, float lo, float hi, float centre, double value) {
        layout.add (std::make_unique<Float> (id (name), title, skewed (lo, hi, centre), static_cast<float> (value),
                                             Attributes().withLabel ("s").withStringFromValueFunction (secondsText)));
    };
    auto bipolar = [&] (const juce::String& name, const char* title, float range, double value, const char* unit, int decimals) {
        layout.add (std::make_unique<Float> (id (name), title, juce::NormalisableRange<float> (-range, range, 0.01f), static_cast<float> (value),
                                             Attributes().withLabel (unit).withStringFromValueFunction ([unit, decimals] (float v, int) {
                                                 return signedText (v, unit, decimals);
                                             })));
    };

    // The macros of its own: BLEND, TONE (the cutoff, shown in Hz); the LFO's amount (the
    // first version's MOTION macro: its ID is kept, its knob is in the TONE popover now).
    percent (blend, "Blend", d.blend);
    layout.add (std::make_unique<Float> (id (tone), "Tone", juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), static_cast<float> (100.0 * d.tone),
                                         Attributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return hertzText (math::toneToHz (0.01 * v)); })));
    percent (motion, "LFO Amount", d.lfoAmount);

    // Oscillators.
    choice (osc1Wave, "Osc 1 Wave", { "Saw", "Pulse", "Saw+Pulse" }, static_cast<int> (d.osc1Wave));
    integer (osc1Octave, "Osc 1 Octave", -2, 2, d.osc1Octave, "oct");
    integer (osc1Coarse, "Osc 1 Coarse", -12, 12, static_cast<int> (d.osc1Coarse), "st");
    layout.add (std::make_unique<Float> (id (osc1Width), "Osc 1 Pulse Width", juce::NormalisableRange<float> (2.0f, 98.0f, 0.1f), static_cast<float> (100.0 * d.osc1Width),
                                         Attributes().withLabel ("%").withStringFromValueFunction (percentText)));
    percent (osc1Level, "Osc 1 Level", d.osc1Level);
    choice (osc2Wave, "Osc 2 Wave", { "Saw", "Triangle", "Pulse", "Saw+Tri", "Saw+Pulse", "Tri+Pulse", "All" }, static_cast<int> (d.osc2Wave));
    integer (osc2Octave, "Osc 2 Octave", -2, 2, d.osc2Octave, "oct");
    integer (osc2Coarse, "Osc 2 Coarse", -12, 12, static_cast<int> (d.osc2Coarse), "st");
    bipolar (osc2Fine, "Osc 2 Fine", 100.0f, d.osc2FineCents, "ct", 0);
    layout.add (std::make_unique<Float> (id (osc2Width), "Osc 2 Pulse Width", juce::NormalisableRange<float> (2.0f, 98.0f, 0.1f), static_cast<float> (100.0 * d.osc2Width),
                                         Attributes().withLabel ("%").withStringFromValueFunction (percentText)));
    percent (osc2Level, "Osc 2 Level", d.osc2Level);
    onOff (osc2KeyTrack, "Osc 2 Keyboard", d.osc2KeyTrack);
    onOff (osc2LowFreq, "Osc 2 Low Freq", d.osc2LowFrequency);
    percent (noise, "Noise", d.noiseLevel);
    onOff (sync, "Sync", d.sync);

    // Voices.
    choice (polyphony, "Voices", { "1", "2", "4", "8", "16" }, 3);
    onOff (unison, "Unison", d.unison);
    percent (unisonDetune, "Unison Detune", d.unisonDetune);
    percent (spread, "Stereo Spread", d.stereoSpread);

    // Filter.
    percent (resonance, "Resonance", d.resonance);
    percent (keyTrack, "Filter Keyboard", d.keyTrack);
    bipolar (envDepth, "Filter Env Amount", 60.0f, d.envDepthSemitones, "st", 1);
    percent (velocityToFilter, "Velocity to Filter", d.velocityToFilter);
    percent (driveBias, "Drive Bias", d.driveBias);
    layout.add (std::make_unique<Float> (id (driveTrim), "Drive Trim", juce::NormalisableRange<float> (-12.0f, 6.0f, 0.1f), static_cast<float> (d.driveTrimDb),
                                         Attributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return signedText (v, "dB", 1); })));
    time (filterAttack, "Filter Attack", 0.001f, 30.0f, 0.25f, d.filterAttack);
    time (filterDecay, "Filter Decay", 0.005f, 40.0f, 0.6f, d.filterDecay);
    percent (filterSustain, "Filter Sustain", d.filterSustain);
    time (filterRelease, "Filter Release", 0.005f, 45.0f, 0.6f, d.filterRelease);

    // The first version's LFO (unused since version 2: the LFO is ANDOR OSP's LFO 1, mod.lfo1.*,
    // and sessions saved with these are migrated to it). Kept so their IDs never change.
    choice (lfoWave, "LFO Wave (v1, unused)", { "Sine", "Triangle", "Ramp", "Square", "S&H" }, 1);
    layout.add (std::make_unique<Float> (id (lfoRate), "LFO Rate (v1, unused)", skewed (0.02f, 25.0f, 2.0f), 4.0f,
                                         Attributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) {
                                             return juce::String (v, v < 1.0f ? 2 : 1) + " Hz";
                                         })));
    onOff (lfoSync, "LFO Sync (v1, unused)", false);
    {
        juce::StringArray divisions;
        for (int i = 0; i < lfoDivisionCount; ++i)
            divisions.add (lfoDivisionName (i));
        choice (lfoDivision, "LFO Division (v1, unused)", divisions, 6);
    }
    // The LFO's depths (the LFO itself is LFO 1 of the modulation panel).
    bipolar (lfoPitch1, "LFO to Osc 1 Pitch", 12.0f, d.lfoPitch1, "st", 2);
    bipolar (lfoPitch2, "LFO to Osc 2 Pitch", 12.0f, d.lfoPitch2, "st", 2);
    bipolar (lfoWidth1, "LFO to Osc 1 Width", 45.0f, 100.0 * d.lfoWidth1, "%", 0);
    bipolar (lfoWidth2, "LFO to Osc 2 Width", 45.0f, 100.0 * d.lfoWidth2, "%", 0);
    bipolar (lfoCutoff, "LFO to Cutoff", 48.0f, d.lfoCutoff, "st", 1);

    // Poly Mod.
    percent (polyModEnv, "Poly Mod Filter Env", d.polyModEnv);
    percent (polyModOsc2, "Poly Mod Osc 2", d.polyModOsc2);
    onOff (polyModToPitch1, "Poly Mod to Osc 1 Pitch", d.polyModToPitch1);
    onOff (polyModToWidth1, "Poly Mod to Osc 1 Width", d.polyModToWidth1);
    onOff (polyModToCutoff, "Poly Mod to Cutoff", d.polyModToCutoff);

    // ANALOG: the voices' tolerances and wander, and the oscillators' phase at a note.
    percent (analogAmount, "Analog", d.analogAmount);
    choice (phaseMode, "Osc Phase", { "Free", "Reset" }, static_cast<int> (d.phaseMode));

    // Version 2. FILTER DRIVE: the ladder's input saturation (the DRIVE macro is ANDOR OSP's
    // DRIVE now). KEY DNA: each key's own grains (GRANULAR), as ANDOR OSP's KeyDna.
    auto v2 = [] (const juce::String& name) { return juce::ParameterID { name, 2 }; };
    auto percent2 = [&] (const juce::String& name, const char* title, double value) {
        layout.add (std::make_unique<Float> (v2 (name), title, juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), static_cast<float> (100.0 * value),
                                             Attributes().withLabel ("%").withStringFromValueFunction (percentText)));
    };
    percent2 (filterDrive, "Filter Drive", d.filterDrive);
    const osp::keydna::Settings dna;
    layout.add (std::make_unique<juce::AudioParameterBool> (v2 (keyDna), "Key DNA", dna.enabled));
    percent2 (keyDnaAmount, "Key DNA Amount", dna.amount);
    percent2 (keyDnaIndividuality, "Key DNA Individuality", dna.individuality);
    percent2 (keyDnaPosition, "Key DNA Position", keyDnaDefaultVariation);
    percent2 (keyDnaSize, "Key DNA Size", keyDnaDefaultVariation);
    percent2 (keyDnaDensity, "Key DNA Density", keyDnaDefaultVariation);
    percent2 (keyDnaSpread, "Key DNA Spread", keyDnaDefaultVariation);
    percent2 (keyDnaTune, "Key DNA Tune", 0.0);
}

//==============================================================================
namespace
{
    /** A voice's control moved by modulation: what its value sets in the patch (as read()). */
    void assign (Patch& p, int i, double v) noexcept
    {
        switch (i)
        {
            case iBlend: p.blend = 0.01 * v; break;
            case iTone: p.tone = 0.01 * v; break;
            case iMotion: p.lfoAmount = 0.01 * v; break;
            case iOsc1Coarse: p.osc1Coarse = std::round (v); break;
            case iOsc1Width: p.osc1Width = 0.01 * v; break;
            case iOsc1Level: p.osc1Level = 0.01 * v; break;
            case iOsc2Coarse: p.osc2Coarse = std::round (v); break;
            case iOsc2Fine: p.osc2FineCents = v; break;
            case iOsc2Width: p.osc2Width = 0.01 * v; break;
            case iOsc2Level: p.osc2Level = 0.01 * v; break;
            case iNoise: p.noiseLevel = 0.01 * v; break;
            case iResonance: p.resonance = 0.01 * v; break;
            case iKeyTrack: p.keyTrack = 0.01 * v; break;
            case iEnvDepth: p.envDepthSemitones = v; break;
            case iVelocityToFilter: p.velocityToFilter = 0.01 * v; break;
            case iDriveTrim: p.driveTrimDb = v; break;
            case iFilterAttack: p.filterAttack = v; break;
            case iFilterDecay: p.filterDecay = v; break;
            case iFilterSustain: p.filterSustain = 0.01 * v; break;
            case iFilterRelease: p.filterRelease = v; break;
            case iVelocityRange: p.velocityRangeDb = v; break;
            case iLfoPitch1: p.lfoPitch1 = v; break;
            case iLfoPitch2: p.lfoPitch2 = v; break;
            case iLfoWidth1: p.lfoWidth1 = 0.01 * v; break;
            case iLfoWidth2: p.lfoWidth2 = 0.01 * v; break;
            case iLfoCutoff: p.lfoCutoff = v; break;
            case iPolyModEnv: p.polyModEnv = 0.01 * v; break;
            case iPolyModOsc2: p.polyModOsc2 = 0.01 * v; break;
            case iAnalog: p.analogAmount = 0.01 * v; break;
            case iFilterDrive: p.filterDrive = 0.01 * v; break;
            case iGrainTune: p.grains.tuneSemitones = v; break;
            default: break;
        }
    }

    struct Destination
    {
        int index;
        const char* name;
        osp::mod::Owner owner;
    };

    /** ANDOR ANALOG's own modulation destinations (append only: sessions store the order).
        Every continuous control the engine reads that has no ANDOR OSP destination; voice-
        owned ones are what each note plays with (a POLY source moves each note its own way),
        global ones are the instrument's (its effects, its performance settings). */
    const std::vector<Destination>& destinations()
    {
        using O = osp::mod::Owner;
        static const std::vector<Destination> list {
            { iBlend, "BLEND", O::voice }, { iTone, "TONE", O::voice }, { iResonance, "RESONANCE", O::voice },
            { iEnvDepth, "FILTER ENV AMOUNT", O::voice }, { iKeyTrack, "KEY TRACK", O::voice }, { iVelocityToFilter, "VEL TO FILTER", O::voice },
            { iFilterDrive, "FILTER DRIVE", O::voice }, { iDriveTrim, "FILTER TRIM", O::voice },
            { iFilterAttack, "FILTER ATTACK", O::voice }, { iFilterDecay, "FILTER DECAY", O::voice },
            { iFilterSustain, "FILTER SUSTAIN", O::voice }, { iFilterRelease, "FILTER RELEASE", O::voice },
            { iOsc1Coarse, "OSC 1 SEMI", O::voice }, { iOsc1Width, "OSC 1 WIDTH", O::voice }, { iOsc1Level, "OSC 1 LEVEL", O::voice },
            { iOsc2Coarse, "OSC 2 SEMI", O::voice }, { iOsc2Fine, "OSC 2 FINE", O::voice }, { iOsc2Width, "OSC 2 WIDTH", O::voice },
            { iOsc2Level, "OSC 2 LEVEL", O::voice }, { iNoise, "NOISE", O::voice },
            { iPolyModEnv, "POLY MOD ENV", O::voice }, { iPolyModOsc2, "POLY MOD OSC 2", O::voice },
            { iMotion, "LFO AMOUNT", O::voice }, { iLfoPitch1, "LFO OSC 1 PITCH", O::voice }, { iLfoPitch2, "LFO OSC 2 PITCH", O::voice },
            { iLfoWidth1, "LFO OSC 1 PW", O::voice }, { iLfoWidth2, "LFO OSC 2 PW", O::voice }, { iLfoCutoff, "LFO CUTOFF", O::voice },
            { iAnalog, "ANALOG", O::voice }, { iVelocityRange, "VELOCITY RANGE", O::voice }, { iGrainTune, "GRAIN TUNE", O::voice },
            { iDriveBias, "FILTER BIAS", O::global }, { iUnisonDetune, "UNISON DETUNE", O::global }, { iSpread, "STEREO SPREAD", O::global },
            { iGlide, "GLIDE", O::global }, { iFineTune, "FINE TUNE", O::global }, { iOutput, "VOLUME", O::global },
            { iDriveTone, "DRIVE TONE", O::global }, { iDriveBody, "DRIVE BODY", O::global },
            { iDriftSpeed, "DRIFT SPEED", O::global }, { iDriftPitch, "DRIFT PITCH", O::global }, { iDriftTone, "DRIFT TONE", O::global },
            { iTapeWow, "TAPE WOW", O::global }, { iTapeFlutter, "TAPE FLUTTER", O::global }, { iTapeWear, "TAPE WEAR", O::global },
            { iChorusRate, "CHORUS RATE", O::global }, { iChorusWidth, "CHORUS WIDTH", O::global }, { iChorusStereo, "CHORUS STEREO", O::global },
            { iPulseRate, "PULSE RATE", O::global }, { iPulseShape, "PULSE SHAPE", O::global }, { iPulseStereo, "PULSE STEREO", O::global },
            { iShaperSmooth, "SHAPER SMOOTH", O::global },
            { iSpaceDecay, "SPACE DECAY", O::global }, { iSpacePreDelay, "SPACE PRE-DELAY", O::global }, { iSpaceSize, "SPACE SIZE", O::global },
            { iSpaceDamping, "SPACE DAMPING", O::global }, { iSpaceModulation, "SPACE MODULATION", O::global }, { iSpaceWidth, "SPACE WIDTH", O::global },
            { iSpaceLowCut, "SPACE LOW CUT", O::global }, { iSpaceHighCut, "SPACE HIGH CUT", O::global },
            { iEchoTime, "ECHO TIME", O::global }, { iEchoFeedback, "ECHO FEEDBACK", O::global }, { iEchoTone, "ECHO TONE", O::global },
            { iEchoAge, "ECHO AGE", O::global },
            { iKaleidoscopeFocus, "KALEIDOSCOPE FOCUS", O::global }, { iKaleidoscopeSpread, "KALEIDOSCOPE SPREAD", O::global },
            { iTapeFrameAge, "TAPE FRAME AGE", O::global }, { iTapeFrameStability, "TAPE FRAME STABILITY", O::global },
            { iToyboxMotion, "TOYBOX MOTION", O::global }, { iToyboxDigital, "TOYBOX DIGITAL", O::global },
            { iMirageClock, "MIRAGE CLOCK", O::global }, { iMirageFilter, "MIRAGE FILTER", O::global },
        };
        return list;
    }
}

const std::vector<osp::mod::ProductDest>& modulationDestinations()
{
    static const std::vector<osp::mod::ProductDest> list = [] {
        std::vector<osp::mod::ProductDest> l;
        for (const auto& d : destinations())
            l.push_back ({ ids()[d.index].toStdString(), d.name, d.owner, osp::mod::Update::continuous });
        jassert (static_cast<int> (l.size()) <= osp::mod::maxProductDests);
        return l;
    }();
    return list;
}

void Reader::attach (juce::AudioProcessorValueTreeState& state)
{
    static_assert (count <= maxValues);
    values.clear();
    ranges.clear();
    for (const auto& name : ids())
    {
        auto* v = state.getRawParameterValue (name);
        jassert (v != nullptr);
        values.push_back (v);
        auto* parameter = state.getParameter (name);
        ranges.push_back (parameter != nullptr ? parameter->getNormalisableRange() : juce::NormalisableRange<float>());
    }
    destinationIndex.clear();
    for (const auto& d : destinations())
        destinationIndex.push_back (d.index);
}

void Reader::snapshot (Values& raw) const noexcept
{
    for (int i = 0; i < count && i < static_cast<int> (values.size()); ++i)
        raw[static_cast<std::size_t> (i)] = value (i);
}

Patch Reader::read (const Values& raw) const noexcept
{
    return readWith (raw.data());
}

bool Reader::move (Values& raw, int k, double offset) const noexcept
{
    if (k < 0 || k >= static_cast<int> (destinationIndex.size()) || ranges.size() != static_cast<std::size_t> (count))
        return false;
    moveIndex (raw, destinationIndex[static_cast<std::size_t> (k)], offset);
    return true;
}

void Reader::moveIndex (Values& raw, int index, double offset) const noexcept
{
    const auto i = static_cast<std::size_t> (index);
    const auto& range = ranges[i];
    const double at = std::clamp (static_cast<double> (range.convertTo0to1 (std::clamp (raw[i], range.start, range.end))) + offset, 0.0, 1.0);
    raw[i] = range.convertFrom0to1 (static_cast<float> (at));
}

void Reader::moveById (Values& raw, const char* id, double offset) const noexcept
{
    // (Four IDs, compared on the audio thread without allocating.)
    for (int i = 0; i < count && ranges.size() == static_cast<std::size_t> (count); ++i)
        if (ids()[i] == id)
        {
            moveIndex (raw, i, offset);
            return;
        }
}

void Reader::moveControl (Patch& patch, const Values& base, int k, double offset) const noexcept
{
    if (k < 0 || k >= static_cast<int> (destinationIndex.size()))
        return;
    auto raw = base[static_cast<std::size_t> (destinationIndex[static_cast<std::size_t> (k)])];
    Values one;   // (only the moved value is read back)
    const auto i = static_cast<std::size_t> (destinationIndex[static_cast<std::size_t> (k)]);
    one[i] = raw;
    if (move (one, k, offset))
        assign (patch, static_cast<int> (i), static_cast<double> (one[i]));
}

float Reader::value (int index) const noexcept
{
    const auto* v = values[static_cast<std::size_t> (index)];
    return v != nullptr ? v->load (std::memory_order_relaxed) : 0.0f;
}

Patch Reader::read() const noexcept
{
    return readWith (nullptr);
}

Patch Reader::readWith (const float* raw) const noexcept
{
    Patch p;
    if (values.size() != static_cast<std::size_t> (count))
        return p;
    auto at = [this, raw] (int i) { return raw != nullptr ? raw[i] : value (i); };
    auto v = [&at] (int i) { return static_cast<double> (at (i)); };
    auto on = [&at] (int i) { return at (i) >= 0.5f; };
    auto index = [&at] (int i, int n) { return std::clamp (juce::roundToInt (at (i)), 0, n - 1); };
    p.blend = 0.01 * v (iBlend);
    p.tone = 0.01 * v (iTone);
    p.lfoAmount = 0.01 * v (iMotion);
    p.drive = 0.01 * v (iDrive);
    p.space = 0.01 * v (iSpace);
    p.echo = 0.01 * v (iEcho);
    p.osc1Wave = static_cast<Osc1Wave> (index (iOsc1Wave, 3));
    p.osc1Octave = juce::roundToInt (v (iOsc1Octave));
    p.osc1Coarse = std::round (v (iOsc1Coarse));
    p.osc1Width = 0.01 * v (iOsc1Width);
    p.osc1Level = 0.01 * v (iOsc1Level);
    p.osc2Wave = static_cast<Osc2Wave> (index (iOsc2Wave, 7));
    p.osc2Octave = juce::roundToInt (v (iOsc2Octave));
    p.osc2Coarse = std::round (v (iOsc2Coarse));
    p.osc2FineCents = v (iOsc2Fine);
    p.osc2Width = 0.01 * v (iOsc2Width);
    p.osc2Level = 0.01 * v (iOsc2Level);
    p.osc2KeyTrack = on (iOsc2KeyTrack);
    p.osc2LowFrequency = on (iOsc2LowFreq);
    p.noiseLevel = 0.01 * v (iNoise);
    p.sync = on (iSync);
    p.polyphony = polyphonyFromChoice (index (iPolyphony, polyphonyChoiceCount));
    p.unison = on (iUnison);
    p.unisonDetune = 0.01 * v (iUnisonDetune);
    p.stereoSpread = 0.01 * v (iSpread);
    p.resonance = 0.01 * v (iResonance);
    p.keyTrack = 0.01 * v (iKeyTrack);
    p.envDepthSemitones = v (iEnvDepth);
    p.velocityToFilter = 0.01 * v (iVelocityToFilter);
    p.driveBias = 0.01 * v (iDriveBias);
    p.driveTrimDb = v (iDriveTrim);
    p.filterAttack = v (iFilterAttack);
    p.filterDecay = v (iFilterDecay);
    p.filterSustain = 0.01 * v (iFilterSustain);
    p.filterRelease = v (iFilterRelease);
    // ANDOR OSP's amplitude envelope (milliseconds; its sustain in %).
    p.ampAttack = 0.001 * v (iAmpAttack);
    p.ampDecay = 0.001 * v (iAmpDecay);
    p.ampSustain = 0.01 * v (iAmpSustain);
    p.ampRelease = 0.001 * v (iAmpRelease);
    p.velocityRangeDb = v (iVelocityRange);
    p.lfoPitch1 = v (iLfoPitch1);
    p.lfoPitch2 = v (iLfoPitch2);
    p.lfoWidth1 = 0.01 * v (iLfoWidth1);
    p.lfoWidth2 = 0.01 * v (iLfoWidth2);
    p.lfoCutoff = v (iLfoCutoff);
    p.polyModEnv = 0.01 * v (iPolyModEnv);
    p.polyModOsc2 = 0.01 * v (iPolyModOsc2);
    p.polyModToPitch1 = on (iPolyModToPitch1);
    p.polyModToWidth1 = on (iPolyModToWidth1);
    p.polyModToCutoff = on (iPolyModToCutoff);
    p.analogAmount = 0.01 * v (iAnalog);
    p.phaseMode = static_cast<PhaseMode> (index (iPhase, 2));
    p.voiceMode = static_cast<VoiceMode> (index (iVoiceMode, 2));
    p.glideSeconds = 0.001 * v (iGlide);
    p.bendRangeSemitones = v (iBendRange);
    p.fineTuneCents = v (iFineTune);
    p.seed = static_cast<std::uint32_t> (std::max (1L, std::lround (v (iSeed))));
    p.tuneSemitones = v (iTune);
    p.panPercent = v (iPan);
    p.levelDb = v (iLevel);
    p.mute = on (iMute);
    p.outputDb = v (iOutput);
    auto& s = p.effects;
    s.spaceType = static_cast<osp::SpaceType> (index (iSpaceType, 4));
    s.spaceDecaySeconds = v (iSpaceDecay);
    s.spacePreDelayMs = v (iSpacePreDelay);
    s.spaceSize = 0.01 * v (iSpaceSize);
    s.spaceDamping = 0.01 * v (iSpaceDamping);
    s.spaceModulation = 0.01 * v (iSpaceModulation);
    s.spaceWidth = 0.01 * v (iSpaceWidth);
    s.spaceLowCutHz = v (iSpaceLowCut);
    s.spaceHighCutHz = v (iSpaceHighCut);
    s.echoType = static_cast<osp::EchoType> (index (iEchoType, 2));
    s.echoSync = on (iEchoSync);
    s.echoDivision = index (iEchoDivision, osp::shaping::echoDivisionCount);
    s.echoTimeMs = v (iEchoTime);
    s.echoFeedback = 0.01 * v (iEchoFeedback);
    s.echoTone = 0.01 * v (iEchoTone);
    s.echoAge = 0.01 * v (iEchoAge);
    s.echoStereo = static_cast<osp::EchoStereo> (index (iEchoStereo, 3));

    // Version 2: ANDOR OSP's settings, converted exactly as ANDOR OSP converts them.
    p.filterDrive = 0.01 * v (iFilterDrive);
    s.driveMode = static_cast<osp::DriveMode> (index (iDriveMode, 3));
    s.driveTone = 0.01 * v (iDriveTone);
    s.driveBody = 0.01 * v (iDriveBody);
    p.motion = 0.01 * v (iMovement);
    s.movementMode = static_cast<osp::MovementMode> (index (iMovementMode, 5));
    s.driftSpeed = 0.01 * v (iDriftSpeed);
    s.driftPitch = 0.01 * v (iDriftPitch);
    s.driftTone = 0.01 * v (iDriftTone);
    s.tapeWow = 0.01 * v (iTapeWow);
    s.tapeFlutter = 0.01 * v (iTapeFlutter);
    s.tapeWear = 0.01 * v (iTapeWear);
    s.chorusRate = 0.01 * v (iChorusRate);
    s.chorusWidth = 0.01 * v (iChorusWidth);
    s.chorusStereo = 0.01 * v (iChorusStereo);
    s.pulseRate = 0.01 * v (iPulseRate);
    s.pulseShape = 0.01 * v (iPulseShape);
    s.pulseStereo = 0.01 * v (iPulseStereo);
    s.shaper.pattern = index (iShaperPattern, osp::RhythmicShaper::patternCount);
    s.shaper.rate = static_cast<osp::ShaperRate> (index (iShaperRate, 6));
    s.shaper.target = static_cast<osp::ShaperTarget> (index (iShaperTarget, 3));
    s.shaper.smooth = 0.01 * v (iShaperSmooth);
    s.shaper.custom = on (iShaperCustom);
    p.reimagined = 0.01 * v (iReimagined);
    auto& r = p.reimaginedSettings;
    r.mode = osp::reimagined::modeFromIndex (index (iReimaginedMode, osp::reimaginedModeCount));
    r.kaleidoscope = { 0.01 * v (iKaleidoscopeFocus), 0.01 * v (iKaleidoscopeSpread) };
    r.tapeFrame = { 0.01 * v (iTapeFrameAge), 0.01 * v (iTapeFrameStability), static_cast<osp::TapeFrameLength> (index (iTapeFrameLength, 3)) };
    r.toybox = { 0.01 * v (iToyboxMotion), 0.01 * v (iToyboxDigital), static_cast<osp::ToyboxPlay> (index (iToyboxPlay, 3)) };
    r.mirage = { 0.01 * v (iMirageClock), 0.01 * v (iMirageFilter), static_cast<osp::MirageTone> (index (iMirageTone, 2)) };
    p.granular = on (iSourceMode);
    p.grains.position = 0.01 * v (iGrainPosition);
    p.grains.sizeSeconds = 0.001 * v (iGrainSize);
    p.grains.density = v (iGrainDensity);
    p.grains.tuneSemitones = v (iGrainTune);
    p.grains.spread = 0.01 * v (iGrainSpread);
    auto& lfo = p.lfo;
    lfo.shape = static_cast<osp::mod::LfoShape> (index (iLfoShape, osp::mod::lfoShapeCount));
    lfo.rateHz = v (iLfoRate);
    lfo.sync = on (iLfoSync);
    lfo.division = index (iLfoDivision, osp::mod::syncDivisionCount);
    lfo.phase = 0.01 * v (iLfoPhase);
    lfo.bipolar = ! on (iLfoPolarity);
    lfo.mode = static_cast<osp::mod::LfoMode> (index (iLfoMode, 3));
    lfo.scope = on (iLfoScope) ? osp::mod::Scope::poly : osp::mod::Scope::global;
    return p;
}

osp::keydna::Settings Reader::keyDna (std::uint32_t seed) const noexcept
{
    osp::keydna::Settings k;
    if (values.size() != static_cast<std::size_t> (count))
        return k;
    k.enabled = value (iKeyDna) >= 0.5f;
    k.amount = 0.01 * value (iKeyDnaAmount);
    k.individuality = 0.01 * value (iKeyDnaIndividuality);
    k.seed = seed;
    using osp::keydna::Param;
    k[Param::position].variation = 0.01 * value (iKeyDnaPosition);
    k[Param::size].variation = 0.01 * value (iKeyDnaSize);
    k[Param::density].variation = 0.01 * value (iKeyDnaDensity);
    k[Param::spread].variation = 0.01 * value (iKeyDnaSpread);
    k[Param::tune].variation = 0.01 * value (iKeyDnaTune);
    return k;
}

std::map<juce::String, float> valuesOf (const Patch& p)
{
    std::map<juce::String, float> m;
    auto put = [&m] (int index, double value) { m[ids()[index]] = static_cast<float> (value); };
    put (iBlend, 100.0 * p.blend);
    put (iTone, 100.0 * p.tone);
    put (iMotion, 100.0 * p.lfoAmount);
    put (iDrive, 100.0 * p.drive);
    put (iSpace, 100.0 * p.space);
    put (iEcho, 100.0 * p.echo);
    put (iOsc1Wave, static_cast<int> (p.osc1Wave));
    put (iOsc1Octave, p.osc1Octave);
    put (iOsc1Coarse, p.osc1Coarse);
    put (iOsc1Width, 100.0 * p.osc1Width);
    put (iOsc1Level, 100.0 * p.osc1Level);
    put (iOsc2Wave, static_cast<int> (p.osc2Wave));
    put (iOsc2Octave, p.osc2Octave);
    put (iOsc2Coarse, p.osc2Coarse);
    put (iOsc2Fine, p.osc2FineCents);
    put (iOsc2Width, 100.0 * p.osc2Width);
    put (iOsc2Level, 100.0 * p.osc2Level);
    put (iOsc2KeyTrack, p.osc2KeyTrack ? 1.0 : 0.0);
    put (iOsc2LowFreq, p.osc2LowFrequency ? 1.0 : 0.0);
    put (iNoise, 100.0 * p.noiseLevel);
    put (iSync, p.sync ? 1.0 : 0.0);
    int polyIndex = 0;
    for (int i = 0; i < polyphonyChoiceCount; ++i)
        if (polyphonyFromChoice (i) <= p.polyphony)
            polyIndex = i;
    put (iPolyphony, polyIndex);
    put (iUnison, p.unison ? 1.0 : 0.0);
    put (iUnisonDetune, 100.0 * p.unisonDetune);
    put (iSpread, 100.0 * p.stereoSpread);
    put (iResonance, 100.0 * p.resonance);
    put (iKeyTrack, 100.0 * p.keyTrack);
    put (iEnvDepth, p.envDepthSemitones);
    put (iVelocityToFilter, 100.0 * p.velocityToFilter);
    put (iDriveBias, 100.0 * p.driveBias);
    put (iDriveTrim, p.driveTrimDb);
    put (iFilterAttack, p.filterAttack);
    put (iFilterDecay, p.filterDecay);
    put (iFilterSustain, 100.0 * p.filterSustain);
    put (iFilterRelease, p.filterRelease);
    put (iAmpAttack, 1000.0 * p.ampAttack);
    put (iAmpDecay, 1000.0 * p.ampDecay);
    put (iAmpSustain, 100.0 * p.ampSustain);
    put (iAmpRelease, 1000.0 * p.ampRelease);
    put (iVelocityRange, p.velocityRangeDb);
    put (iLfoPitch1, p.lfoPitch1);
    put (iLfoPitch2, p.lfoPitch2);
    put (iLfoWidth1, 100.0 * p.lfoWidth1);
    put (iLfoWidth2, 100.0 * p.lfoWidth2);
    put (iLfoCutoff, p.lfoCutoff);
    put (iPolyModEnv, 100.0 * p.polyModEnv);
    put (iPolyModOsc2, 100.0 * p.polyModOsc2);
    put (iPolyModToPitch1, p.polyModToPitch1 ? 1.0 : 0.0);
    put (iPolyModToWidth1, p.polyModToWidth1 ? 1.0 : 0.0);
    put (iPolyModToCutoff, p.polyModToCutoff ? 1.0 : 0.0);
    put (iAnalog, 100.0 * p.analogAmount);
    put (iPhase, static_cast<int> (p.phaseMode));
    put (iVoiceMode, static_cast<int> (p.voiceMode));
    put (iGlide, 1000.0 * p.glideSeconds);
    put (iBendRange, p.bendRangeSemitones);
    put (iFineTune, p.fineTuneCents);
    put (iSeed, p.seed);
    put (iTune, p.tuneSemitones);
    put (iPan, p.panPercent);
    put (iLevel, p.levelDb);
    put (iMute, p.mute ? 1.0 : 0.0);
    put (iOutput, p.outputDb);
    const auto& s = p.effects;
    put (iSpaceType, static_cast<int> (s.spaceType));
    put (iSpaceDecay, s.spaceDecaySeconds);
    put (iSpacePreDelay, s.spacePreDelayMs);
    put (iSpaceSize, 100.0 * s.spaceSize);
    put (iSpaceDamping, 100.0 * s.spaceDamping);
    put (iSpaceModulation, 100.0 * s.spaceModulation);
    put (iSpaceWidth, 100.0 * s.spaceWidth);
    put (iSpaceLowCut, s.spaceLowCutHz);
    put (iSpaceHighCut, s.spaceHighCutHz);
    put (iEchoType, static_cast<int> (s.echoType));
    put (iEchoSync, s.echoSync ? 1.0 : 0.0);
    put (iEchoDivision, s.echoDivision);
    put (iEchoTime, s.echoTimeMs);
    put (iEchoFeedback, 100.0 * s.echoFeedback);
    put (iEchoTone, 100.0 * s.echoTone);
    put (iEchoAge, 100.0 * s.echoAge);
    put (iEchoStereo, static_cast<int> (s.echoStereo));
    put (iFilterDrive, 100.0 * p.filterDrive);
    put (iDriveMode, static_cast<int> (s.driveMode));
    put (iDriveTone, 100.0 * s.driveTone);
    put (iDriveBody, 100.0 * s.driveBody);
    put (iMovement, 100.0 * p.motion);
    put (iMovementMode, static_cast<int> (s.movementMode));
    put (iDriftSpeed, 100.0 * s.driftSpeed);
    put (iDriftPitch, 100.0 * s.driftPitch);
    put (iDriftTone, 100.0 * s.driftTone);
    put (iTapeWow, 100.0 * s.tapeWow);
    put (iTapeFlutter, 100.0 * s.tapeFlutter);
    put (iTapeWear, 100.0 * s.tapeWear);
    put (iChorusRate, 100.0 * s.chorusRate);
    put (iChorusWidth, 100.0 * s.chorusWidth);
    put (iChorusStereo, 100.0 * s.chorusStereo);
    put (iPulseRate, 100.0 * s.pulseRate);
    put (iPulseShape, 100.0 * s.pulseShape);
    put (iPulseStereo, 100.0 * s.pulseStereo);
    put (iShaperPattern, s.shaper.pattern);
    put (iShaperRate, static_cast<int> (s.shaper.rate));
    put (iShaperTarget, static_cast<int> (s.shaper.target));
    put (iShaperSmooth, 100.0 * s.shaper.smooth);
    put (iShaperCustom, s.shaper.custom ? 1.0 : 0.0);
    const auto& r = p.reimaginedSettings;
    put (iReimagined, 100.0 * p.reimagined);
    put (iReimaginedMode, static_cast<int> (r.mode));
    put (iKaleidoscopeFocus, 100.0 * r.kaleidoscope.focus);
    put (iKaleidoscopeSpread, 100.0 * r.kaleidoscope.spread);
    put (iTapeFrameAge, 100.0 * r.tapeFrame.age);
    put (iTapeFrameStability, 100.0 * r.tapeFrame.stability);
    put (iTapeFrameLength, static_cast<int> (r.tapeFrame.frame));
    put (iToyboxMotion, 100.0 * r.toybox.motion);
    put (iToyboxDigital, 100.0 * r.toybox.digital);
    put (iToyboxPlay, static_cast<int> (r.toybox.play));
    put (iMirageClock, 100.0 * r.mirage.clock);
    put (iMirageFilter, 100.0 * r.mirage.filter);
    put (iMirageTone, static_cast<int> (r.mirage.tone));
    put (iSourceMode, p.granular ? 1.0 : 0.0);
    put (iGrainPosition, 100.0 * p.grains.position);
    put (iGrainSize, 1000.0 * p.grains.sizeSeconds);
    put (iGrainDensity, p.grains.density);
    put (iGrainTune, p.grains.tuneSemitones);
    put (iGrainSpread, 100.0 * p.grains.spread);
    put (iLfoShape, static_cast<int> (p.lfo.shape));
    put (iLfoRate, p.lfo.rateHz);
    put (iLfoSync, p.lfo.sync ? 1.0 : 0.0);
    put (iLfoDivision, p.lfo.division);
    put (iLfoPhase, 100.0 * p.lfo.phase);
    put (iLfoPolarity, p.lfo.bipolar ? 0.0 : 1.0);
    put (iLfoMode, static_cast<int> (p.lfo.mode));
    put (iLfoScope, p.lfo.scope == osp::mod::Scope::poly ? 1.0 : 0.0);
    return m;
}

} // namespace andor::analog::params
