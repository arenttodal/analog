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
    constexpr const char* factoryVersion = "analog-factory-1";

    juce::String safeFileName (const std::string& name)
    {
        return juce::File::createLegalFileName (juce::String (name));
    }

    /** A factory patch as the shell's saved state: every parameter, the patch's values where
        it has them, the defaults elsewhere (so a preset never inherits what played before). */
    std::unique_ptr<juce::XmlElement> presetXml (osp::plugin::InstrumentHost& host, const andor::analog::Patch& patch)
    {
        const auto values = andor::analog::params::valuesOf (patch);
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
            if (auto xml = presetXml (host, factory.patch))
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
        // ANDOR OSP). DRIVE, SPACE and ECHO are ANDOR OSP's own parameters (the same meaning);
        // BLEND, TONE and MOTION are the instrument's.
        p.macros = { { { andor::analog::params::blend, "BLEND", "Blend: oscillator 1 and oscillator 2 (their waves, tuning, sync, noise, voices)" },
                       { "drive", "DRIVE", "Drive: the filter's input saturation, clean to hot" },
                       { andor::analog::params::tone, "TONE", "Tone: the four-pole filter's cutoff (resonance, envelope and tracking behind it)" },
                       { andor::analog::params::motion, "MOTION", "Motion: how much the LFO moves pitch, pulse width and filter (Poly Mod and ANALOG behind it)" },
                       { "space", "SPACE", "Space: the room it plays in" },
                       { "echo", "ECHO", "Echo: repeats of the sound, from a tape echo or a bucket-brigade delay" } } };
        p.macroPopover = [] (int macro, osp::plugin::InstrumentHost& host) { return andor::analog::createMacroPopover (macro, host); };
        p.addParameters = [] (juce::AudioProcessorValueTreeState::ParameterLayout& layout) { andor::analog::params::addTo (layout); };
        p.installFactoryPresets = installFactoryPresets;
        return p;
    }();
    return info;
}

std::unique_ptr<Engine> createEngine()
{
    return std::make_unique<andor::analog::AnalogEngine>();
}

} // namespace andor::shell
