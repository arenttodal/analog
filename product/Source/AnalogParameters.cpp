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
        iLfoWave, iLfoRate, iLfoSync, iLfoDivision, iLfoPitch1, iLfoPitch2, iLfoWidth1, iLfoWidth2, iLfoCutoff,
        iPolyModEnv, iPolyModOsc2, iPolyModToPitch1, iPolyModToWidth1, iPolyModToCutoff,
        iAnalog, iPhase,
        iVoiceMode, iGlide, iBendRange, iFineTune, iSeed, iTune, iPan, iLevel, iMute, iOutput,
        iSpaceType, iSpaceDecay, iSpacePreDelay, iSpaceSize, iSpaceDamping, iSpaceModulation, iSpaceWidth, iSpaceLowCut, iSpaceHighCut,
        iEchoType, iEchoSync, iEchoDivision, iEchoTime, iEchoFeedback, iEchoTone, iEchoAge, iEchoStereo,
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
            lfoWave, lfoRate, lfoSync, lfoDivision, lfoPitch1, lfoPitch2, lfoWidth1, lfoWidth2, lfoCutoff,
            polyModEnv, polyModOsc2, polyModToPitch1, polyModToWidth1, polyModToCutoff,
            analogAmount, phaseMode,
            "voiceMode", "glide", "bendRange", "fineTune", "seed", "layerA.tune", "layerA.pan", "layerA.level", "layerA.mute", "gain",
            "space.type", "space.decay", "space.preDelay", "space.size", "space.damping", "space.modulation", "space.width", "space.lowCut", "space.highCut",
            "echo.type", "echo.sync", "echo.division", "echo.time", "echo.feedback", "echo.tone", "echo.age", "echo.stereo",
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

    // The macros of its own: BLEND, TONE (the cutoff, shown in Hz), MOTION.
    percent (blend, "Blend", d.blend);
    layout.add (std::make_unique<Float> (id (tone), "Tone", juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f), static_cast<float> (100.0 * d.tone),
                                         Attributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return hertzText (math::toneToHz (0.01 * v)); })));
    percent (motion, "Motion", d.movement);

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

    // LFO.
    choice (lfoWave, "LFO Wave", { "Sine", "Triangle", "Ramp", "Square", "S&H" }, static_cast<int> (d.lfoWave));
    layout.add (std::make_unique<Float> (id (lfoRate), "LFO Rate", skewed (0.02f, 25.0f, 2.0f), static_cast<float> (d.lfoRateHz),
                                         Attributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) {
                                             return juce::String (v, v < 1.0f ? 2 : 1) + " Hz";
                                         })));
    onOff (lfoSync, "LFO Sync", d.lfoSync);
    {
        juce::StringArray divisions;
        for (int i = 0; i < lfoDivisionCount; ++i)
            divisions.add (lfoDivisionName (i));
        choice (lfoDivision, "LFO Division", divisions, d.lfoDivision);
    }
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
}

//==============================================================================
void Reader::attach (juce::AudioProcessorValueTreeState& state)
{
    values.clear();
    for (const auto& name : ids())
    {
        auto* v = state.getRawParameterValue (name);
        jassert (v != nullptr);
        values.push_back (v);
    }
}

float Reader::value (int index) const noexcept
{
    const auto* v = values[static_cast<std::size_t> (index)];
    return v != nullptr ? v->load (std::memory_order_relaxed) : 0.0f;
}

Patch Reader::read() const noexcept
{
    Patch p;
    if (values.size() != static_cast<std::size_t> (count))
        return p;
    auto v = [this] (int i) { return static_cast<double> (value (i)); };
    auto on = [this] (int i) { return value (i) >= 0.5f; };
    auto index = [this] (int i, int n) { return std::clamp (juce::roundToInt (value (i)), 0, n - 1); };
    p.blend = 0.01 * v (iBlend);
    p.tone = 0.01 * v (iTone);
    p.movement = 0.01 * v (iMotion);
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
    p.lfoWave = static_cast<LfoWave> (index (iLfoWave, 5));
    p.lfoRateHz = v (iLfoRate);
    p.lfoSync = on (iLfoSync);
    p.lfoDivision = index (iLfoDivision, lfoDivisionCount);
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
    return p;
}

std::map<juce::String, float> valuesOf (const Patch& p)
{
    std::map<juce::String, float> m;
    auto put = [&m] (int index, double value) { m[ids()[index]] = static_cast<float> (value); };
    put (iBlend, 100.0 * p.blend);
    put (iTone, 100.0 * p.tone);
    put (iMotion, 100.0 * p.movement);
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
    put (iLfoWave, static_cast<int> (p.lfoWave));
    put (iLfoRate, p.lfoRateHz);
    put (iLfoSync, p.lfoSync ? 1.0 : 0.0);
    put (iLfoDivision, p.lfoDivision);
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
    return m;
}

} // namespace andor::analog::params
