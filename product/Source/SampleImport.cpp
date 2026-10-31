#include "SampleImport.h"

namespace andor::analog::import
{

namespace
{
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

GroupLoad loadGroup (const juce::var& manifest, int groupIndex, const juce::File& sampleFolder)
{
    GroupLoad result;
    const auto* groups = manifest.getProperty ("groups", {}).getArray();
    if (groups == nullptr || groupIndex < 0 || groupIndex >= groups->size())
        return result;
    const auto& group = groups->getReference (groupIndex);
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto source = std::make_shared<SampleSource>();
    source->name = group.getProperty ("name", {}).toString().toStdString();
    source->gain = static_cast<float> (number (group.getProperty ("volumeLinear", {}), 1.0)
                                       * number (manifest.getProperty ("source", {}).getProperty ("programVolumeLinear", {}), 1.0));
    if (auto* zones = group.getProperty ("zones", {}).getArray())
        for (const auto& z : *zones)
        {
            ++result.zones;
            const auto name = z.getProperty ("file", {}).toString();
            const auto file = findSample (sampleFolder, name);
            if (file == juce::File())
            {
                result.missing.addIfNotAlreadyThere (name);
                continue;
            }
            SampleZone zone;
            zone.root = number (z.getProperty ("root", {}), 60.0);
            zone.audio = decode (formats, file, zone.root);
            if (zone.audio == nullptr)
            {
                result.unreadable.addIfNotAlreadyThere (name);
                continue;
            }
            zone.keyLo = static_cast<int> (number (z.getProperty ("keyLo", {})));
            zone.keyHi = static_cast<int> (number (z.getProperty ("keyHi", {}), 127.0));
            zone.velLo = static_cast<int> (number (z.getProperty ("velLo", {}), 1.0));
            zone.velHi = static_cast<int> (number (z.getProperty ("velHi", {}), 127.0));
            zone.tuneSemitones = number (z.getProperty ("tuneCents", {})) / 100.0;
            zone.gain = static_cast<float> (number (z.getProperty ("volumeLinear", {}), 1.0));
            zone.startFrame = number (z.getProperty ("sampleStart", {}));
            zone.file = name.toStdString();
            // Kontakt's first loop (it repeats until the note ends: later loops are never
            // reached when it repeats without a count).
            if (auto* loops = z.getProperty ("loops", {}).getArray(); loops != nullptr && ! loops->isEmpty())
            {
                const auto& l = loops->getReference (0);
                zone.loop.on = static_cast<int> (number (l.getProperty ("mode", {}))) >= 1 && number (l.getProperty ("length", {})) >= 2.0;
                zone.loop.alternating = static_cast<bool> (l.getProperty ("alternating", false));
                zone.loop.start = number (l.getProperty ("start", {}));
                zone.loop.length = number (l.getProperty ("length", {}));
                zone.loop.crossfade = number (l.getProperty ("xfade", {}));
            }
            source->zones.push_back (std::move (zone));
        }
    if (source->isValid())
        result.source = std::move (source);
    return result;
}

} // namespace andor::analog::import
