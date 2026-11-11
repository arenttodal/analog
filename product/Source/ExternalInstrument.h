#pragma once

#include "model/ModelExchange.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace andor::analog
{

/**
    EXPERIMENT (docs/experiments/KONTAKT_EXTERNAL_MORPH.md): one hosted instrument plugin, Kontakt,
    as card C's EXTERNAL SoundSource. Not a general plugin host: it finds Kontakt's VST3 only
    (or, for tests and the surrogate demo, the file ANDOR_EXTERNAL_INSTRUMENT_VST3 names).

    Threads:
    - Message thread: locate, load (the plugin is created and prepared here), unload, the editor,
      the state. A loaded instance is handed to the audio thread through osp::ModelExchange and
      destroyed here only once the audio thread reports it no longer uses it.
    - Audio thread: process() - no creation, destruction, scanning, disk access, allocation or
      lock of ours. (What the hosted plugin does inside its own processBlock is its own.)
    - prepare() is the host's prepare (audio stopped): the live instance is re-prepared there.

    A closed editor never unloads the instrument. A plugin that cannot be found keeps its saved
    identity and native state (written back unchanged), so a session opened on a computer
    without Kontakt loses nothing.
*/
class ExternalInstrument final
{
public:
    enum class State
    {
        empty,          ///< nothing chosen
        notInstalled,   ///< LOAD found no Kontakt
        ready,          ///< loaded, processing
        missing,        ///< a saved one that is not on this computer (its state kept)
        failed          ///< it would not load (the message says why)
    };

    struct Transport
    {
        bool valid = false, playing = false;
        double bpm = 120.0, ppq = 0.0;
        int numerator = 4, denominator = 4;
        std::int64_t timeInSamples = 0;
    };

    /** One forwarded MIDI event (tests: what card C's lane sent). */
    struct Forwarded
    {
        std::int64_t sample = 0;   ///< since the instrument started processing
        std::uint8_t status = 0, data1 = 0, data2 = 0;
    };

    ExternalInstrument();
    ~ExternalInstrument();

    //== Message thread ==========================================================================
    /** The folders searched for Kontakt's VST3 (the platform's standard VST3 locations). */
    static juce::StringArray searchFolders();
    /** Finds Kontakt (or the ANDOR_EXTERNAL_INSTRUMENT_VST3 override). False: `why` says why. */
    bool locate (juce::PluginDescription& found, juce::String& why);
    /** LOAD KONTAKT: locate and load. */
    bool loadKontakt();
    /** Loads one plugin (its native state applied if given). */
    bool load (const juce::PluginDescription& description, const juce::MemoryBlock* nativeState = nullptr);
    /** UNLOAD: its notes end, it fades out (10 ms) and is retired; the saved identity goes too. */
    void unload();
    void openEditor();
    void closeEditor();
    bool editorOpen() const noexcept { return editorWindow != nullptr; }
    /** Tests: the editor's window off the desktop (a display with no window manager). */
    void setEditorsOnDesktop (bool onDesktop) noexcept { editorsOnDesktop = onDesktop; }
    juce::Component* editorComponent() const noexcept;

    State state() const noexcept { return currentState; }
    juce::String displayName() const { return description.name.isNotEmpty() ? description.name : savedName; }
    juce::String version() const { return description.version; }
    juce::String message() const { return statusMessage; }
    /** Recently heard (a peak above -70 dBFS in the last second): the card's activity light. */
    bool sounding() const noexcept { return lastPeak.load (std::memory_order_relaxed) > 3.0e-4f; }

    /** The state to save: identity, native state (base64), version; null when nothing is chosen. */
    std::unique_ptr<juce::XmlElement> toXml() const;
    /** Restores (loads now, on the message thread; elsewhere, posted to it). Missing: kept. */
    void restore (const juce::XmlElement& xml);
    /** A session or preset without one: nothing loaded. */
    void clear();
    /** Frees instances the audio thread no longer uses (the engine's timer). */
    void collectGarbage();

    /** The host's prepare: every block at most `maxBlock` samples (larger ones are split). */
    void prepare (double sampleRate, int maxBlock);
    int latencySamples() const noexcept { return latency.load (std::memory_order_relaxed); }

    //== Audio thread ============================================================================
    /**
        Renders n samples of the instrument into left / right (overwritten), fed `midi` (sample
        positions within the block). `active` false (card C not EXTERNAL): its notes end, it
        fades out over 10 ms and then is no longer processed (silence).
    */
    void process (const juce::MidiBuffer& midi, float* left, float* right, int n, const Transport& transport, bool active) noexcept;

    /** Tests: the events forwarded so far (the newest `capacity`). */
    static constexpr int forwardedCapacity = 512;
    int forwardedCount() const noexcept { return forwardedTotal.load (std::memory_order_acquire); }
    Forwarded forwarded (int index) const noexcept { return forwardLog[static_cast<std::size_t> (index % forwardedCapacity)]; }
    std::int64_t samplesProcessed() const noexcept { return clock.load (std::memory_order_relaxed); }

private:
    class SnapshotHead final : public juce::AudioPlayHead
    {
    public:
        juce::Optional<PositionInfo> getPosition() const override;
        Transport now;   ///< (written by the audio thread before each processBlock)
        double rate = 48000.0;
    };

    struct Live
    {
        std::uint64_t generation = 0;
        std::unique_ptr<juce::AudioPluginInstance> instance;   ///< null: none (unloaded)
        mutable juce::AudioBuffer<float> buffer;               ///< its processBlock's (prepared size)
        mutable juce::MidiBuffer midi;                          ///< one chunk's events (preallocated)
        mutable SnapshotHead head;
        mutable int maxBlock = 512;
    };

    class EditorWindow;

    void publish (std::unique_ptr<juce::AudioPluginInstance> instance);
    void prepareLive (const Live& live);
    void runInstance (const Live& live, const juce::MidiBuffer& midi, int from, int n, float* left, float* right, float gain, bool endNotes,
                      const Transport& transport) noexcept;
    void logForwarded (const juce::MidiBuffer& midi, int n) noexcept;

    juce::AudioPluginFormatManager formats;
    osp::ModelExchange<Live> exchange;
    std::uint64_t generation = 0;
    juce::PluginDescription description;   ///< the one loaded (message thread)
    std::unique_ptr<juce::XmlElement> savedXml;   ///< a restored one that could not load (kept as saved)
    juce::String savedName, statusMessage;
    State currentState = State::empty;
    std::unique_ptr<EditorWindow> editorWindow;
    bool editorsOnDesktop = true;
    double rate = 48000.0;
    int maxBlock = 512;
    std::atomic<int> latency { 0 };

    // Audio thread.
    const Live* current = nullptr;
    const Live* retiring = nullptr;
    float retireGain = 0.0f, activeGain = 0.0f;
    bool wasActive = false, sentEndToRetiring = false, sentEndOnInactive = false;
    std::atomic<std::int64_t> clock { 0 };
    std::atomic<float> lastPeak { 0.0f };
    std::array<Forwarded, forwardedCapacity> forwardLog {};
    std::atomic<int> forwardedTotal { 0 };
    const juce::MidiBuffer noMidi;

    JUCE_DECLARE_WEAK_REFERENCEABLE (ExternalInstrument)
};

} // namespace andor::analog
