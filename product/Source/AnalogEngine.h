#pragma once

#include "AnalogParameters.h"
#include "ShellEngine.h"
#include "Synth.h"

#include "model/ModelExchange.h"
#include "morph/MorphAnalyzer.h"

#include <atomic>
#include <memory>
#include <mutex>

namespace andor::analog
{

/**
    ANDOR ANALOG's engine at the ANDOR Instrument Shell's boundary: it reads the parameters
    (raw values cached in prepare()), turns the block's MIDI into the synth's events at their
    own sample positions, and renders. Everything the synth does is in dsp/ (pure C++).

    Threads: prepare() on the message thread (allocates); process() on the audio thread
    (no allocation, no locks); sourcePicture() on the message thread (parameters only).
*/
class AnalogEngine final : public andor::shell::Engine, private ControlModulator
{
public:
    /** 4x is the shipping quality; tests may ask for less. */
    explicit AnalogEngine (int oversampling = 4);

    Description describe() const override;
    juce::String generatorName() const override { return "OSCILLATORS"; }
    juce::StringArray boundParameters() const override { return params::engineParameterIds(); }
    void prepare (double sampleRate, int maximumBlockSize, juce::AudioProcessorValueTreeState& parameters) override;
    int latencySamples() const override { return synth.latencySamples(); }
    /** Layer B is ORGANIC (the shell's hybrid layer): its model reaches every voice. */
    void setSource (int layer, const osp::plugin::LoadedInstrument* source) noexcept override
    {
        if (layer == 1)
            synth.setOrganic (source != nullptr ? source->model.get() : nullptr);
    }
    void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Performance& performance) noexcept override;
    int activeNotes() const noexcept override { return synth.activeVoices(); }
    void setShaperPattern (const osp::ShaperPattern& pattern) noexcept override { shaperPattern = pattern; }
    float shaperPhase() const noexcept override { return synth.shaperPhase(); }
    void setModulationCurve (int index, const osp::mod::Curve& curve) noexcept override;
    juce::String reimaginedModeUnavailable (int layer, int mode) const override;
    const osp::InstrumentEngine::GrainSnapshot* grainSnapshot (int layer) const noexcept override { return layer == 0 ? &grains : layer == 1 ? &organicHeads : nullptr; }
    void setModulation (const osp::mod::Settings& settings, const std::array<int, osp::mod::ccSlots>& controllers) noexcept override;
    bool arpModulation (osp::mod::Dest dest, double& offset) const noexcept override { return synth.sharedOffset (dest, offset); }
    void setPlayedKeys (bool arpeggiating, const juce::MidiBuffer& played) noexcept override;
    osp::InstrumentEngine::ModView modulationView() const noexcept override;
    bool sourcePicture (int layer, const juce::AudioProcessorValueTreeState& parameters, std::vector<float>& cycle) const override;

    // KEY DNA: ANDOR OSP's (its editor, its profile), on the oscillators' GRANULAR (card A).
    bool hasKeyDna() const noexcept override { return true; }
    void setKeyDnaTable (int layer, const osp::keydna::Table* table) noexcept override
    {
        if (layer == 0)
            synth.setKeyDnaTable (table);
    }

    void sourcePublished (int layer, std::shared_ptr<const osp::plugin::LoadedInstrument> source) override;
    /** "morph": MORPH's state in words (empty: ready) for its popover. */
    juce::String status (const juce::String& topic) const override;
    ~AnalogEngine() override;

