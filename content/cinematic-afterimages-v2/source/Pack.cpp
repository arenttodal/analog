#include "Pack.h"

#include "AnalogMath.h"
#include "FactoryPatches.h"
#include "Unison.h"

#include <map>

namespace ca2
{

using namespace andor::analog;

const char* familyName (Family f)
{
    static const char* names[] { "Fundamental Weight", "Corrosion", "Monumental Low End", "Endless Distance", "Human Voltage",
                                 "Slow Light", "Synthetic Orchestra", "Musical Machines", "Dark Matter", "Fragile Machines" };
    return names[std::clamp (static_cast<int> (f), 0, 9)];
}

const char* familySubtitle (Family f)
{
    static const char* names[] { "Fat, musical analog basses", "Distorted, aggressive and industrial basses",
                                 "Cinematic bass drones and colossal hybrid foundations", "Huge reverb-drenched leads", "Expressive analog leads",
                                 "Lush evolving pads", "Brass, strings and cinematic ensembles", "Arpeggios and rhythmic instruments",
                                 "Drones, tension and noir atmospheres", "Keys, bells, plucks and cinematic effects" };
    return names[std::clamp (static_cast<int> (f), 0, 9)];
}

std::string Spec::id() const
{
    char text[16];
    std::snprintf (text, sizeof (text), "CA2-%03d", number);
    return text;
}

// The families' definitions (one file each).
void addWeight (std::vector<Spec>&);
void addCorrosion (std::vector<Spec>&);
void addMonumental (std::vector<Spec>&);
void addDistance (std::vector<Spec>&);
void addVoltage (std::vector<Spec>&);
void addSlowLight (std::vector<Spec>&);
void addOrchestra (std::vector<Spec>&);
void addMachines (std::vector<Spec>&);
void addDarkMatter (std::vector<Spec>&);
void addFragile (std::vector<Spec>&);

const std::vector<Spec>& collection()
{
    static const auto list = [] {
        std::vector<Spec> l;
        addWeight (l);
        addCorrosion (l);
        addMonumental (l);
        addDistance (l);
        addVoltage (l);
        addSlowLight (l);
        addOrchestra (l);
        addMachines (l);
        addDarkMatter (l);
        addFragile (l);
        return l;
    }();
    return list;
}

double trimOf (const Spec& spec)
{
    // trims.inc: { number, dB }, written by the calibration ([ca2-calibrate]); absent: 0.
    static const std::map<int, double> trims {
#include "trims.inc"
    };
    const auto it = trims.find (spec.number);
    return it != trims.end() ? it->second : 0.0;
}

void configure (ShellProcessor& p, const Spec& spec, bool withTrim)
{
    Patch patch = design::canvas();
    spec.patch (patch);
    const double trim = withTrim ? trimOf (spec) : 0.0;
    // One loudness trim for every source the preset plays (the oscillators' LEVEL, SUB, card C's
    // two layers): the balance stays the designer's, the master VOLUME stays at 0 dB.
    // (LEVEL reaches +6 dB; the rest of a positive trim goes to the voice's TRIM after the filter.)
    const double level = patch.levelDb + trim;
    patch.levelDb = std::min (6.0, level);
    patch.driveTrimDb = std::clamp (patch.driveTrimDb + std::max (0.0, level - 6.0), -12.0, 6.0);
    if (patch.sub.enabled)
        patch.sub.levelDb = std::min (0.0, patch.sub.levelDb + trim);
    if (patch.hybrid2.enabled)
        patch.hybrid2.levelDb = std::min (6.0, patch.hybrid2.levelDb + trim);
    // (As the factory set: the oscillators' mix in their levels, BLEND at its centre.)
    foldBlend (patch.blend, patch.osc1Level, patch.osc2Level);
    patch.blend = 0.5;
    const auto values = params::valuesOf (patch);
    for (auto* base : p.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (base))
        {
            const auto it = values.find (ranged->getParameterID());
            const float value = it != values.end() ? it->second : ranged->convertFrom0to1 (ranged->getDefaultValue());
            ranged->setValueNotifyingHost (ranged->convertTo0to1 (value));
        }
    // Card C's LEVEL is the shell's (a Patch does not write it).
    design::set (p, "layerB.level", static_cast<float> (std::min (6.0, patch.organic.levelDb + trim)));
    if (spec.extra != nullptr)
        spec.extra (p);
}

juce::File contentFolder()
{
    return juce::File (__FILE__).getParentDirectory().getParentDirectory();
}

juce::File presetFolder()
{
    return contentFolder().getChildFile ("presets").getChildFile ("Cinematic Afterimages Vol. 2");
}

juce::File fileFor (const Spec& spec)
{
    return presetFolder().getChildFile (familyName (spec.family)).getChildFile (juce::File::createLegalFileName (juce::String (spec.name)) + ".andoranalog");
}

namespace design
{
    Patch canvas()
    {
        Patch p;
        p.morph.enabled = false;
        p.space = 0.0;
        p.echo = 0.0;
        p.motion = 0.0;
        p.drive = 0.0;
        p.lfo.shape = osp::mod::LfoShape::triangle;
        p.lfo.rateHz = 5.0;
        p.lfoAmount = 0.0;
        p.lfoPitch1 = p.lfoPitch2 = 0.0;
        p.lfoWidth1 = p.lfoWidth2 = 0.0;
        p.osc1Level = 0.8;
        p.osc2Level = 0.6;
        p.analogAmount = 0.3;
        p.levelDb = 0.0;
        p.organic.levelDb = -48.0;   // (card C silent unless a preset gives it a voice)
        return p;
    }

