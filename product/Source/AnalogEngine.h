#pragma once

#include "AnalogParameters.h"
#include "ShellEngine.h"
#include "Synth.h"

#include "model/ModelExchange.h"

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
    juce::String generatorName() const override { return "OSC 1"; }
    /** Three engines: card B is OSC 2 (the shell's layer C). */
    juce::String layerGeneratorName (int layer) const override { return layer == 0 ? generatorName() : (layer == 2 ? juce::String ("OSC 2") : juce::String()); }
    juce::StringArray boundParameters() const override { return params::engineParameterIds(); }
    void prepare (double sampleRate, int maximumBlockSize, juce::AudioProcessorValueTreeState& parameters) override;
    int latencySamples() const override { return synth.latencySamples(); }
    /** Layer B is the hybrid (the shell's hybrid layer): its recording reaches every voice
        while the hybrid is a SAMPLE (process() hands it over; another type keeps it dormant). */
    void setSource (int layer, const osp::plugin::LoadedInstrument* source) noexcept override
    {
        if (layer == 1)
            shellRecording = source != nullptr ? source->model.get() : nullptr;
    }
    void attachParameters (juce::AudioProcessorValueTreeState& parameters) override;
    /** Card C's type (analog.hybrid.type): only the selected one plays. Message thread. */
    HybridType hybridType() const noexcept;
    void setHybridType (HybridType type);
    bool engineSourceIsPeriodic (int layer) const override { return layer == 1 && hybridType() == HybridType::oscillator; }
    /** Card C empty (a SAMPLE without a recording, an INSTRUMENT PATCH without a sound): a full
        card with a faint stand-in picture and what to do. */
    juce::String engineSourcePlaceholder (int layer) const override;
    void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Performance& performance) noexcept override;
    int activeNotes() const noexcept override { return synth.activeVoices(); }
    void setShaperPattern (const osp::ShaperPattern& pattern) noexcept override { shaperPattern = pattern; }
    float shaperPhase() const noexcept override { return synth.shaperPhase(); }
    void setModulationCurve (int index, const osp::mod::Curve& curve) noexcept override;
    juce::String reimaginedModeUnavailable (int layer, int mode) const override;
    const osp::InstrumentEngine::GrainSnapshot* grainSnapshot (int layer) const noexcept override { return layer == 0 ? &grains : layer == 1 ? &organicHeads : layer == 2 ? &grains2 : nullptr; }
    void setModulation (const osp::mod::Settings& settings, const std::array<int, osp::mod::ccSlots>& controllers) noexcept override;
    bool arpModulation (osp::mod::Dest dest, double& offset) const noexcept override { return synth.sharedOffset (dest, offset); }
    void setPlayedKeys (bool arpeggiating, const juce::MidiBuffer& played) noexcept override;
    /** The per-card arpeggiator's lanes: shown A is OSC 1, B OSC 2, C the hybrid (the synth's bits). */
    void setCardRouting (bool on) noexcept override { synth.setCardRouting (on); }
    /** Tests: the synth as it plays (its voices' views). */
    const Synth& synthForTest() const noexcept { return synth; }
    osp::InstrumentEngine::ModView modulationView() const noexcept override;
    bool sourcePicture (int layer, const juce::AudioProcessorValueTreeState& parameters, std::vector<float>& cycle) const override;

    // KEY DNA: ANDOR OSP's (its editor, its profile), on the oscillators' GRANULAR (card A).
    bool hasKeyDna() const noexcept override { return true; }
    /** One profile, card A's: its key on card A (card B's grains follow the same profile). */
    bool hasKeyDnaKey (int layer) const noexcept override { return layer == 0; }
    void setKeyDnaTable (int layer, const osp::keydna::Table* table) noexcept override
    {
        if (layer == 0)
            synth.setKeyDnaTable (table);
    }

    void sourcePublished (int layer, std::shared_ptr<const osp::plugin::LoadedInstrument> source) override;
    /** OSC 3 (the hybrid as an oscillator) alone, as its card shows it; NOISE's picture. */
    static std::vector<float> noisePictureOf (const Patch& patch, int points = 256);
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
    /** One oscillator alone (slot 1 or 2: its card's picture): at full level, the other silent,
        OSC 2 on its own period, OSC 1 still synced to OSC 2 when SYNC is on (its synced shape). */
    static std::vector<float> oscillatorPictureOf (Patch patch, int slot, int points = 256);

private:
    /** MORPH switches itself on when its knob (the MORPH macro) is taken in hand while it is
        off (a preset or a session that had it off): turning MORPH always morphs. */
    struct MorphTouch final : juce::AudioProcessorParameter::Listener
    {
        juce::RangedAudioParameter* enabled = nullptr;
        void parameterValueChanged (int, float) override {}
        void parameterGestureChanged (int, bool starting) override
        {
            if (starting && enabled != nullptr && enabled->getValue() < 0.5f)
                enabled->setValueNotifyingHost (1.0f);   // (on the thread of the gesture: the editor's)
        }
    } morphTouch;
    juce::RangedAudioParameter* morphPositionParam = nullptr;
    std::atomic<bool> recordingPublished { false };   ///< the shell has a recording on card C (message thread)

    // State version 8: card C's sources as handed to the synth (audio thread): the shell's
    // recording and the imported zones, each only while its type is selected.
    const osp::InstrumentModel* shellRecording = nullptr;
    const osp::InstrumentModel* recordingApplied = nullptr;
    const SampleSource* zonesApplied = nullptr;
    juce::AudioProcessorValueTreeState* state = nullptr;
    std::atomic<float>* hybridTypeValue = nullptr;

    int oversamplingFactor;
    Synth synth;
    params::Reader reader;
    osp::ShaperPattern shaperPattern = osp::RhythmicShaper::patternSteps (osp::ShaperParams().pattern);   ///< audio thread
    std::vector<float> scratch;
    osp::InstrumentEngine::GrainSnapshot grains;   ///< written by process(), read by the card
    osp::InstrumentEngine::GrainSnapshot grains2;  ///< card B's (OSC 2's) grains, as `grains`
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
