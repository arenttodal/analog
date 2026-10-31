#include "SampleImport.h"

#include <juce_cryptography/juce_cryptography.h>

namespace andor::analog::import
{

namespace
{
    const juce::Identifier sourceType ("SampleSource"), zoneType ("Zone"), groupType ("Group");

    juce::String plain (const juce::String& s)
    {
        juce::String out;
        for (auto c : s.toLowerCase())
            if (juce::CharacterFunctions::isLetterOrDigit (c))
                out << juce::String::charToString (c);
        return out;
    }

    double number (const juce::var& v, double fallback = 0.0)
    {
        return v.isVoid() || v.isUndefined() ? fallback : static_cast<double> (v);
    }

    /** The recording, decoded (every channel), or nothing. */
    std::shared_ptr<osp::PlaybackSource> decode (juce::AudioFormatManager& formats, const juce::File& file, double root)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels == 0)
            return nullptr;
        const auto frames = static_cast<int> (reader->lengthInSamples);
        const auto channels = static_cast<int> (reader->numChannels);
        juce::AudioBuffer<float> buffer (channels, frames);
        if (! reader->read (&buffer, 0, frames, 0, true, true))
            return nullptr;
        osp::AudioData audio;
        audio.sampleRate = reader->sampleRate;
        for (int ch = 0; ch < channels; ++ch)
            audio.channels.emplace_back (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + frames);
        return std::make_shared<osp::PlaybackSource> (audio, root, 4);
    }

    const juce::var* groupOf (const juce::var& manifest, int index)
    {
        const auto* groups = manifest.getProperty ("groups", {}).getArray();
        if (groups == nullptr || index < 0 || index >= groups->size())
            return nullptr;
        return &groups->getReference (index);
    }

    /** Where a zone's recording is: in the store (its content name) or at its path. */
    juce::File zoneFile (const juce::XmlElement& zone, const juce::File& assetFolder)
    {
        if (const auto asset = zone.getStringAttribute ("asset"); asset.isNotEmpty() && assetFolder != juce::File())
            if (const auto f = assetFolder.getChildFile (asset); f.existsAsFile())
                return f;
        if (const auto path = zone.getStringAttribute ("path"); path.isNotEmpty())
            if (const juce::File f (path); f.existsAsFile())
                return f;
        return {};
    }
}

juce::var readManifest (const juce::File& manifestFile)
{
    return juce::JSON::parse (manifestFile.loadFileAsString());
}

juce::Array<std::pair<int, juce::String>> playableGroups (const juce::var& manifest)
{
    juce::Array<std::pair<int, juce::String>> out;
    if (auto* groups = manifest.getProperty ("groups", {}).getArray())
        for (const auto& g : *groups)
            if (auto* zones = g.getProperty ("zones", {}).getArray(); zones != nullptr && ! zones->isEmpty())
                out.add ({ static_cast<int> (g.getProperty ("index", -1)), g.getProperty ("name", {}).toString() });
    return out;
}

juce::Array<int> savedLaneGroups (const juce::var& manifest)
{
    juce::Array<int> out;
    const auto lanes = manifest.getProperty ("lanes", {});
    for (const char* lane : { "A", "B", "C", "D" })
        if (const auto l = lanes.getProperty (lane, {}); l.isObject())
            out.add (static_cast<int> (l.getProperty ("selectedGroup", -1)));
    return out;
}

juce::File findSample (const juce::File& folder, const juce::String& name)
{
    if (const auto exact = folder.getChildFile (name); exact.existsAsFile())
        return exact;
    const auto wanted = plain (name);
    juce::File found;
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false))
        if (plain (f.getFileName()).contains (wanted))
        {
            if (found != juce::File())
                return {};   // (two candidates: ambiguous, not guessed)
            found = f;
        }
    return found;
}

juce::String storeAsset (const juce::File& recording, const juce::File& assetFolder)
{
    if (! assetFolder.createDirectory())
        return {};
    // Named by its content: the same recording is stored once, whatever it was called.
    const auto name = juce::SHA256 (recording).toHexString().substring (0, 24) + recording.getFileExtension().toLowerCase();
    const auto target = assetFolder.getChildFile (name);
    if (! target.existsAsFile() && ! recording.copyFileTo (target))
        return {};
    return name;
}