    double hz (double cutoffHz) { return math::hzToTone (cutoffHz); }

    void filterEnv (Patch& p, double a, double d, double s, double r, double depthSemitones)
    {
        p.filterAttack = a;
        p.filterDecay = d;
        p.filterSustain = s;
        p.filterRelease = r;
        p.envDepthSemitones = depthSemitones;
    }

    void ampEnv (Patch& p, double a, double d, double s, double r)
    {
        p.ampAttack = a;
        p.ampDecay = d;
        p.ampSustain = s;
        p.ampRelease = r;
    }

    void mono (Patch& p, bool legato, double glideSeconds)
    {
        p.voiceMode = VoiceMode::mono;
        p.polyphony = 1;
        p.legato = legato;
        p.glideSeconds = glideSeconds;
    }

    void vibratoOnWheel (Patch& p, double cents, double rateHz, double delaySeconds, double riseSeconds)
    {
        p.vibrato.enabled = true;
        p.vibrato.depthCents = cents;
        p.vibrato.rateHz = rateHz;
        p.vibrato.delaySeconds = delaySeconds;
        p.vibrato.riseSeconds = riseSeconds;
        p.vibrato.control = osp::vibrato::Control::modWheel;
    }

    void vibratoAlways (Patch& p, double cents, double rateHz, double delaySeconds, double riseSeconds)
    {
        vibratoOnWheel (p, cents, rateHz, delaySeconds, riseSeconds);
        p.vibrato.control = osp::vibrato::Control::alwaysOn;
    }

    void unisonCard (Patch& p, int card, int voices, double detune, double blend, double width, UnisonSpread spread)
    {
        auto& u = p.oscUnison[static_cast<std::size_t> (std::clamp (card, 0, 2))];
        u.voices = voices;
        u.detune = detune;
        u.blend = blend;
        u.width = width;
        u.spread = spread;
    }

    void sub (Patch& p, andor::analog::SubSettings::Wave wave, int octave, double levelDb, int lowKey, int highKey, double grit)
    {
        p.sub.enabled = true;
        p.sub.wave = wave;
        p.sub.octave = octave;
        p.sub.levelDb = levelDb;
        p.sub.lowKey = lowKey;
        p.sub.highKey = highKey;
        p.sub.grit = grit;
    }

    void osc3 (Patch& p, Osc2Wave wave, int octave, double coarse, double fine, double levelDb)
    {
        p.hybridType = HybridType::oscillator;
        p.osc3Model = OscModel::classic;
        p.osc3Wave = wave;
        p.osc3Octave = octave;
        p.osc3Coarse = coarse;
        p.osc3FineCents = fine;
        p.organic.levelDb = levelDb;
    }

    void noiseC (Patch& p, NoiseColor color, double tone, double movement, double levelDb)
    {
        p.hybridType = HybridType::noise;
        p.hybridNoiseColor = color;
        p.hybridNoiseTone = tone;
        p.hybridNoiseMovement = movement;
        p.organic.levelDb = levelDb;
    }

    void room (Patch& p, osp::SpaceType type, double amount, double decaySeconds, double preDelayMs, double highCutHz, double lowCutHz)
    {
        p.space = amount;
        p.effects.spaceEngine = osp::SpaceEngine::rooms;
        p.effects.spaceType = type;
        p.effects.spaceDecaySeconds = decaySeconds;
        p.effects.spacePreDelayMs = preDelayMs;
        p.effects.spaceHighCutHz = highCutHz;
        p.effects.spaceLowCutHz = lowCutHz;
    }

    void abyss (Patch& p, osp::AbyssVariant variant, double amount, double decaySeconds, double preDelayMs, double highCutHz)
    {
        p.space = amount;
        p.effects.spaceEngine = osp::SpaceEngine::abyss;
        p.effects.abyssVariant = variant;
        p.effects.spaceDecaySeconds = decaySeconds;
        p.effects.spacePreDelayMs = preDelayMs;
        p.effects.spaceHighCutHz = highCutHz;
    }

