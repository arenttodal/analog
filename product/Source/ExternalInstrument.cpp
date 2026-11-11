#include "ExternalInstrument.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace andor::analog
{

namespace
{
    constexpr const char* overrideVariable = "ANDOR_EXTERNAL_INSTRUMENT_VST3";
    constexpr float fadeSeconds = 0.01f;

    bool isKontaktFile (const juce::File& f)
    {
        return f.getFileExtension().equalsIgnoreCase (".vst3") && f.getFileNameWithoutExtension().startsWithIgnoreCase ("Kontakt");
    }
}

//==============================================================================
class ExternalInstrument::EditorWindow final : public juce::DocumentWindow
{
public:
    EditorWindow (ExternalInstrument& o, juce::AudioProcessorEditor* editor, const juce::String& name, bool onDesktop)
        : juce::DocumentWindow (name + juce::String::fromUTF8 (" \xc2\xb7 ANDOR Analog card C"), juce::Colour (0xff23272a), juce::DocumentWindow::closeButton,
                                onDesktop),
          owner (o)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        if (onDesktop)
        {
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
    }
    void closeButtonPressed() override { owner.closeEditor(); }   // (the instrument stays loaded)
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            owner.closeEditor();
            return true;
        }
        return juce::DocumentWindow::keyPressed (key);
    }

private:
    ExternalInstrument& owner;
};

//==============================================================================
juce::Optional<juce::AudioPlayHead::PositionInfo> ExternalInstrument::SnapshotHead::getPosition() const
{
    PositionInfo info;
    info.setIsPlaying (now.playing);
    info.setBpm (now.bpm);
    info.setTimeInSamples (now.timeInSamples);
    info.setTimeInSeconds (static_cast<double> (now.timeInSamples) / rate);
    if (now.valid)
        info.setPpqPosition (now.ppq);
    info.setTimeSignature (TimeSignature { now.numerator, now.denominator });
    return info;
}

//==============================================================================
ExternalInstrument::ExternalInstrument()
{
    formats.addDefaultFormats();
}

ExternalInstrument::~ExternalInstrument()
{
    editorWindow.reset();
}

juce::StringArray ExternalInstrument::searchFolders()
{
    juce::StringArray folders;
   #if JUCE_PLUGINHOST_VST3
    juce::VST3PluginFormat vst3;
    const auto locations = vst3.getDefaultLocationsToSearch();
    for (int i = 0; i < locations.getNumPaths(); ++i)
        folders.add (locations[i].getFullPathName());
   #endif
    return folders;
}

bool ExternalInstrument::locate (juce::PluginDescription& found, juce::String& why)
{
   #if JUCE_PLUGINHOST_VST3
    juce::VST3PluginFormat vst3;
    juce::Array<juce::File> candidates;
    if (const char* path = std::getenv (overrideVariable); path != nullptr && *path != 0)
        candidates.add (juce::File (juce::String::fromUTF8 (path)));   // (tests, the surrogate demo)
    else
        for (const auto& folder : searchFolders())
        {
            // The folder itself and one level down (Native Instruments' own subfolder).
            for (const auto& entry : juce::RangedDirectoryIterator (juce::File (folder), false, "*", juce::File::findFilesAndDirectories))
            {
                const auto f = entry.getFile();
                if (isKontaktFile (f))
                    candidates.add (f);
                else if (f.isDirectory() && ! f.getFileExtension().equalsIgnoreCase (".vst3"))
                    for (const auto& inner : juce::RangedDirectoryIterator (f, false, "*.vst3", juce::File::findFilesAndDirectories))
                        if (isKontaktFile (inner.getFile()))
                            candidates.add (inner.getFile());
            }
        }
    // The newest first (Kontakt 8 before 7).
    std::sort (candidates.begin(), candidates.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) > 0; });
    for (const auto& file : candidates)
    {
        juce::OwnedArray<juce::PluginDescription> types;
        vst3.findAllTypesForFile (types, file.getFullPathName());
        for (const auto* t : types)
            if (t->isInstrument)
            {
                found = *t;
                return true;
            }
    }
    why = candidates.isEmpty() ? "Kontakt (VST3) is not installed in the standard VST3 folders."
                               : "Kontakt was found but did not report an instrument (" + candidates[0].getFullPathName() + ").";
    return false;
   #else
    juce::ignoreUnused (found);
    why = "This build cannot host plugins (JUCE_PLUGINHOST_VST3 is off).";
    return false;
   #endif
}

