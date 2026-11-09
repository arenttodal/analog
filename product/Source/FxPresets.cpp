#include "FxPresets.h"
#include "fx/Effects.h"
#include "FxParameters.h"

namespace andor::analog::fxpresets
{

using fx::Type;

namespace
{
    std::vector<Preset> build (Type type)
    {
        // Each a sound worth starting from (values in the parameters' own units).
        switch (type)
        {
            case Type::spread:
                return { { "Wide Double", { { "mode", 0 }, { "voices", 0 }, { "detune", 9 }, { "width", 130 }, { "mix", 45 } } },
                         { "Hyper Stack", { { "mode", 0 }, { "voices", 3 }, { "detune", 18 }, { "hyperRate", 0.4f }, { "width", 150 }, { "mix", 55 } } },
                         { "Dimension Air", { { "mode", 1 }, { "dimSize", 40 }, { "dimRate", 0.3f }, { "dimAmount", 45 }, { "width", 120 }, { "mix", 60 } } },
                         { "Both Wide", { { "mode", 2 }, { "voices", 1 }, { "detune", 10 }, { "dimAmount", 35 }, { "width", 140 }, { "mix", 50 } } },
                         { "Mono Safe", { { "mode", 2 }, { "lowMono", 1 }, { "lowMonoHz", 180 }, { "width", 110 }, { "mix", 40 } } } };
            case Type::compressor:
                return { { "Gentle Glue", { { "mode", 0 }, { "threshold", -20 }, { "ratio", 2 }, { "attack", 30 }, { "release", 250 }, { "knee", 9 }, { "makeup", 2 } } },
                         { "Tight Peaks", { { "mode", 0 }, { "threshold", -14 }, { "ratio", 6 }, { "attack", 2 }, { "release", 90 }, { "detector", 1 }, { "knee", 3 } } },
                         { "FET Punch", { { "mode", 1 }, { "fet.input", 18 }, { "fet.output", -14 }, { "fet.attack", 400 }, { "fet.release", 250 }, { "fet.ratio", 0 } } },
                         { "FET All Smash", { { "mode", 1 }, { "fet.input", 24 }, { "fet.output", -20 }, { "fet.attack", 100 }, { "fet.release", 120 }, { "fet.ratio", 4 }, { "mix", 60 } } },
                         { "FET Color Only", { { "mode", 1 }, { "fet.colorOnly", 1 }, { "fet.color", 80 }, { "fet.input", 6 }, { "fet.output", -6 } } },
                         { "Multiband Gentle", { { "mode", 2 }, { "multi.depth", 35 }, { "multi.threshold", -24 }, { "multi.down", 3 }, { "multi.up", 1.3f }, { "multi.deadband", 3 }, { "mix", 70 } } },
                         { juce::String::fromUTF8 ("OTT \xe2\x80\x94 Aggressive"),
                           { { "mode", 2 }, { "multi.depth", 100 }, { "multi.threshold", -30 }, { "multi.down", 32 }, { "multi.up", 4 }, { "multi.attack", 0.1f },
                             { "multi.release", 50 }, { "multi.deadband", 0 }, { "multi.lowGain", 2 }, { "multi.highGain", 3 }, { "mix", 70 } } } };
            case Type::eq:
                return { { "Flat", {} },
                         { "Air Lift", { { "band4.type", 2 }, { "band4.freq", 10000 }, { "band4.gain", 4 } } },
                         { "Warm Low", { { "band1.type", 1 }, { "band1.freq", 140 }, { "band1.gain", 3 }, { "band3.gain", -2 }, { "band3.freq", 3000 } } },
                         { "Mid Scoop", { { "band2.freq", 600 }, { "band2.gain", -5 }, { "band2.q", 0.7f } } },
                         { "Telephone", { { "hp.on", 1 }, { "hp.freq", 400 }, { "hp.steep", 1 }, { "lp.on", 1 }, { "lp.freq", 3200 }, { "lp.steep", 1 }, { "band3.gain", 4 }, { "band3.freq", 1500 } } } };
            case Type::filter:
                return { { "Low Pass Sweep", { { "type", 1 }, { "freq", 1200 }, { "reso", 35 } } },
                         { "Thin High Pass", { { "type", 3 }, { "freq", 600 }, { "reso", 10 } } },
                         { "Band Focus", { { "type", 4 }, { "freq", 1500 }, { "reso", 55 } } },
                         { "Metal Comb", { { "type", 7 }, { "freq", 220 }, { "reso", 80 }, { "combDamp", 20 } } },
                         { "Vowel A-E", { { "type", 9 }, { "freq", 600 }, { "reso", 40 } } },
                         { "Driven Lows", { { "type", 0 }, { "freq", 900 }, { "reso", 50 }, { "drive", 12 } } } };
            case Type::phaser:
                return { { "Slow Swirl", { { "mode", 0 }, { "rate", 0.2f }, { "depth", 70 }, { "feedback", 30 }, { "stages", 1 } } },
                         { "Deep Twelve", { { "mode", 1 }, { "rate", 0.15f }, { "depth", 80 }, { "feedback", 55 }, { "stages", 4 } } },
                         { "Quarter Sync", { { "sync", 1 }, { "division", 4 }, { "depth", 60 }, { "feedback", 40 } } } };
            case Type::flanger:
                return { { "Jet", { { "rate", 0.12f }, { "depth", 90 }, { "delay", 3 }, { "feedback", 75 } } },
                         { "Soft Sweep", { { "rate", 0.3f }, { "depth", 50 }, { "delay", 1.5f }, { "feedback", 20 }, { "mix", 40 } } },
                         { "Through Zero", { { "mode", 1 }, { "rate", 0.2f }, { "depth", 80 }, { "feedback", 30 } } } };
            case Type::shifter:
                return { { "Up a Little", { { "mode", 0 }, { "hz", 7 }, { "mix", 50 } } },
                         { "Ring Up", { { "mode", 0 }, { "hz", 180 }, { "feedback", 30 }, { "mix", 60 } } },
                         { "Wide Opposites", { { "mode", 0 }, { "hz", 12 }, { "width", 200 }, { "mix", 50 } } },
                         { "Blur Wash", { { "mode", 1 }, { "blurAmount", 80 }, { "blurTime", 300 }, { "mix", 60 } } } };
            case Type::convolution:
            {
                std::vector<Preset> list;
                for (int i = 0; i < fx::Impulse::factoryCount; ++i)
                    list.push_back ({ fx::Impulse::factoryName (i), { { "impulse", static_cast<float> (i) } } });
                return list;
            }
            case Type::utility:
                return { { "Neutral", {} },
                         { "Mono", { { "width", 0 } } },
                         { "Mono Bass", { { "monoBass", 1 }, { "monoBassHz", 140 } } },
                         { "Wider", { { "width", 150 }, { "monoBass", 1 } } },
                         { "Clean Lows", { { "hp.on", 1 }, { "hp.freq", 40 } } } };
            case Type::retro:
                // V2 (docs/FX_EFFECTS_LIBRARY.md): original sounds; no names of real machines.
                return { { "Warm Tape I", { { "mode", 0 }, { "tape.model", 1 }, { "character", 45 }, { "wear", 15 }, { "saturation", 30 }, { "head.bump", 30 }, { "wow.depth", 8 }, { "flutter.depth", 5 }, { "mix", 70 } } },
                         { "Worn Tape", { { "mode", 0 }, { "character", 55 }, { "wear", 70 }, { "dropout.rate", 1.5f }, { "dropout.depth", 50 }, { "noise.level", -60 }, { "mix", 75 } } },
                         { "Cassette Air", { { "mode", 0 }, { "character", 30 }, { "wear", 30 }, { "tone", 3 }, { "bandwidth.high", 15000 }, { "noise.level", -66 }, { "mix", 60 } } },
                         { "Gentle Flutter", { { "mode", 0 }, { "character", 25 }, { "wear", 20 }, { "wow.depth", 20 }, { "flutter.depth", 25 }, { "flutter.rate", 8 }, { "transport.random", 40 }, { "mix", 60 } } },
                         { "Dusty Vinyl", { { "mode", 1 }, { "vinyl.rpm", 0 }, { "character", 40 }, { "wear", 35 }, { "crackle", 35 }, { "pops", 15 }, { "noise.level", -62 }, { "mix", 70 } } },
                         { "45 RPM Haze", { { "mode", 1 }, { "vinyl.rpm", 1 }, { "character", 50 }, { "wear", 45 }, { "wow.depth", 25 }, { "crackle", 15 }, { "tone", -3 }, { "mix", 65 } } },
                         { "Early Digital", { { "mode", 2 }, { "digital.era", 0 }, { "character", 35 }, { "digital.bits", 10 }, { "digital.rate", 14 }, { "mix", 85 } } },
                         { "90s Sampler", { { "mode", 2 }, { "digital.era", 1 }, { "character", 15 }, { "wear", 10 }, { "digital.bits", 14 }, { "digital.rate", 26 }, { "mix", 100 } } },
                         { "Codec Air", { { "mode", 2 }, { "digital.era", 2 }, { "character", 20 }, { "wear", 15 }, { "codec.loss", 35 }, { "codec.speed", 20 }, { "mix", 70 } } },
                         { "Broken Packet", { { "mode", 2 }, { "digital.era", 2 }, { "character", 40 }, { "wear", 50 }, { "codec.loss", 60 }, { "codec.speed", 70 }, { "codec.mode", 4 }, { "mix", 100 } } },
                         { "Artifact Only", { { "mode", 2 }, { "digital.era", 2 }, { "character", 30 }, { "codec.loss", 55 }, { "codec.artifactOnly", 1 }, { "mix", 100 } } },
                         { "Acoustic Warmth", { { "mode", 0 }, { "tape.model", 1 }, { "character", 30 }, { "wear", 10 }, { "saturation", 25 }, { "preserve", 2 }, { "head.bump", 20 }, { "mix", 50 } } },
                         { "Bowed Halo", { { "mode", 0 }, { "character", 35 }, { "wear", 30 }, { "stereo.phase", 60 }, { "wow.depth", 18 }, { "flutter.depth", 6 }, { "transport.random", 50 }, { "tone", 2 }, { "mix", 55 } } } };
            default: return {};
        }
    }

