#include "FxParameters.h"

#include <cmath>
#include "fx/FxDsp.h"
#include "fx/Effects.h"

namespace andor::analog::fxparams
{

using fx::Settings;
using Kind = Spec::Kind;

namespace
{
    // Builders: a real parameter (linear or skewed about `centre`), a choice, a switch.
    Spec real (const char* name, const char* title, float lo, float hi, float def, const char* unit, std::function<void (Settings&, int, float)> w, float centre = -1.0f)
    {
        Spec s;
        s.name = name;
        s.title = title;
        s.kind = Kind::real;
        s.low = lo;
        s.high = hi;
        s.fallback = def;
        s.unit = unit;
        s.centre = centre;
        s.write = std::move (w);
        return s;
    }
    Spec choice (const char* name, const char* title, juce::StringArray items, int def, std::function<void (Settings&, int, float)> w)
    {
        Spec s;
        s.name = name;
        s.title = title;
        s.kind = Kind::choice;
        s.low = 0.0f;
        s.high = static_cast<float> (items.size() - 1);
        s.fallback = static_cast<float> (def);
        s.choices = std::move (items);
        s.write = std::move (w);
        return s;
    }
    Spec toggle (const char* name, const char* title, bool def, std::function<void (Settings&, int, float)> w)
    {
        Spec s;
        s.name = name;
        s.title = title;
        s.kind = Kind::toggle;
        s.fallback = def ? 1.0f : 0.0f;
        s.write = std::move (w);
        return s;
    }
    int index (float v) { return juce::roundToInt (v); }
    bool on (float v) { return v >= 0.5f; }
    float pct (float v) { return 0.01f * v; }

    /** ON, MIX and OUTPUT: every new effect's (MIX's default its own). */
    template <typename Member>
    void common (std::vector<Spec>& out, Member member, float mixDefault)
    {
        out.push_back (toggle ("on", "On", true, [member] (Settings& s, int k, float v) { (s.*member)[static_cast<std::size_t> (k)].on = on (v); }));
        out.push_back (real ("mix", "Mix", 0.0f, 100.0f, mixDefault, "%", [member] (Settings& s, int k, float v) { (s.*member)[static_cast<std::size_t> (k)].mix = pct (v); }));
        out.push_back (real ("output", "Output", -18.0f, 12.0f, 0.0f, "dB", [member] (Settings& s, int k, float v) { (s.*member)[static_cast<std::size_t> (k)].outputDb = v; }));
    }

    juce::StringArray divisions()
    {
        juce::StringArray d;
        for (const auto* n : fx::dsp::divisionNames)
            d.add (n);
        return d;
    }