bool ExternalInstrument::loadKontakt()
{
    juce::PluginDescription found;
    juce::String why;
    if (! locate (found, why))
    {
        currentState = State::notInstalled;
        statusMessage = why;
        return false;
    }
    return load (found);
}

bool ExternalInstrument::load (const juce::PluginDescription& d, const juce::MemoryBlock* nativeState)
{
    JUCE_ASSERT_MESSAGE_THREAD
    closeEditor();
    juce::String error;
    auto instance = formats.createPluginInstance (d, rate, maxBlock, error);
    if (instance == nullptr)
    {
        currentState = State::failed;
        statusMessage = d.name + ": " + (error.isNotEmpty() ? error : juce::String ("it would not load."));
        return false;
    }
    description = d;
    savedName = d.name;
    publish (std::move (instance));
    if (nativeState != nullptr && nativeState->getSize() > 0)
        if (const auto& live = exchange.latestModel(); live != nullptr && live->instance != nullptr)
            live->instance->setStateInformation (nativeState->getData(), static_cast<int> (nativeState->getSize()));
    savedXml.reset();
    currentState = State::ready;
    statusMessage = d.name + (d.version.isNotEmpty() ? " " + d.version : juce::String());
    return true;
}

void ExternalInstrument::unload()
{
    JUCE_ASSERT_MESSAGE_THREAD
    closeEditor();
    if (exchange.latestModel() != nullptr)
        publish (nullptr);
    description = {};
    savedXml.reset();
    savedName.clear();
    statusMessage.clear();
    currentState = State::empty;
}

void ExternalInstrument::clear()
{
    unload();
}

void ExternalInstrument::publish (std::unique_ptr<juce::AudioPluginInstance> instance)
{
    auto live = std::make_shared<Live>();
    live->generation = ++generation;
    live->instance = std::move (instance);
    if (live->instance != nullptr)
    {
        live->instance->setPlayHead (&live->head);
        prepareLive (*live);
    }
    else
        latency.store (0);
    exchange.publish (std::move (live));
}

void ExternalInstrument::prepareLive (const Live& live)
{
    auto& p = *live.instance;
    // Its buses: no audio in, its main output stereo, every other output off (Kontakt's
    // multi-outs) - else what it offers, and the first two channels are read.
    auto layout = p.getBusesLayout();
    for (auto& in : layout.inputBuses)
        in = juce::AudioChannelSet::disabled();
    for (int i = 0; i < layout.outputBuses.size(); ++i)
        layout.outputBuses.getReference (i) = i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
    if (! p.setBusesLayout (layout))
        p.enableAllBuses();
    p.setRateAndBufferSizeDetails (rate, maxBlock);
    p.prepareToPlay (rate, maxBlock);
    const int channels = std::max ({ 2, p.getTotalNumInputChannels(), p.getTotalNumOutputChannels() });
    live.buffer.setSize (channels, maxBlock);
    live.midi.ensureSize (8192);
    live.head.rate = rate;
    live.maxBlock = maxBlock;
    latency.store (p.getLatencySamples());
}

void ExternalInstrument::prepare (double sampleRate, int block)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlock = std::max (32, block);
    if (const auto& live = exchange.latestModel(); live != nullptr && live->instance != nullptr)
    {
        live->instance->releaseResources();
        prepareLive (*live);
    }
}

