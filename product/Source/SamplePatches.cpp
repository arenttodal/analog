#include "SamplePatches.h"

#include "AnalogParameters.h"
#include "AnalogMath.h"
#include "ShellProduct.h"

namespace andor::analog::import
{

Patch makePatch (bool hybrid)
{
    Patch p;   // (INIT: the oscillators' CLASSIC pair)
    p.osc1Enabled = p.osc2Enabled = hybrid;
    p.noiseLevel = 0.0;
    p.hybridType = HybridType::instrumentPatch;   // (card C plays the imported source)
    // ORGANIC (the imported source) at its level, the A/B mix at its centre (both at full level).
    p.organic.levelDb = 0.0;
    p.organic.blend = 0.5;
    p.organic.loop = true;
    p.organic.follow = true;
    // Kontakt's shared settings (screenshots and the saved script state; docs/sample-source):
    // its two volume AHDSRs (A 509.8 ms / S 0 dB / R 1.1 s, and A 1 ms / D 750 ms / S -12 dB /
    // R 1.1 s) multiply: about A 0.51 s, D 0.75 s to -12 dB, R 1.1 s.
    p.ampAttack = 0.51;
    p.ampDecay = 0.75;
    p.ampSustain = 0.251;
    p.ampRelease = 1.1;
    // SV LP2 at 15.3 kHz, no resonance; the filter envelope and key tracking off.
    p.tone = math::hzToTone (15300.0);
    p.resonance = 0.0;
    p.keyTrack = 0.0;
    p.envDepthSemitones = 0.0;
    // Its reverb send (SPACE, send slot 5) as ANDOR OSP's SPACE, about as present. (An
    // approximation, not the Kontakt reverb; Replika's return is off in the original.)
    p.space = 0.3;
    p.echo = 0.0;
    return p;
}

std::unique_ptr<juce::XmlElement> presetState (osp::plugin::InstrumentHost& host, const Patch& patch, const juce::XmlElement& definition)
{
    // As the factory presets: every parameter, the patch's values where it has them, the
    // defaults elsewhere (so a preset never inherits what played before).
    const auto values = params::valuesOf (patch);
    auto tree = host.apvts().copyState();
    for (auto* parameter : host.apvts().processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
        {
            const auto it = values.find (ranged->getParameterID());
            const float value = it != values.end() ? it->second : ranged->convertFrom0to1 (ranged->getDefaultValue());
            if (auto child = tree.getChildWithProperty ("id", ranged->getParameterID()); child.isValid())
                child.setProperty ("value", ranged->convertFrom0to1 (ranged->convertTo0to1 (value)), nullptr);
        }
    for (int i = tree.getNumChildren(); --i >= 0;)
        if (! tree.getChild (i).hasType ("PARAM"))
            tree.removeChild (i, nullptr);
    const auto& info = andor::shell::productInfo();
    auto xml = std::make_unique<juce::XmlElement> (info.stateType);
    xml->setAttribute ("schemaVersion", 1);
    xml->setAttribute ("product", info.slug);
    xml->setAttribute ("productVersion", info.stateVersion);
    if (auto parameters = tree.createXml())
        xml->addChildElement (parameters.release());
    xml->addChildElement (new juce::XmlElement (definition));
    return xml;
}

GeneratedPatches generatePatches (osp::plugin::InstrumentHost& host, const juce::var& manifest, const juce::File& sampleFolder, const juce::File& assetFolder,
                                  const juce::File& presetFolder)
{
    GeneratedPatches out;
    const auto program = manifest.getProperty ("source", {}).getProperty ("program", {}).toString();
    // "Evenant Engine v2 - Finding Rhythm - Bukkehorn 4.5": the folder takes the last part.
    const auto patchName = juce::File::createLegalFileName (program.fromLastOccurrenceOf (" - ", false, false).trim().isNotEmpty()
                                                                ? program.fromLastOccurrenceOf (" - ", false, false).trim() : program);
    const auto folder = presetFolder.getChildFile (patchName);
    folder.createDirectory();
    const auto extension = andor::shell::productInfo().presetExtension;
    const auto recordName = patchName + ".import.xml";
    juce::XmlElement record ("Import");
    record.setAttribute ("collection", patchName);
    record.setAttribute ("program", program);
    record.setAttribute ("origin", manifest.getProperty ("source", {}).getProperty ("nki", {}).toString());
    auto write = [&] (const juce::String& name, const juce::Array<int>& groups, bool hybrid) {
        auto definition = makeDefinition (manifest, groups, name, sampleFolder, assetFolder);
        definition.xml->setAttribute ("collection", patchName);
        definition.xml->setAttribute ("import", recordName);
        for (const auto& m : definition.missing)
            out.missing.addIfNotAlreadyThere (m);
        int zones = 0, found = 0;
        for (const auto* z : definition.xml->getChildWithTagNameIterator ("Zone"))
        {
            ++zones;
            found += z->hasAttribute ("asset") || z->hasAttribute ("path") ? 1 : 0;
        }
        if (zones == 0 || found == 0)
        {
            out.skipped.add (name);
            return;
        }
        if (! hybrid)
            record.addChildElement (new juce::XmlElement (*definition.xml));
        const auto state = presetState (host, makePatch (hybrid), *definition.xml);
        const auto file = folder.getChildFile (juce::File::createLegalFileName (name) + extension);
        if (state->writeTo (file))
            out.files.add (file);
    };
    const auto groups = manifest.getProperty ("groups", {});
    for (const auto& [index, name] : playableGroups (manifest))
    {
        const auto g = groups[index];
        write (g.getProperty ("lane", {}).toString() + juce::String (static_cast<int> (g.getProperty ("slot", 0)) + 1) + " " + name, { index }, false);
    }
    const auto saved = savedLaneGroups (manifest);
    if (! saved.isEmpty())
    {
        write ("Saved Lanes", saved, false);
        write ("Hybrid - " + groups[saved[0]].getProperty ("name", {}).toString() + " + Oscillators", { saved[0] }, true);
    }
    if (assetFolder.createDirectory())
    {
        out.record = assetFolder.getChildFile (recordName);
        record.writeTo (out.record);
    }
    return out;
}

std::unique_ptr<juce::XmlElement> readRecord (const juce::File& assetFolder, const juce::String& recordName)
{
    if (recordName.isEmpty() || recordName.containsAnyOf ("/\\"))
        return nullptr;
    auto xml = juce::XmlDocument::parse (assetFolder.getChildFile (recordName));
    return xml != nullptr && xml->hasTagName ("Import") ? std::move (xml) : nullptr;
}

} // namespace andor::analog::import