    std::vector<Spec> build (fx::Type type)
    {
        std::vector<Spec> o;
        using fx::Type;
#define AT(member) s.member[static_cast<std::size_t> (k)]
        switch (type)
        {
            case Type::spread:
                common (o, &Settings::spread, 50.0f);
                o.push_back (choice ("mode", "Mode", { "Hyper", "Dimension", "Both" }, 2, [] (Settings& s, int k, float v) { AT (spread).mode = index (v); }));
                o.push_back (real ("width", "Width", 0.0f, 200.0f, 100.0f, "%", [] (Settings& s, int k, float v) { AT (spread).width = pct (v); }));
                o.push_back (choice ("voices", "Hyper Voices", { "2", "4", "6", "8" }, 1, [] (Settings& s, int k, float v) { AT (spread).voices = index (v); }));
                o.push_back (real ("detune", "Detune", 0.0f, 60.0f, 8.0f, "cents", [] (Settings& s, int k, float v) { AT (spread).detuneCents = v; }));
                o.push_back (real ("hyperRate", "Hyper Rate", 0.03f, 5.0f, 0.3f, "Hz", [] (Settings& s, int k, float v) { AT (spread).hyperRateHz = v; }, 0.5f));
                o.push_back (real ("dimSize", "Dimension Size", 0.0f, 100.0f, 42.0f, "%", [] (Settings& s, int k, float v) { AT (spread).dimSize = pct (v); }));
                o.push_back (real ("dimRate", "Dimension Rate", 0.03f, 3.0f, 0.35f, "Hz", [] (Settings& s, int k, float v) { AT (spread).dimRateHz = v; }, 0.4f));
                o.push_back (real ("dimAmount", "Dimension Amount", 0.0f, 100.0f, 35.0f, "%", [] (Settings& s, int k, float v) { AT (spread).dimAmount = pct (v); }));
                o.push_back (toggle ("lowMono", "Low Mono", true, [] (Settings& s, int k, float v) { AT (spread).lowMono = on (v); }));
                o.push_back (real ("lowMonoHz", "Low Mono Frequency", 50.0f, 400.0f, 130.0f, "Hz", [] (Settings& s, int k, float v) { AT (spread).lowMonoHz = v; }, 150.0f));
                break;
            case Type::compressor:
                common (o, &Settings::comp, 100.0f);
                o.push_back (choice ("mode", "Mode", { "Clean", "FET 76", "Multiband" }, 0, [] (Settings& s, int k, float v) { AT (comp).mode = index (v); }));
                o.push_back (real ("threshold", "Threshold", -60.0f, 0.0f, -18.0f, "dB", [] (Settings& s, int k, float v) { AT (comp).thresholdDb = v; }));
                o.push_back (real ("ratio", "Ratio", 1.0f, 20.0f, 3.0f, "x", [] (Settings& s, int k, float v) { AT (comp).ratio = v; }, 4.0f));
                o.push_back (real ("attack", "Attack", 0.1f, 100.0f, 10.0f, "ms", [] (Settings& s, int k, float v) { AT (comp).attackMs = v; }, 10.0f));
                o.push_back (real ("release", "Release", 10.0f, 1500.0f, 180.0f, "ms", [] (Settings& s, int k, float v) { AT (comp).releaseMs = v; }, 200.0f));
                o.push_back (real ("knee", "Knee", 0.0f, 18.0f, 6.0f, "dB", [] (Settings& s, int k, float v) { AT (comp).kneeDb = v; }));
                o.push_back (real ("makeup", "Makeup", -12.0f, 24.0f, 0.0f, "dB", [] (Settings& s, int k, float v) { AT (comp).makeupDb = v; }));
                o.push_back (choice ("detector", "Detector", { "RMS", "Peak" }, 0, [] (Settings& s, int k, float v) { AT (comp).peak = index (v) == 1; }));
                o.push_back (real ("link", "Stereo Link", 0.0f, 100.0f, 100.0f, "%", [] (Settings& s, int k, float v) { AT (comp).link = pct (v); }));
                o.push_back (real ("sidechain", "Sidechain HP", 20.0f, 400.0f, 20.0f, "Hz", [] (Settings& s, int k, float v) { AT (comp).sidechainHz = v; }, 80.0f));
                o.push_back (toggle ("autoMakeup", "Auto Makeup", false, [] (Settings& s, int k, float v) { AT (comp).autoMakeup = on (v); }));
                o.push_back (real ("fet.input", "FET Input", -12.0f, 48.0f, 12.0f, "dB", [] (Settings& s, int k, float v) { AT (comp).fetInputDb = v; }));
                o.push_back (real ("fet.output", "FET Output", -36.0f, 18.0f, -12.0f, "dB", [] (Settings& s, int k, float v) { AT (comp).fetOutputDb = v; }));
                o.push_back (real ("fet.attack", "FET Attack", 20.0f, 800.0f, 200.0f, "us", [] (Settings& s, int k, float v) { AT (comp).fetAttackUs = v; }, 150.0f));
                o.push_back (real ("fet.release", "FET Release", 50.0f, 1100.0f, 300.0f, "ms", [] (Settings& s, int k, float v) { AT (comp).fetReleaseMs = v; }, 300.0f));
                o.push_back (choice ("fet.ratio", "FET Ratio", { "4:1", "8:1", "12:1", "20:1", "ALL" }, 0, [] (Settings& s, int k, float v) { AT (comp).fetRatio = index (v); }));
                o.push_back (real ("fet.color", "FET Color", 0.0f, 100.0f, 50.0f, "%", [] (Settings& s, int k, float v) { AT (comp).fetColor = pct (v); }));
                o.push_back (toggle ("fet.link", "FET Stereo Link", true, [] (Settings& s, int k, float v) { AT (comp).fetLink = on (v); }));
                o.push_back (toggle ("fet.colorOnly", "FET Color Only", false, [] (Settings& s, int k, float v) { AT (comp).fetColorOnly = on (v); }));
                o.push_back (real ("multi.depth", "Depth", 0.0f, 100.0f, 50.0f, "%", [] (Settings& s, int k, float v) { AT (comp).depth = pct (v); }));
                o.push_back (real ("multi.time", "Time", 0.0f, 100.0f, 50.0f, "%", [] (Settings& s, int k, float v) { AT (comp).time = pct (v); }));
                o.push_back (real ("multi.tone", "Tone", 0.0f, 100.0f, 50.0f, "%", [] (Settings& s, int k, float v) { AT (comp).tone = pct (v); }));
                o.push_back (real ("multi.threshold", "Band Threshold", -60.0f, 0.0f, -24.0f, "dB", [] (Settings& s, int k, float v) { AT (comp).mbThresholdDb = v; }));
                o.push_back (real ("multi.down", "Down Ratio", 1.0f, 32.0f, 4.0f, "x", [] (Settings& s, int k, float v) { AT (comp).mbDownRatio = v; }, 4.0f));
                o.push_back (real ("multi.up", "Up Ratio", 1.0f, 8.0f, 1.5f, "x", [] (Settings& s, int k, float v) { AT (comp).mbUpRatio = v; }, 2.0f));
                o.push_back (real ("multi.attack", "Band Attack", 0.1f, 100.0f, 10.0f, "ms", [] (Settings& s, int k, float v) { AT (comp).mbAttackMs = v; }, 10.0f));
                o.push_back (real ("multi.release", "Band Release", 10.0f, 1500.0f, 120.0f, "ms", [] (Settings& s, int k, float v) { AT (comp).mbReleaseMs = v; }, 200.0f));
                o.push_back (real ("multi.low", "Low Crossover", 50.0f, 800.0f, 120.0f, "Hz", [] (Settings& s, int k, float v) { AT (comp).lowHz = v; }, 200.0f));
                o.push_back (real ("multi.high", "High Crossover", 800.0f, 10000.0f, 2500.0f, "Hz", [] (Settings& s, int k, float v) { AT (comp).highHz = v; }, 2500.0f));
                for (int b = 0; b < 3; ++b)
                {
                    static const char* names[3] { "low", "mid", "high" };
                    static const char* titles[3] { "Low", "Mid", "High" };
                    o.push_back (real ((std::string ("multi.") + names[b] + "Gain").c_str(), (std::string (titles[b]) + " Gain").c_str(), -18.0f, 18.0f, 0.0f, "dB",
                                       [b] (Settings& s, int k, float v) { AT (comp).bandGainDb[static_cast<std::size_t> (b)] = v; }));
                    o.push_back (real ((std::string ("multi.") + names[b] + "Offset").c_str(), (std::string (titles[b]) + " Threshold Offset").c_str(), -24.0f, 24.0f, 0.0f, "dB",
                                       [b] (Settings& s, int k, float v) { AT (comp).bandOffsetDb[static_cast<std::size_t> (b)] = v; }));
                }
                o.push_back (real ("multi.deadband", "Deadband", 0.0f, 24.0f, 3.0f, "dB", [] (Settings& s, int k, float v) { AT (comp).deadbandDb = v; }));
                o.push_back (toggle ("multi.match", "Loudness Match", true, [] (Settings& s, int k, float v) { AT (comp).loudnessMatch = on (v); }));
                break;
            case Type::eq:
                common (o, &Settings::eq, 100.0f);
                for (int b = 0; b < 4; ++b)
                {
                    const auto band = "band" + std::to_string (b + 1), title = "Band " + std::to_string (b + 1);
                    const fx::EqSettings defaults;
                    const auto& d = defaults.bands[static_cast<std::size_t> (b)];
                    o.push_back (toggle ((band + ".on").c_str(), (title + " On").c_str(), true, [b] (Settings& s, int k, float v) { AT (eq).bands[static_cast<std::size_t> (b)].on = on (v); }));
                    o.push_back (choice ((band + ".type").c_str(), (title + " Type").c_str(), { "Peak", "Low Shelf", "High Shelf", "High Pass", "Low Pass" }, d.type,
                                         [b] (Settings& s, int k, float v) { AT (eq).bands[static_cast<std::size_t> (b)].type = index (v); }));
                    o.push_back (real ((band + ".freq").c_str(), (title + " Frequency").c_str(), 20.0f, 20000.0f, d.hz, "Hz",
                                       [b] (Settings& s, int k, float v) { AT (eq).bands[static_cast<std::size_t> (b)].hz = v; }, 1000.0f));
                    o.push_back (real ((band + ".gain").c_str(), (title + " Gain").c_str(), -18.0f, 18.0f, 0.0f, "dB",
                                       [b] (Settings& s, int k, float v) { AT (eq).bands[static_cast<std::size_t> (b)].gainDb = v; }));
                    o.push_back (real ((band + ".q").c_str(), (title + " Q").c_str(), 0.25f, 12.0f, d.q, "x",
                                       [b] (Settings& s, int k, float v) { AT (eq).bands[static_cast<std::size_t> (b)].q = v; }, 1.0f));
                }
                o.push_back (toggle ("hp.on", "High Pass On", false, [] (Settings& s, int k, float v) { AT (eq).hpOn = on (v); }));
                o.push_back (real ("hp.freq", "High Pass Frequency", 20.0f, 500.0f, 30.0f, "Hz", [] (Settings& s, int k, float v) { AT (eq).hpHz = v; }, 80.0f));
                o.push_back (toggle ("hp.steep", "High Pass 24 dB", false, [] (Settings& s, int k, float v) { AT (eq).hpSteep = on (v); }));
                o.push_back (toggle ("lp.on", "Low Pass On", false, [] (Settings& s, int k, float v) { AT (eq).lpOn = on (v); }));
                o.push_back (real ("lp.freq", "Low Pass Frequency", 1000.0f, 20000.0f, 18000.0f, "Hz", [] (Settings& s, int k, float v) { AT (eq).lpHz = v; }, 6000.0f));
                o.push_back (toggle ("lp.steep", "Low Pass 24 dB", false, [] (Settings& s, int k, float v) { AT (eq).lpSteep = on (v); }));
                break;
            case Type::filter:
                common (o, &Settings::filter, 100.0f);
                o.push_back (choice ("type", "Type", { "LP 12", "LP 24", "HP 12", "HP 24", "BP 12", "Notch", "All-pass", "Comb +", "Comb -", "Formant I", "Formant II", "Formant III" }, 0,
                                     [] (Settings& s, int k, float v) { AT (filter).type = index (v); }));
                o.push_back (real ("freq", "Frequency", 20.0f, 20000.0f, 1000.0f, "Hz", [] (Settings& s, int k, float v) { AT (filter).hz = v; }, 1000.0f));
                o.push_back (real ("reso", "Resonance", 0.0f, 95.0f, 0.0f, "%", [] (Settings& s, int k, float v) { AT (filter).resonance = pct (v); }));
                o.push_back (real ("drive", "Drive", 0.0f, 24.0f, 0.0f, "dB", [] (Settings& s, int k, float v) { AT (filter).driveDb = v; }));
                o.push_back (real ("pan", "Pan", -100.0f, 100.0f, 0.0f, "%", [] (Settings& s, int k, float v) { AT (filter).pan = pct (v); }));
                o.push_back (real ("combDamp", "Comb Damping", 0.0f, 100.0f, 30.0f, "%", [] (Settings& s, int k, float v) { AT (filter).combDamping = pct (v); }));
                o.push_back (real ("formantShift", "Formant Shift", -12.0f, 12.0f, 0.0f, "st", [] (Settings& s, int k, float v) { AT (filter).formantShift = v; }));
                break;
            case Type::phaser:
                common (o, &Settings::phaser, 50.0f);
                o.push_back (choice ("mode", "Mode", { "Classic", "Deep" }, 0, [] (Settings& s, int k, float v) { AT (phaser).mode = index (v); }));
                o.push_back (real ("rate", "Rate", 0.03f, 10.0f, 0.5f, "Hz", [] (Settings& s, int k, float v) { AT (phaser).rateHz = v; }, 0.8f));
                o.push_back (toggle ("sync", "Sync", false, [] (Settings& s, int k, float v) { AT (phaser).sync = on (v); }));
                o.push_back (choice ("division", "Division", divisions(), 4, [] (Settings& s, int k, float v) { AT (phaser).division = index (v); }));
                o.push_back (real ("depth", "Depth", 0.0f, 100.0f, 60.0f, "%", [] (Settings& s, int k, float v) { AT (phaser).depth = pct (v); }));
                o.push_back (real ("center", "Center", 80.0f, 8000.0f, 800.0f, "Hz", [] (Settings& s, int k, float v) { AT (phaser).centreHz = v; }, 800.0f));
                o.push_back (real ("feedback", "Feedback", -85.0f, 85.0f, 35.0f, "%", [] (Settings& s, int k, float v) { AT (phaser).feedback = pct (v); }));
                o.push_back (choice ("stages", "Stages", { "2", "4", "6", "8", "12" }, 1, [] (Settings& s, int k, float v) { AT (phaser).stages = index (v); }));
                o.push_back (real ("stereo", "Stereo", 0.0f, 180.0f, 90.0f, "deg", [] (Settings& s, int k, float v) { AT (phaser).stereoDegrees = v; }));
                break;
            case Type::flanger:
                common (o, &Settings::flanger, 50.0f);
                o.push_back (choice ("mode", "Mode", { "Classic", "Through-Zero" }, 0, [] (Settings& s, int k, float v) { AT (flanger).mode = index (v); }));
                o.push_back (real ("rate", "Rate", 0.03f, 10.0f, 0.25f, "Hz", [] (Settings& s, int k, float v) { AT (flanger).rateHz = v; }, 0.8f));
                o.push_back (toggle ("sync", "Sync", false, [] (Settings& s, int k, float v) { AT (flanger).sync = on (v); }));
                o.push_back (choice ("division", "Division", divisions(), 6, [] (Settings& s, int k, float v) { AT (flanger).division = index (v); }));
                o.push_back (real ("depth", "Depth", 0.0f, 100.0f, 50.0f, "%", [] (Settings& s, int k, float v) { AT (flanger).depth = pct (v); }));
                o.push_back (real ("delay", "Delay", 0.2f, 12.0f, 2.0f, "ms", [] (Settings& s, int k, float v) { AT (flanger).delayMs = v; }, 2.5f));
                o.push_back (real ("feedback", "Feedback", -92.0f, 92.0f, 40.0f, "%", [] (Settings& s, int k, float v) { AT (flanger).feedback = pct (v); }));
                o.push_back (real ("stereo", "Stereo", 0.0f, 180.0f, 90.0f, "deg", [] (Settings& s, int k, float v) { AT (flanger).stereoDegrees = v; }));
                o.push_back (real ("width", "Width", 0.0f, 100.0f, 100.0f, "%", [] (Settings& s, int k, float v) { AT (flanger).width = pct (v); }));
                break;
            case Type::shifter:
                common (o, &Settings::shifter, 50.0f);
                o.push_back (choice ("mode", "Mode", { "Shift", "Blur" }, 0, [] (Settings& s, int k, float v) { AT (shifter).mode = index (v); }));
                o.push_back (real ("hz", "Shift", 0.0f, 2000.0f, 100.0f, "Hz", [] (Settings& s, int k, float v) { AT (shifter).shiftHz = v; }, 150.0f));
                o.push_back (choice ("direction", "Direction", { "Up", "Down" }, 0, [] (Settings& s, int k, float v) { AT (shifter).down = index (v) == 1; }));
                o.push_back (real ("width", "Width", 0.0f, 200.0f, 100.0f, "%", [] (Settings& s, int k, float v) { AT (shifter).width = pct (v); }));
                o.push_back (real ("feedback", "Feedback", 0.0f, 80.0f, 0.0f, "%", [] (Settings& s, int k, float v) { AT (shifter).feedback = pct (v); }));
                o.push_back (real ("blurAmount", "Blur Amount", 0.0f, 100.0f, 50.0f, "%", [] (Settings& s, int k, float v) { AT (shifter).blurAmount = pct (v); }));
                o.push_back (real ("blurTime", "Blur Time", 10.0f, 500.0f, 120.0f, "ms", [] (Settings& s, int k, float v) { AT (shifter).blurMs = v; }, 120.0f));
                break;
            case Type::convolution:
            {
                common (o, &Settings::conv, 30.0f);
                o.push_back (choice ("mode", "Mode", { "Realistic", "Creative" }, 0, [] (Settings& s, int k, float v) { AT (conv).mode = index (v); }));
                juce::StringArray impulses;
                for (int i = 0; i < fx::Impulse::factoryCount; ++i)
                    impulses.add (fx::Impulse::factoryName (i));
                o.push_back (choice ("impulse", "Impulse", impulses, 1, [] (Settings& s, int k, float v) { AT (conv).impulse = index (v); }));
                o.push_back (real ("predelay", "Pre-delay", 0.0f, 250.0f, 0.0f, "ms", [] (Settings& s, int k, float v) { AT (conv).preDelayMs = v; }, 40.0f));
                o.push_back (real ("decay", "Decay", 0.1f, 20.0f, 4.0f, "s", [] (Settings& s, int k, float v) { AT (conv).decaySeconds = v; }, 2.0f));
                o.push_back (real ("size", "Size", 50.0f, 200.0f, 100.0f, "%", [] (Settings& s, int k, float v) { AT (conv).size = pct (v); }));
                o.push_back (real ("lowCut", "Low Cut", 20.0f, 2000.0f, 20.0f, "Hz", [] (Settings& s, int k, float v) { AT (conv).lowCutHz = v; }, 150.0f));
                o.push_back (real ("highCut", "High Cut", 1000.0f, 20000.0f, 20000.0f, "Hz", [] (Settings& s, int k, float v) { AT (conv).highCutHz = v; }, 6000.0f));
                o.push_back (real ("width", "Width", 0.0f, 200.0f, 100.0f, "%", [] (Settings& s, int k, float v) { AT (conv).width = pct (v); }));
                o.push_back (toggle ("normalize", "Normalize", true, [] (Settings& s, int k, float v) { AT (conv).normalize = on (v); }));
                o.push_back (toggle ("reverse", "Reverse", false, [] (Settings& s, int k, float v) { AT (conv).reverse = on (v); }));
                break;
            }
            case Type::utility:
                common (o, &Settings::utility, 100.0f);
                o.push_back (real ("gain", "Gain", -36.0f, 24.0f, 0.0f, "dB", [] (Settings& s, int k, float v) { AT (utility).gainDb = v; }));
                o.push_back (real ("pan", "Balance", -100.0f, 100.0f, 0.0f, "%", [] (Settings& s, int k, float v) { AT (utility).pan = pct (v); }));
                o.push_back (real ("width", "Width", 0.0f, 200.0f, 100.0f, "%", [] (Settings& s, int k, float v) { AT (utility).width = pct (v); }));
                o.push_back (toggle ("invertL", "Invert Left", false, [] (Settings& s, int k, float v) { AT (utility).invertLeft = on (v); }));
                o.push_back (toggle ("invertR", "Invert Right", false, [] (Settings& s, int k, float v) { AT (utility).invertRight = on (v); }));
                o.push_back (toggle ("monoBass", "Mono Bass", false, [] (Settings& s, int k, float v) { AT (utility).monoBass = on (v); }));
                o.push_back (real ("monoBassHz", "Mono Bass Frequency", 40.0f, 400.0f, 130.0f, "Hz", [] (Settings& s, int k, float v) { AT (utility).monoBassHz = v; }, 150.0f));
                o.push_back (toggle ("hp.on", "High Pass On", false, [] (Settings& s, int k, float v) { AT (utility).hpOn = on (v); }));
                o.push_back (real ("hp.freq", "High Pass Frequency", 20.0f, 200.0f, 30.0f, "Hz", [] (Settings& s, int k, float v) { AT (utility).hpHz = v; }, 60.0f));
                o.push_back (toggle ("lp.on", "Low Pass On", false, [] (Settings& s, int k, float v) { AT (utility).lpOn = on (v); }));
                o.push_back (real ("lp.freq", "Low Pass Frequency", 2000.0f, 20000.0f, 18000.0f, "Hz", [] (Settings& s, int k, float v) { AT (utility).lpHz = v; }, 8000.0f));
                break;
            case Type::retro:
                // V2 (version hint 29): docs/FX_EFFECTS_LIBRARY.md, RETRO. CHARACTER and WEAR are
                // macros over the detail values (deriveRetro), never written into them.
                common (o, &Settings::retro, 65.0f);
                o.push_back (choice ("mode", "Mode", { "Tape", "Vinyl", "Digital" }, 0, [] (Settings& s, int k, float v) { AT (retro).mode = index (v); }));
                o.push_back (choice ("tape.model", "Tape Model", { "Cassette", "Studio Reel" }, 0, [] (Settings& s, int k, float v) { AT (retro).tapeModel = index (v); }));
                o.push_back (choice ("vinyl.rpm", "Vinyl Speed", { juce::String::fromUTF8 ("33\xe2\x85\x93"), "45", "78" }, 0, [] (Settings& s, int k, float v) { AT (retro).rpm = index (v); }));
                o.push_back (choice ("digital.era", "Digital Type", { "80s", "90s", "Codec" }, 1, [] (Settings& s, int k, float v) { AT (retro).era = index (v); }));
                o.push_back (real ("character", "Character", 0.0f, 100.0f, 40.0f, "%", [] (Settings& s, int k, float v) { AT (retro).character = pct (v); }));
                o.push_back (real ("wear", "Wear", 0.0f, 100.0f, 25.0f, "%", [] (Settings& s, int k, float v) { AT (retro).wear = pct (v); }));
                o.push_back (real ("input", "Input", -18.0f, 18.0f, 0.0f, "dB", [] (Settings& s, int k, float v) { AT (retro).inputDb = v; }));
                o.push_back (real ("tone", "Tone", -12.0f, 12.0f, 0.0f, "dB", [] (Settings& s, int k, float v) { AT (retro).toneDb = v; }));
                o.push_back (real ("saturation", "Saturation", 0.0f, 100.0f, 20.0f, "%", [] (Settings& s, int k, float v) { AT (retro).saturation = pct (v); }));
                o.push_back (choice ("preserve", "Preserve", { "Low", "Neutral", "High" }, 1, [] (Settings& s, int k, float v) { AT (retro).preserve = index (v); }));
                o.push_back (real ("wow.depth", "Wow Depth", 0.0f, 100.0f, 12.0f, "%", [] (Settings& s, int k, float v) { AT (retro).wowDepth = pct (v); }));
                o.push_back (real ("wow.rate", "Wow Rate", 0.1f, 3.0f, 0.55f, "Hz", [] (Settings& s, int k, float v) { AT (retro).wowHz = v; }, 0.6f));
                o.push_back (real ("flutter.depth", "Flutter Depth", 0.0f, 100.0f, 7.0f, "%", [] (Settings& s, int k, float v) { AT (retro).flutterDepth = pct (v); }));
                o.push_back (real ("flutter.rate", "Flutter Rate", 3.0f, 20.0f, 6.0f, "Hz", [] (Settings& s, int k, float v) { AT (retro).flutterHz = v; }, 7.0f));
                o.push_back (real ("transport.random", "Transport Random", 0.0f, 100.0f, 20.0f, "%", [] (Settings& s, int k, float v) { AT (retro).randomness = pct (v); }));
                o.push_back (real ("stereo.phase", "Stereo Phase", 0.0f, 180.0f, 0.0f, "deg", [] (Settings& s, int k, float v) { AT (retro).stereoPhaseDeg = v; }));
                o.push_back (choice ("noise.type", "Noise Type", { "Auto", "Tape Hiss", "Vinyl", "Room", "Digital" }, 0, [] (Settings& s, int k, float v) { AT (retro).noiseType = index (v); }));
                o.push_back (real ("noise.level", "Noise Level", -96.0f, -24.0f, -96.0f, "dB", [] (Settings& s, int k, float v) { AT (retro).noiseDb = v; }));
                o.push_back (real ("noise.follow", "Noise Follow", 0.0f, 100.0f, 0.0f, "%", [] (Settings& s, int k, float v) { AT (retro).noiseFollow = pct (v); }));
                o.push_back (toggle ("noise.automute", "Noise Auto Mute", true, [] (Settings& s, int k, float v) { AT (retro).noiseAutoMute = on (v); }));
                o.push_back (real ("crackle", "Crackle", 0.0f, 100.0f, 0.0f, "%", [] (Settings& s, int k, float v) { AT (retro).crackle = pct (v); }));
                o.push_back (real ("pops", "Pops", 0.0f, 100.0f, 0.0f, "%", [] (Settings& s, int k, float v) { AT (retro).pops = pct (v); }));
                o.push_back (real ("dropout.rate", "Dropout Rate", 0.0f, 12.0f, 0.0f, "/s", [] (Settings& s, int k, float v) { AT (retro).dropoutRate = v; }, 2.0f));
                o.push_back (real ("dropout.depth", "Dropout Depth", 0.0f, 100.0f, 0.0f, "%", [] (Settings& s, int k, float v) { AT (retro).dropoutDepth = pct (v); }));
                o.push_back (real ("dropout.length", "Dropout Length", 2.0f, 250.0f, 25.0f, "ms", [] (Settings& s, int k, float v) { AT (retro).dropoutMs = v; }, 30.0f));
                o.push_back (real ("bandwidth.low", "Low Edge", 20.0f, 800.0f, 20.0f, "Hz", [] (Settings& s, int k, float v) { AT (retro).lowHz = v; }, 120.0f));
                o.push_back (real ("bandwidth.high", "High Edge", 2000.0f, 20000.0f, 18000.0f, "Hz", [] (Settings& s, int k, float v) { AT (retro).highHz = v; }, 7000.0f));
                o.push_back (real ("head.bump", "Head Bump", 0.0f, 100.0f, 15.0f, "%", [] (Settings& s, int k, float v) { AT (retro).headBump = pct (v); }));
                o.push_back (real ("compress.amount", "Compress", 0.0f, 100.0f, 0.0f, "%", [] (Settings& s, int k, float v) { AT (retro).compAmount = pct (v); }));
                o.push_back (real ("compress.attack", "Compress Attack", 0.25f, 4.0f, 1.0f, "x", [] (Settings& s, int k, float v) { AT (retro).compAttack = v; }, 1.0f));
                o.push_back (real ("compress.release", "Compress Release", 0.25f, 8.0f, 1.0f, "x", [] (Settings& s, int k, float v) { AT (retro).compRelease = v; }, 1.0f));
                o.push_back (toggle ("compress.link", "Compress Link", true, [] (Settings& s, int k, float v) { AT (retro).compLink = on (v); }));
                o.push_back (real ("compress.tilt", "Sidechain Tilt", -100.0f, 100.0f, 0.0f, "%", [] (Settings& s, int k, float v) { AT (retro).compTilt = pct (v); }));
                o.push_back (real ("digital.bits", "Bits", 4.0f, 16.0f, 12.0f, "bit", [] (Settings& s, int k, float v) { AT (retro).bits = v; }));
                o.push_back (real ("digital.rate", "Rate", 4.0f, 48.0f, 24.0f, "kHz", [] (Settings& s, int k, float v) { AT (retro).rateKHz = v; }, 16.0f));
                o.push_back (choice ("digital.alias", "Alias", { "Clean", "Hard" }, 0, [] (Settings& s, int k, float v) { AT (retro).aliasHard = index (v) == 1; }));
                o.push_back (real ("digital.mod", "Motion", 0.0f, 100.0f, 0.0f, "%", [] (Settings& s, int k, float v) { AT (retro).digitalMotion = pct (v); }));
                o.push_back (real ("codec.loss", "Loss", 0.0f, 100.0f, 15.0f, "%", [] (Settings& s, int k, float v) { AT (retro).codecLoss = pct (v); }));
                o.push_back (real ("codec.speed", "Speed", 0.0f, 100.0f, 20.0f, "%", [] (Settings& s, int k, float v) { AT (retro).codecSpeed = pct (v); }));
                o.push_back (choice ("codec.mode", "Codec Mode", { "Standard", "Drop", "Repeat", "Jitter", "Combined" }, 0, [] (Settings& s, int k, float v) { AT (retro).codecMode = index (v); }));
                o.push_back (toggle ("codec.artifactOnly", "Artifact Only", false, [] (Settings& s, int k, float v) { AT (retro).artifactOnly = on (v); }));
                o.push_back (choice ("codec.stereo", "Codec Stereo", { "Stereo", "Joint", "Mono" }, 0, [] (Settings& s, int k, float v) { AT (retro).codecStereo = index (v); }));
                o.push_back (real ("codec.artifact", "Artifact", 0.0f, 100.0f, 0.0f, "%", [] (Settings& s, int k, float v) { AT (retro).codecArtifact = pct (v); }));
                o.push_back (real ("codec.ambience", "Artifact Ambience", 0.0f, 100.0f, 0.0f, "%", [] (Settings& s, int k, float v) { AT (retro).codecAmbience = pct (v); }));
                o.push_back (real ("seed", "Seed", 0.0f, 999.0f, 0.0f, "#", [] (Settings& s, int k, float v) { AT (retro).seed = juce::roundToInt (v); }));
                break;
            default: break;
        }
#undef AT
        return o;
    }
}

const std::vector<Spec>& specsOf (fx::Type type)
{
    static const std::array<std::vector<Spec>, fx::typeCount> all = [] {
        std::array<std::vector<Spec>, fx::typeCount> a;
        for (int t = 0; t < fx::typeCount; ++t)
            a[static_cast<std::size_t> (t)] = build (static_cast<fx::Type> (t));
        return a;
    }();
    return all[static_cast<std::size_t> (type)];
}

juce::String idOf (fx::InstanceId id, const juce::String& name)
{
    return juce::String ("analog.fx.") + fx::typeKey (id.type) + "." + juce::String (id.slot + 1) + "." + name;
}

juce::String format (const Spec& spec, float v)
{
    // (A value that rounds to zero reads as 0, never "-0.0".)
    if (spec.kind == Kind::real && std::abs (v) < 0.005f)
        v = 0.0f;
    if (spec.kind == Kind::choice)
        return spec.choices[juce::jlimit (0, spec.choices.size() - 1, juce::roundToInt (v))];
    if (spec.kind == Kind::toggle)
        return v >= 0.5f ? "On" : "Off";
    const auto& u = spec.unit;
    // How many decimals: decided on the value as it will read (9.99999 reads "10", as 10 does),
    // so a value a float's width from a threshold reads the same on every platform.
    auto under = [] (float x, float limit, int decimals) {
        const double scale = std::pow (10.0, decimals);
        return std::round (std::abs (static_cast<double> (x)) * scale) / scale < static_cast<double> (limit);
    };
    if (u == "Hz")
        return ! under (v, 1000.0f, 0) ? juce::String (v / 1000.0f, under (v / 1000.0f, 10.0f, 2) ? 2 : 1) + " kHz" : juce::String (v, under (v, 10.0f, 2) ? 2 : 0) + " Hz";
    if (u == "dB")
        return (v >= 0.05f ? "+" : "") + juce::String (std::abs (v) < 0.05f ? 0.0f : v, 1) + " dB";
    if (u == "ms")
        return ! under (v, 1000.0f, 1) ? juce::String (v / 1000.0f, 2) + " s" : juce::String (v, under (v, 10.0f, 1) ? 1 : 0) + " ms";
    if (u == "us")
        return juce::String (juce::roundToInt (v)) + juce::String::fromUTF8 (" \xc2\xb5s");
    if (u == "s")
        return juce::String (v, under (v, 10.0f, 2) ? 2 : 1) + " s";
    if (u == "%")
        return juce::String (juce::roundToInt (v)) + " %";
    if (u == "x")
        return juce::String (v, under (v, 10.0f, 1) ? 1 : 0) + (spec.name.endsWith ("q") ? "" : (spec.name.startsWith ("compress.") ? juce::String::fromUTF8 (" \xc3\x97") : ":1"));
    if (u == "deg")
        return juce::String (juce::roundToInt (v)) + juce::String::fromUTF8 ("\xc2\xb0");
    if (u == "st")
        return (v >= 0.05f ? "+" : "") + juce::String (std::abs (v) < 0.05f ? 0.0f : v, 1) + " st";
    if (u == "cents")
        return juce::String (v, 1) + " ct";
    if (u == "/s")
        return juce::String (v, under (v, 10.0f, 1) ? 1 : 0) + " /s";
    if (u == "bit")
        return juce::String (v, 1) + " bit";
    if (u == "kHz")
        return juce::String (v, under (v, 10.0f, 1) ? 1 : 0) + " kHz";
    if (u == "#")
        return juce::String (juce::roundToInt (v));
    return juce::String (v, 2);
}

void addTo (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    for (int t = 4; t < fx::typeCount; ++t)
    {
        const auto type = static_cast<fx::Type> (t);
        for (int slot = 0; slot < fx::capacity[static_cast<std::size_t> (t)]; ++slot)
        {
            const juce::String prefix = juce::String ("FX ") + fx::typeName (type) + (fx::capacity[static_cast<std::size_t> (t)] > 1 ? " " + juce::String (slot + 1) : juce::String()) + " ";
            for (const auto& spec : specsOf (type))
            {
                // (Version hints: 28 for the V1 effects; RETRO, appended in V2, 29.)
                const juce::ParameterID id { idOf ({ type, slot }, spec.name), type == fx::Type::retro ? 29 : 28 };
                const auto title = prefix + spec.title;
                if (spec.kind == Kind::toggle)
                    layout.add (std::make_unique<juce::AudioParameterBool> (id, title, spec.fallback >= 0.5f));
                else if (spec.kind == Kind::choice)
                    layout.add (std::make_unique<juce::AudioParameterChoice> (id, title, spec.choices, juce::roundToInt (spec.fallback)));
                else
                {
                    juce::NormalisableRange<float> range (spec.low, spec.high);
                    if (spec.centre > spec.low && spec.centre < spec.high)
                        range.setSkewForCentre (spec.centre);
                    const auto* s = &spec;
                    layout.add (std::make_unique<juce::AudioParameterFloat> (
                        id, title, range, spec.fallback,
                        juce::AudioParameterFloatAttributes().withLabel (spec.unit == "%" ? "%" : spec.unit).withStringFromValueFunction ([s] (float v, int) { return format (*s, v); })));
                }
            }
        }
    }
}

void Reader::attach (juce::AudioProcessorValueTreeState& state)
{
    entries.clear();
    for (int t = 4; t < fx::typeCount; ++t)
    {
        const auto type = static_cast<fx::Type> (t);
        for (int slot = 0; slot < fx::capacity[static_cast<std::size_t> (t)]; ++slot)
            for (const auto& spec : specsOf (type))
                if (auto* raw = state.getRawParameterValue (idOf ({ type, slot }, spec.name)))
                    entries.push_back ({ raw, &spec, slot });
    }
}

void Reader::read (fx::Settings& settings) const noexcept
{
    for (const auto& e : entries)
        e.spec->write (settings, e.slot, e.value->load (std::memory_order_relaxed));
}

} // namespace andor::analog::fxparams