void ExternalInstrument::collectGarbage()
{
    exchange.collectGarbage();
}

juce::Component* ExternalInstrument::editorComponent() const noexcept
{
    return editorWindow != nullptr ? editorWindow->getContentComponent() : nullptr;
}

void ExternalInstrument::openEditor()
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (editorWindow != nullptr)
    {
        editorWindow->toFront (true);
        return;
    }
    const auto& live = exchange.latestModel();
    if (live == nullptr || live->instance == nullptr)
        return;
    if (auto* editor = live->instance->createEditorIfNeeded())
        editorWindow = std::make_unique<EditorWindow> (*this, editor, description.name, editorsOnDesktop);
}

void ExternalInstrument::closeEditor()
{
    // (the window owns the editor: deleting it deletes only the editor, never the instrument)
    editorWindow.reset();
}

std::unique_ptr<juce::XmlElement> ExternalInstrument::toXml() const
{
    if (savedXml != nullptr && currentState != State::ready)
        return std::make_unique<juce::XmlElement> (*savedXml);   // (missing / failed: as it was saved)
    const auto& live = exchange.latestModel();
    if (currentState != State::ready || live == nullptr || live->instance == nullptr)
        return nullptr;
    auto xml = std::make_unique<juce::XmlElement> ("ExternalInstrument");
    xml->setAttribute ("version", 1);
    xml->setAttribute ("name", description.name);
    xml->addChildElement (description.createXml().release());
    juce::MemoryBlock native;
    live->instance->getStateInformation (native);
    xml->setAttribute ("state", native.toBase64Encoding());
    return xml;
}

void ExternalInstrument::restore (const juce::XmlElement& xml)
{
    if (! juce::MessageManager::existsAndIsCurrentThread())
    {
        auto copy = std::make_shared<juce::XmlElement> (xml);
        juce::MessageManager::callAsync ([weak = juce::WeakReference<ExternalInstrument> (this), copy] {
            if (auto* self = weak.get())
                self->restore (*copy);
        });
        return;
    }
    juce::PluginDescription d;
    const auto* pluginXml = xml.getChildByName ("PLUGIN");
    if (pluginXml == nullptr || ! d.loadFromXml (*pluginXml))
    {
        clear();
        return;
    }
    juce::MemoryBlock native;
    native.fromBase64Encoding (xml.getStringAttribute ("state"));
    // Where it was saved; else wherever Kontakt is on this computer (another version's folder).
    auto candidate = d;
    bool found = juce::File::isAbsolutePath (d.fileOrIdentifier) && juce::File (d.fileOrIdentifier).exists();
    if (! found)
    {
        juce::PluginDescription here;
        juce::String why;
        if (locate (here, why) && here.name == d.name)
        {
            candidate = here;
            found = true;
        }
    }
    if (found && load (candidate, &native))
        return;
    // Missing (or it failed): nothing loaded, its identity and state kept, saved back as they were.
    closeEditor();
    if (exchange.latestModel() != nullptr)
        publish (nullptr);
    description = {};
    savedXml = std::make_unique<juce::XmlElement> (xml);
    savedName = xml.getStringAttribute ("name", d.name);
    if (! found)
    {
        currentState = State::missing;
        statusMessage = savedName + " is not installed on this computer. Its instrument and settings are kept in the session.";
    }
    else
        currentState = State::failed;   // (load() said why)
}

