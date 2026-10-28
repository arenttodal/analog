// ANDOR ANALOG: what makes it itself (identity, namespaces, macros, its own parameters and
// factory sounds) and its engine. Started by tools/create_andor_instrument.py from
// shared/andor-shell/template.

#include "AnalogEngine.h"
#include "AnalogParameters.h"
#include "AnalogPopovers.h"
#include "FactoryPatches.h"
#include "ShellProduct.h"

namespace andor::shell
{

namespace
{
    /** Bumped whenever the factory sounds change: an installed set from another version is
        written again (the musician's own presets are never touched). */
    constexpr const char* factoryVersion = "analog-factory-2";

    /** Version 2 moved settings (ANDOR OSP's DRIVE, MOVEMENT, REIMAGINED, GRANULAR and LFO 1
        joined): a version-1 session or preset is brought up to date so it sounds as it did. */
    void migrateFromVersion1 (osp::plugin::InstrumentHost& host)
    {
        namespace p = andor::analog::params;
        // DRIVE was the filter's input drive: that is FILTER DRIVE now, and the DRIVE macro is
        // ANDOR OSP's DRIVE, which version 1 did not have.
        host.setParameterValue (p::filterDrive, host.parameterValue ("drive"));
        host.setParameterValue ("drive", 0.0f);
        // The LFO is LFO 1 of the modulation panel now (S&H, which it lacks, becomes RANDOM).
        constexpr int shapeOf[] = { 0, 1, 2, 4, 5 };   // SINE, TRIANGLE, RAMP, SQUARE, S&H
        host.setParameterValue ("mod.lfo1.shape", static_cast<float> (shapeOf[std::clamp (juce::roundToInt (host.parameterValue (p::lfoWave)), 0, 4)]));
        host.setParameterValue ("mod.lfo1.rate", host.parameterValue (p::lfoRate));
        host.setParameterValue ("mod.lfo1.sync", host.parameterValue (p::lfoSync));
        host.setParameterValue ("mod.lfo1.division", static_cast<float> (andor::analog::syncDivisionOfLegacy (juce::roundToInt (host.parameterValue (p::lfoDivision)))));
        host.setParameterValue ("mod.lfo1.phase", 0.0f);
        host.setParameterValue ("mod.lfo1.polarity", 0.0f);
        host.setParameterValue ("mod.lfo1.mode", 0.0f);
        // ANDOR OSP's MOVEMENT, REIMAGINED and GRANULAR did nothing in version 1 (their
        // defaults were stored, unheard): they start off.
        host.setParameterValue ("motion", 0.0f);
        host.setParameterValue ("reimagined", 0.0f);
        host.setParameterValue ("layerA.sourceMode", 0.0f);
    }

    juce::String safeFileName (const std::string& name)
    {
        return juce::File::createLegalFileName (juce::String (name));
    }

    /** A factory patch as the shell's saved state: every parameter, the patch's values where
        it has them, the defaults elsewhere (so a preset never inherits what played before). */
    std::unique_ptr<juce::XmlElement> presetXml (osp::plugin::InstrumentHost& host, const andor::analog::FactoryPatch& factory)
    {
        namespace p = andor::analog::params;
        auto values = p::valuesOf (factory.patch);
        if (factory.keyDna > 0.0)
        {
            values[p::keyDna] = 1.0f;
            for (const auto& id : { p::keyDnaPosition, p::keyDnaSize, p::keyDnaDensity, p::keyDnaSpread })
                values[id] = static_cast<float> (100.0 * factory.keyDna);
        }
        auto tree = host.apvts().copyState();
        for (auto* p : host.apvts().processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const auto it = values.find (ranged->getParameterID());
                const float value = it != values.end() ? it->second : ranged->convertFrom0to1 (ranged->getDefaultValue());
                auto child = tree.getChildWithProperty ("id", ranged->getParameterID());
                if (child.isValid())
                    child.setProperty ("value", ranged->convertFrom0to1 (ranged->convertTo0to1 (value)), nullptr);
            }
        // Modulation links and other trees belong to the musician's own work: none here.
        for (int i = tree.getNumChildren(); --i >= 0;)
            if (! tree.getChild (i).hasType ("PARAM"))
                tree.removeChild (i, nullptr);
        auto xml = std::make_unique<juce::XmlElement> (productInfo().stateType);
        xml->setAttribute ("schemaVersion", 1);
        xml->setAttribute ("product", productInfo().slug);
        xml->setAttribute ("productVersion", productInfo().stateVersion);
        if (auto params = tree.createXml())
            xml->addChildElement (params.release());
        return xml;
    }

    void installFactoryPresets (osp::plugin::InstrumentHost& host, const juce::File& folder)
    {
        const auto marker = folder.getChildFile (".version");
        if (marker.loadFileAsString().trim() == factoryVersion)
            return;
        // Our own folder only: replace the previous version's set.
        if (folder.isDirectory())
            folder.deleteRecursively();
        if (! folder.createDirectory())
            return;
        for (const auto& factory : andor::analog::factoryPatches())
        {
            const auto file = folder.getChildFile (juce::String (factory.category)).getChildFile (safeFileName (factory.name) + productInfo().presetExtension);
            file.getParentDirectory().createDirectory();
            if (auto xml = presetXml (host, factory))
                xml->writeTo (file);
        }
        marker.replaceWithText (factoryVersion);
    }
}

const ProductInfo& productInfo()
{
    static const ProductInfo info = [] {
        ProductInfo p;
        p.name = "ANDOR Analog";
        p.slug = "analog";
        // The header: ANDOR in the display face's bold, the product in its light, the tagline under them.
        p.identity = { "ANDOR", "/ANALOG", "A living analog instrument" };
        p.presetExtension = ".andoranalog";
        p.presetFolder = "ANDOR Analog";
        p.stateType = "ANDOR_ANALOG";
        // The macro row, in data order (ECHO's knob sits before SPACE's on screen, as in
        // ANDOR OSP). DRIVE, MOVEMENT, SPACE and ECHO are ANDOR OSP's own (parameters,
        // popovers and sound); BLEND and TONE are the instrument's.
        p.macros = { { { andor::analog::params::blend, "BLEND", "Blend: oscillator 1 and oscillator 2 (waves, tuning, LFO depths, Poly Mod, voices, KEY DNA)" },
                       { "drive", "DRIVE", "Drive: saturation of the whole sound (tube, tape or crunch)" },
                       { andor::analog::params::tone, "TONE", "Tone: the four-pole filter's cutoff (resonance, envelope, filter drive, LFO amount behind it)" },
                       { "motion", "MOVEMENT", "Movement: drift, tape, chorus, pulse or a rhythmic shaper" },
                       { "space", "SPACE", "Space: the room it plays in" },
                       { "echo", "ECHO", "Echo: repeats of the sound, from a tape echo or a bucket-brigade delay" } } };
        p.macroPopover = [] (int macro, osp::plugin::InstrumentHost& host) { return andor::analog::createMacroPopover (macro, host); };
        p.addParameters = [] (juce::AudioProcessorValueTreeState::ParameterLayout& layout) { andor::analog::params::addTo (layout); };
        p.installFactoryPresets = installFactoryPresets;
        p.stateVersion = 2;
        p.migrateState = [] (osp::plugin::InstrumentHost& host, int from) {
            if (from < 2)
                migrateFromVersion1 (host);
        };
        return p;
    }();
    return info;
}

std::unique_ptr<Engine> createEngine()
{
    return std::make_unique<andor::analog::AnalogEngine>();
}

} // namespace andor::shell