Definition makeDefinition (const juce::var& manifest, const juce::Array<int>& groups, const juce::String& name, const juce::File& sampleFolder,
                           const juce::File& assetFolder)
{
    Definition result;
    auto xml = std::make_unique<juce::XmlElement> (sourceType);
    xml->setAttribute ("schemaVersion", SampleSource::schemaVersion);
    xml->setAttribute ("name", name);
    const auto source = manifest.getProperty ("source", {});
    xml->setAttribute ("gain", number (source.getProperty ("programVolumeLinear", {}), 1.0));
    xml->setAttribute ("levelMatchDb", kontaktLevelMatchDb);
    xml->setAttribute ("origin", source.getProperty ("nki", {}).toString());
    xml->setAttribute ("program", source.getProperty ("program", {}).toString());
    for (const int index : groups)
    {
        const auto* group = groupOf (manifest, index);
        if (group == nullptr)
            continue;
        const auto groupGain = number (group->getProperty ("volumeLinear", {}), 1.0);
        auto* g = xml->createNewChildElement (groupType);
        g->setAttribute ("index", index);
        g->setAttribute ("lane", group->getProperty ("lane", {}).toString());
        g->setAttribute ("name", group->getProperty ("name", {}).toString());
        g->setAttribute ("volumeLinear", groupGain);
        if (auto* zones = group->getProperty ("zones", {}).getArray())
            for (const auto& z : *zones)
            {
                auto* e = xml->createNewChildElement (zoneType);
                const auto file = z.getProperty ("file", {}).toString();
                e->setAttribute ("group", index);
                e->setAttribute ("file", file);
                if (const auto found = findSample (sampleFolder, file); found != juce::File())
                {
                    if (assetFolder != juce::File())
                    {
                        if (const auto asset = storeAsset (found, assetFolder); asset.isNotEmpty())
                            e->setAttribute ("asset", asset);
                        else
                            result.missing.addIfNotAlreadyThere (file);
                    }
                    else
                        e->setAttribute ("path", found.getFullPathName());
                }
                else
                    result.missing.addIfNotAlreadyThere (file);
                for (const char* key : { "keyLo", "keyHi", "velLo", "velHi", "root", "sampleStart", "frames", "rate", "channels" })
                    e->setAttribute (key, number (z.getProperty (key, {})));
                e->setAttribute ("tuneSemitones", number (z.getProperty ("tuneCents", {})) / 100.0);
                e->setAttribute ("gain", number (z.getProperty ("volumeLinear", {}), 1.0) * groupGain);
                // Kontakt's first loop (it repeats until the note ends: later loops are never
                // reached when it repeats without a count).
                if (auto* loops = z.getProperty ("loops", {}).getArray(); loops != nullptr && ! loops->isEmpty())
                {
                    const auto& l = loops->getReference (0);
                    e->setAttribute ("loop", number (l.getProperty ("mode", {})) >= 1 && number (l.getProperty ("length", {})) >= 2.0);
                    e->setAttribute ("alternating", static_cast<bool> (l.getProperty ("alternating", false)));
                    e->setAttribute ("loopStart", number (l.getProperty ("start", {})));
                    e->setAttribute ("loopLength", number (l.getProperty ("length", {})));
                    e->setAttribute ("crossfade", number (l.getProperty ("xfade", {})));
                }
            }
    }
    result.xml = std::move (xml);
    return result;
}

LoadResult loadDefinition (const juce::XmlElement& definition, const juce::File& assetFolder)
{
    LoadResult result;
    if (! definition.hasTagName (sourceType) || definition.getIntAttribute ("schemaVersion", 0) > SampleSource::schemaVersion)
        return result;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto source = std::make_shared<SampleSource>();
    source->name = definition.getStringAttribute ("name").toStdString();
    source->gain = static_cast<float> (definition.getDoubleAttribute ("gain", 1.0) * juce::Decibels::decibelsToGain (definition.getDoubleAttribute ("levelMatchDb", 0.0)));
    for (const auto* e : definition.getChildWithTagNameIterator (zoneType.toString()))
    {
        ++result.zones;
        const auto name = e->getStringAttribute ("file");
        const auto file = zoneFile (*e, assetFolder);
        if (file == juce::File())
        {
            result.missing.addIfNotAlreadyThere (name);
            continue;
        }
        SampleZone zone;
        zone.root = e->getDoubleAttribute ("root", 60.0);
        zone.audio = decode (formats, file, zone.root);
        if (zone.audio == nullptr)
        {
            result.unreadable.addIfNotAlreadyThere (name);
            continue;
        }
        zone.keyLo = e->getIntAttribute ("keyLo", 0);
        zone.keyHi = e->getIntAttribute ("keyHi", 127);
        zone.velLo = e->getIntAttribute ("velLo", 1);
        zone.velHi = e->getIntAttribute ("velHi", 127);
        zone.tuneSemitones = e->getDoubleAttribute ("tuneSemitones");
        zone.gain = static_cast<float> (e->getDoubleAttribute ("gain", 1.0));
        zone.startFrame = e->getDoubleAttribute ("sampleStart");
        zone.loop.on = e->getBoolAttribute ("loop", false);
        zone.loop.alternating = e->getBoolAttribute ("alternating", false);
        zone.loop.start = e->getDoubleAttribute ("loopStart");
        zone.loop.length = e->getDoubleAttribute ("loopLength");
        zone.loop.crossfade = e->getDoubleAttribute ("crossfade");
        zone.file = name.toStdString();
        source->zones.push_back (std::move (zone));
    }
    if (source->isValid())
        result.source = std::move (source);
    return result;
}

LoadResult loadGroup (const juce::var& manifest, int groupIndex, const juce::File& sampleFolder)
{
    const auto* group = groupOf (manifest, groupIndex);
    if (group == nullptr)
        return {};
    auto definition = makeDefinition (manifest, { groupIndex }, group->getProperty ("name", {}).toString(), sampleFolder, {});
    auto result = loadDefinition (*definition.xml, {});
    for (const auto& m : definition.missing)
        result.missing.addIfNotAlreadyThere (m);
    return result;
}

int relink (juce::XmlElement& definition, const juce::File& folder, const juce::File& assetFolder)
{
    int found = 0;
    for (auto* e : definition.getChildWithTagNameIterator (zoneType.toString()))
        if (zoneFile (*e, assetFolder) == juce::File())
            if (const auto f = findSample (folder, e->getStringAttribute ("file")); f != juce::File())
                if (const auto asset = storeAsset (f, assetFolder); asset.isNotEmpty())
                {
                    e->setAttribute ("asset", asset);
                    e->removeAttribute ("path");
                    ++found;
                }
    return found;
}

} // namespace andor::analog::import