    juce::File root()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("ANDOR").getChildFile ("ANDOR Analog");
    }

    juce::RangedAudioParameter* parameterOf (osp::plugin::InstrumentHost& host, fx::InstanceId id, const juce::String& name)
    {
        return host.apvts().getParameter (fxparams::idOf (id, name));
    }

    std::unique_ptr<juce::XmlElement> toXml (const Preset& p)
    {
        auto xml = std::make_unique<juce::XmlElement> ("FxPreset");
        xml->setAttribute ("name", p.name);
        for (const auto& [name, value] : p.values)
        {
            auto* v = xml->createNewChildElement ("V");
            v->setAttribute ("n", name);
            v->setAttribute ("v", value);
        }
        return xml;
    }

    Preset fromXml (const juce::XmlElement& xml)
    {
        Preset p;
        p.name = xml.getStringAttribute ("name");
        for (const auto* v : xml.getChildWithTagNameIterator ("V"))
            p.values.push_back ({ v->getStringAttribute ("n"), static_cast<float> (v->getDoubleAttribute ("v")) });
        return p;
    }
}

const std::vector<Preset>& factory (Type type)
{
    static const std::array<std::vector<Preset>, fx::typeCount> all = [] {
        std::array<std::vector<Preset>, fx::typeCount> a;
        for (int t = 0; t < fx::typeCount; ++t)
            a[static_cast<std::size_t> (t)] = build (static_cast<Type> (t));
        return a;
    }();
    return all[static_cast<std::size_t> (type)];
}