//==============================================================================
void ExternalInstrument::process (const juce::MidiBuffer& midi, float* left, float* right, int n, const Transport& transport, bool active) noexcept
{
    std::fill (left, left + n, 0.0f);
    std::fill (right, right + n, 0.0f);
    if (const auto* next = exchange.takePending())
    {
        if (current != nullptr && current->instance != nullptr && (activeGain > 0.0f || wasActive))
        {
            retiring = current;   // (its notes end; it fades out below)
            retireGain = activeGain;
            sentEndToRetiring = false;
        }
        current = next;
        activeGain = 0.0f;
        sentEndOnInactive = false;
    }
    if (current != nullptr)
        exchange.publishOldestInUse (retiring != nullptr ? retiring->generation : current->generation);

    if (retiring != nullptr)
    {
        runInstance (*retiring, noMidi, 0, n, left, right, 0.0f, ! sentEndToRetiring, transport);
        sentEndToRetiring = true;
        if (retireGain <= 0.0f)
            retiring = nullptr;
    }
    if (current != nullptr && current->instance != nullptr && (active || activeGain > 0.0f))
    {
        const bool endNotes = ! active && ! sentEndOnInactive;
        runInstance (*current, active ? midi : noMidi, 0, n, left, right, active ? 1.0f : 0.0f, endNotes, transport);
        if (endNotes)
            sentEndOnInactive = true;
        if (active)
        {
            sentEndOnInactive = false;
            logForwarded (midi, n);
        }
    }
    wasActive = active;
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
        peak = std::max ({ peak, std::abs (left[i]), std::abs (right[i]) });
    const float held = lastPeak.load (std::memory_order_relaxed) * std::exp (-static_cast<float> (n) / static_cast<float> (rate));
    lastPeak.store (std::max (peak, held), std::memory_order_relaxed);
    clock.fetch_add (n, std::memory_order_relaxed);
}

void ExternalInstrument::runInstance (const Live& live, const juce::MidiBuffer& midi, int from, int n, float* left, float* right, float target, bool endNotes,
                                      const Transport& transport) noexcept
{
    auto& gain = &live == retiring ? retireGain : activeGain;
    const float step = 1.0f / std::max (1.0f, fadeSeconds * static_cast<float> (rate));
    for (int offset = from; offset < from + n;)
    {
        const int chunk = std::min (live.maxBlock, from + n - offset);
        live.midi.clear();
        if (endNotes && offset == from)
        {
            const std::uint8_t sustainOff[3] { 0xb0, 64, 0 }, allNotesOff[3] { 0xb0, 123, 0 };
            live.midi.addEvent (sustainOff, 3, 0);
            live.midi.addEvent (allNotesOff, 3, 0);
        }
        for (const auto m : midi)
            if (m.samplePosition >= offset && m.samplePosition < offset + chunk)
                live.midi.addEvent (m.data, m.numBytes, m.samplePosition - offset);
        live.buffer.setSize (live.buffer.getNumChannels(), chunk, false, false, true);
        live.buffer.clear();
        auto& now = live.head.now;
        now = transport;
        now.timeInSamples = transport.timeInSamples + offset;
        now.ppq = transport.ppq + static_cast<double> (offset) * transport.bpm / 60.0 / rate;
        live.instance->processBlock (live.buffer, live.midi);
        const float* outL = live.buffer.getReadPointer (0);
        const float* outR = live.buffer.getNumChannels() > 1 ? live.buffer.getReadPointer (1) : outL;
        for (int i = 0; i < chunk; ++i)
        {
            gain = gain < target ? std::min (target, gain + step) : std::max (target, gain - step);
            left[offset + i] += gain * outL[i];
            right[offset + i] += gain * outR[i];
        }
        offset += chunk;
    }
}

void ExternalInstrument::logForwarded (const juce::MidiBuffer& midi, int) noexcept
{
    const auto base = clock.load (std::memory_order_relaxed);
    int total = forwardedTotal.load (std::memory_order_relaxed);
    for (const auto m : midi)
    {
        auto& f = forwardLog[static_cast<std::size_t> (total % forwardedCapacity)];
        f.sample = base + m.samplePosition;
        f.status = m.numBytes > 0 ? m.data[0] : 0;
        f.data1 = m.numBytes > 1 ? m.data[1] : 0;
        f.data2 = m.numBytes > 2 ? m.data[2] : 0;
        ++total;
    }
    forwardedTotal.store (total, std::memory_order_release);
}

} // namespace andor::analog