    // An imported sample source (the Sample Source importer): its definition (a `SampleSource`
    // element, import::makeDefinition) is kept with presets and sessions; its recordings load on
    // their own thread and reach the audio thread through an exchange, in ORGANIC's place.
    void saveState (juce::XmlElement& state) const override;
    void restoreState (const juce::XmlElement& state, const juce::File& assetFolder) override;
    /** Message thread: a definition to play (null: none), its recordings in `assetFolder`. */
    void setSampleSourceDefinition (std::unique_ptr<juce::XmlElement> definition, const juce::File& assetFolder);
    /** Message thread: a copy of the definition in use (null: none). */
    std::unique_ptr<juce::XmlElement> sampleSourceDefinition() const;
    /** The source's state for the interface: its name, loading, and what is missing. */
    struct SampleSourceState
    {
        juce::String name;
        bool loading = false, loaded = false;
        juce::StringArray missing, unreadable;
        int zones = 0;
    };
    SampleSourceState sampleSourceState() const;
    /** Tests: waits until the definition in use has loaded (and reached the synth on the next
        block). True when it did within the time. */
    bool waitForSampleSource (int timeoutMs) const;
    /** ORGANIC's card while the imported source plays: its collection and sound ("Bukkehorn 4.5
        · A8 Voxx"), its picture (sourcePicture, layer 1). */
    juce::String engineSourceName (int layer) const override;
    void removeEngineSource (int layer) override;

    /** One period of the oscillators' mix for a patch (the card's picture). */
    static std::vector<float> pictureOf (const Patch& patch, int points = 256);

private:
    int oversamplingFactor;
    Synth synth;
    params::Reader reader;
    osp::ShaperPattern shaperPattern = osp::RhythmicShaper::patternSteps (osp::ShaperParams().pattern);   ///< audio thread
    std::vector<float> scratch;
    osp::InstrumentEngine::GrainSnapshot grains;   ///< written by process(), read by the card
    osp::InstrumentEngine::GrainSnapshot organicHeads;   ///< ORGANIC's read heads (card B's playheads), as ANDOR OSP's
    void publishGrains() noexcept;

    // Modulation: the knobs as the block starts (the instrument's controls moved by their
    // routes), the controls a voice's routes move (ControlModulator), the panel's meters.
    void moveControl (Patch& patch, int k, double offset) const noexcept override { reader.moveControl (patch, raw, k, offset); }
    void publishModulationView() noexcept;
    params::Reader::Values raw {};
    bool modulated = false;
    std::array<std::atomic<float>, osp::mod::sourceCount> viewValue {};
    std::array<std::atomic<float>, 2> viewPhase {}, viewEnvTime {}, viewGlobalEnv {};
    std::array<std::atomic<int>, 2> viewEnvStage {};
    std::atomic<bool> viewVoice { false };
    float lastScreenPitch = 0.0f, lastScreenMod = 0.0f;
    bool prepared = false;

    /**
        MORPH's analysis of ORGANIC's recording, on its own thread (never the audio thread):
        a profile per recording, cached by content (so a reload or a relink is instant), handed
        to the audio thread through `pending` and freed only once that thread has moved past it.
    */
    struct ProfileBox
    {
        const osp::InstrumentModel* model = nullptr;   ///< the recording it describes (identity)
        std::shared_ptr<const osp::plugin::LoadedInstrument> source;   ///< kept alive with it
        morph::MorphProfile profile;
    };
    class Analyser;
    std::unique_ptr<Analyser> analyser;
    std::atomic<const ProfileBox*> pending { nullptr }, inUse { nullptr };
    friend class Analyser;

    struct LoadedSource
    {
        std::uint64_t generation = 0;
        std::shared_ptr<const SampleSource> source;   ///< null: none
    };
    mutable std::mutex sourceMutex;                     ///< the definition and the load's outcome
    std::unique_ptr<juce::XmlElement> sourceDefinition;
    SampleSourceState sourceState;
    std::shared_ptr<const SampleSource> sourceLoaded;   ///< the loaded source (its picture)
    std::uint64_t sourceGeneration = 0;
    osp::ModelExchange<LoadedSource> sourceExchange;   ///< (published under sourceMutex)
    const LoadedSource* sourcePlaying = nullptr;       ///< audio thread
    juce::ThreadPool sourceLoader { 1 };
    void loadSampleSource (std::unique_ptr<juce::XmlElement> definition, juce::File assetFolder, std::uint64_t generation);
};

} // namespace andor::analog