juce::File userFolder (Type type) { return root().getChildFile ("FX Presets").getChildFile (fx::typeName (type)); }

std::vector<Preset> user (Type type)
{
    std::vector<Preset> list;
    for (const auto& f : userFolder (type).findChildFiles (juce::File::findFiles, false, "*.xml"))
        if (auto xml = juce::XmlDocument::parse (f); xml != nullptr && xml->hasTagName ("FxPreset"))
            list.push_back (fromXml (*xml));
    std::sort (list.begin(), list.end(), [] (const Preset& a, const Preset& b) { return a.name.compareNatural (b.name) < 0; });
    return list;
}

void apply (osp::plugin::InstrumentHost& host, fx::InstanceId id, const Preset& preset)
{
    // Every one of the slot's parameters but ON: the preset's value, else the default (a preset
    // is a whole sound, not a difference).
    for (const auto& spec : fxparams::specsOf (id.type))
    {
        if (spec.name == "on")
            continue;
        float value = spec.fallback;
        for (const auto& [name, v] : preset.values)
            if (name == spec.name)
                value = v;
        if (auto* p = parameterOf (host, id, spec.name))
        {
            const float normal = p->convertTo0to1 (value);
            if (std::abs (normal - p->getValue()) < 1.0e-6f)
                continue;
            p->beginChangeGesture();
            p->setValueNotifyingHost (normal);
            p->endChangeGesture();
        }
    }
}