    void vintage (Patch& p, osp::VintageVariant variant, double amount, double decaySeconds, double preDelayMs, double highCutHz)
    {
        p.space = amount;
        p.effects.spaceEngine = osp::SpaceEngine::vintage;
        p.effects.vintageVariant = variant;
        p.effects.spaceDecaySeconds = decaySeconds;
        p.effects.spacePreDelayMs = preDelayMs;
        p.effects.spaceHighCutHz = highCutHz;
    }

    void bloom (Patch& p, osp::BloomVariant variant, double amount, double decaySeconds, int division)
    {
        p.space = amount;
        p.effects.spaceEngine = osp::SpaceEngine::bloom;
        p.effects.bloomVariant = variant;
        p.effects.spaceDecaySeconds = decaySeconds;
        p.effects.bloomSync = true;
        p.effects.bloomDivision = division;
    }

    void echo (Patch& p, osp::EchoType type, double amount, int division, double feedback, double tone, osp::EchoStereo stereo)
    {
        p.echo = amount;
        p.effects.echoType = type;
        p.effects.echoSync = true;
        p.effects.echoDivision = division;   // 0 1/16, 1 1/8T, 2 1/16D, 3 1/8, 4 1/4T, 5 1/8D, 6 1/4
        p.effects.echoFeedback = feedback;
        p.effects.echoTone = tone;
        p.effects.echoStereo = stereo;
    }

    void drive (Patch& p, osp::DriveMode mode, double amount, double tone, double body)
    {
        p.drive = amount;
        p.effects.driveMode = mode;
        p.effects.driveTone = tone;
        p.effects.driveBody = body;
    }

    void drift (Patch& p, double amount, double speed, double pitch, double tone)
    {
        p.motion = amount;
        p.effects.movementMode = osp::MovementMode::drift;
        p.effects.driftSpeed = speed;
        p.effects.driftPitch = pitch;
        p.effects.driftTone = tone;
    }

    void tape (Patch& p, double amount, double wow, double flutter, double wear)
    {
        p.motion = amount;
        p.effects.movementMode = osp::MovementMode::tape;
        p.effects.tapeWow = wow;
        p.effects.tapeFlutter = flutter;
        p.effects.tapeWear = wear;
    }

    void chorusMovement (Patch& p, double amount, double rate, double width, double stereo)
    {
        p.motion = amount;
        p.effects.movementMode = osp::MovementMode::chorus;
        p.effects.chorusRate = rate;
        p.effects.chorusWidth = width;
        p.effects.chorusStereo = stereo;
    }

    void pulse (Patch& p, double amount, double rate, double shape, double stereo, bool sync, int division)
    {
        p.motion = amount;
        p.effects.movementMode = osp::MovementMode::pulse;
        p.effects.pulseRate = rate;
        p.effects.pulseShape = shape;
        p.effects.pulseStereo = stereo;
        p.effects.pulseSync = sync;
        p.effects.pulseDivision = division;
    }

    void route (ShellProcessor& p, int slot, osp::mod::Source source, osp::mod::Dest dest, float depthPercent)
    {
        using Host = osp::plugin::InstrumentHost;
        p.setParameterValue (Host::modRouteId (slot, "source"), static_cast<float> (osp::mod::sourceIndex (source) + 1));
        p.setParameterValue (Host::modRouteId (slot, "dest"), static_cast<float> (osp::mod::choiceOf (dest)));
        p.setParameterValue (Host::modRouteId (slot, "depth"), depthPercent);
        p.setParameterValue (Host::modRouteId (slot, "enabled"), 1.0f);
    }

    void choose (ShellProcessor& p, const juce::String& id, const juce::String& choice)
    {
        auto* c = dynamic_cast<juce::AudioParameterChoice*> (p.apvts().getParameter (id));
        jassert (c != nullptr && c->choices.contains (choice));
        if (c != nullptr)
            c->setValueNotifyingHost (c->convertTo0to1 (static_cast<float> (c->choices.indexOf (choice))));
    }

    void set (ShellProcessor& p, const juce::String& id, float plainValue)
    {
        auto* r = p.apvts().getParameter (id);
        jassert (r != nullptr);
        if (r != nullptr)
            r->setValueNotifyingHost (r->convertTo0to1 (plainValue));
    }

    void arp (ShellProcessor& p, const juce::String& pattern, const juce::String& rate, int octaves, float gatePercent)
    {
        set (p, osp::plugin::ids::arpEnabled, 1.0f);
        choose (p, osp::plugin::ids::arpPattern, pattern);
        choose (p, osp::plugin::ids::arpRate, rate);
        set (p, osp::plugin::ids::arpOctaves, static_cast<float> (octaves));
        set (p, osp::plugin::ids::arpGate, gatePercent);
    }
}

} // namespace ca2