Preset capture (osp::plugin::InstrumentHost& host, fx::InstanceId id, const juce::String& name)
{
    Preset p;
    p.name = name;
    for (const auto& spec : fxparams::specsOf (id.type))
        if (spec.name != "on")
            if (auto* param = parameterOf (host, id, spec.name))
                p.values.push_back ({ spec.name, param->convertFrom0to1 (param->getValue()) });
    return p;
}

bool matches (osp::plugin::InstrumentHost& host, fx::InstanceId id, const Preset& preset)
{
    for (const auto& spec : fxparams::specsOf (id.type))
    {
        if (spec.name == "on")
            continue;
        float want = spec.fallback;
        for (const auto& [name, v] : preset.values)
            if (name == spec.name)
                want = v;
        if (auto* p = parameterOf (host, id, spec.name); p != nullptr && std::abs (p->convertTo0to1 (want) - p->getValue()) > 1.0e-4f)
            return false;
    }
    return true;
}

bool saveUser (Type type, const Preset& preset)
{
    const auto folder = userFolder (type);
    folder.createDirectory();
    const auto file = folder.getChildFile (juce::File::createLegalFileName (preset.name) + ".xml");
    return toXml (preset)->writeTo (file);
}

bool removeUser (Type type, const juce::String& name)
{
    const auto file = userFolder (type).getChildFile (juce::File::createLegalFileName (name) + ".xml");
    return file.existsAsFile() && file.moveToTrash();
}

juce::File chainFolder() { return root().getChildFile ("FX Chains"); }

std::unique_ptr<juce::XmlElement> chainToXml (osp::plugin::InstrumentHost& host, const fx::Graph& route, const juce::String& name)
{
    auto xml = std::make_unique<juce::XmlElement> ("FxChain");
    xml->setAttribute ("name", name);
    xml->setAttribute ("schema", fx::Graph::schema);
    xml->setAttribute ("graph", juce::String (fx::toText (route)));
    for (const auto& id : fx::instancesOf (route))
    {
        if (fx::isLegacy (id.type))
            continue;   // (the original four's settings are the instrument's: not a chain's)
        auto effect = toXml (capture (host, id, {}));
        effect->setTagName ("Effect");
        effect->setAttribute ("instance", juce::String (id.key()));
        if (auto* on = parameterOf (host, id, "on"))
            effect->setAttribute ("on", on->getValue() >= 0.5f);
        xml->addChildElement (effect.release());
    }
    return xml;
}

bool chainFromXml (osp::plugin::InstrumentHost& host, const juce::XmlElement& xml, fx::Graph& route)
{
    if (! xml.hasTagName ("FxChain"))
        return false;
    fx::Graph g;
    if (! fx::fromText (xml.getStringAttribute ("graph").toStdString(), g))
        return false;
    for (const auto* effect : xml.getChildWithTagNameIterator ("Effect"))
    {
        fx::InstanceId id;
        if (! fx::InstanceId::fromKey (effect->getStringAttribute ("instance").toStdString(), id) || fx::isLegacy (id.type))
            continue;
        apply (host, id, fromXml (*effect));
        if (auto* on = parameterOf (host, id, "on"))
            on->setValueNotifyingHost (effect->getBoolAttribute ("on", true) ? 1.0f : 0.0f);
    }
    route = g;
    return true;
}

} // namespace andor::analog::fxpresets
