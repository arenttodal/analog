#pragma once

#include "AnalogParameters.h"
#include "FxParameters.h"
#include "ShellEngine.h"
#include "Synth.h"
#include "UserMacroState.h"

#include "model/ModelExchange.h"
#include "rhythm/RhythmLoop.h"
#include "ExternalInstrument.h"

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
    /** The voices' oversampling and the CUSTOM route's effects (FxEngine's plan; 0 in CLASSIC):
        the shell tells the host whenever it changes. */
    int latencySamples() const override
    {
        // (EXPERIMENT: an external instrument that reports latency - the voices wait for it)
        // (D / AUX: D KONTAKT's latency delays everything before D, in series with C's)
        return synth.latencySamples() + synth.fxEngine().latencySamples() + (hybridType() == HybridType::external ? external.latencySamples() : 0)
               + (auxKontaktActive() ? auxKontakt.latencySamples() : 0);
    }
    /** EXPERIMENT: card C's hosted instrument (message thread: load, editor, state). */
    ExternalInstrument& externalInstrument() noexcept { return external; }
    const ExternalInstrument& externalInstrument() const noexcept { return external; }
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
    bool engineSourceIsPeriodic (int layer) const override
    {
        return layer == 1 && (layer2Shown() ? layer2Type() == HybridType::oscillator : hybridType() == HybridType::oscillator);
    }
    /** Cinematic V6 gate 3: card C shows its second layer (its 2 key, while the layer is on):
        the card's heading, picture, stand-in and Remove are the second layer's then. */
    bool layer2Shown() const;
    HybridType layer2Type() const;
    /** Card C empty (a SAMPLE without a recording, an INSTRUMENT PATCH without a sound): a full
        card with a faint stand-in picture and what to do. */
    juce::String engineSourcePlaceholder (int layer) const override;
    void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Performance& performance) noexcept override;
    int activeNotes() const noexcept override { return synth.activeVoices(); }
    void setShaperPattern (const osp::ShaperPattern& pattern) noexcept override { shaperPattern = pattern; }
    void setLaneShaperPattern (int lane, const osp::ShaperPattern& pattern) noexcept override
    {
        if (lane == 1)
            shaperPatternB = pattern;
    }
    float shaperPhase() const noexcept override { return synth.shaperPhase(); }
    float laneShaperPhase (int lane) const noexcept override { return synth.shaperPhase (lane); }
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
    /** RHYTHMIC MORPH: card C's recording as analysed for its rhythm (message thread; null: no
        recording, or still analysing), whether it is being analysed now, C RHYTHM's value and the
        loop's playhead (any thread). */
    std::shared_ptr<const rhythm::PreparedLoop> rhythmLoop() const;
    bool rhythmAnalysing() const noexcept { return loopAnalysing.load (std::memory_order_relaxed); }
    float rhythmValue() const noexcept { return synth.rhythmValue(); }
    double rhythmPlayhead() const noexcept { return synth.rhythmPlayhead(); }
    /** Tests: a prepared loop as if card C's recording had just been analysed (message thread). */
    void setRhythmLoopForTest (std::shared_ptr<const rhythm::PreparedLoop> loop);
    //== D / AUX (docs/aux) =====================================================================
    /** D RHYTHM's recording: its state for the strip and rack (message thread). */
    struct AuxRecordingState
    {
        enum class Status { none, loading, ready, missing, error };
        Status status = Status::none;
        juce::String name;      ///< the file's name (without extension)
        juce::String message;   ///< what went wrong (missing / error)
        /** A replacement that failed (unreadable, no audio, not copied): the recording before
            it plays on, unchanged, and this says why. Cleared by the next load. */
        juce::String lastError;
    };
    /** Message thread: a recording (WAV, AIFF, FLAC) for D RHYTHM. It is copied into
        `assetFolder` (named by its content, as card C's imports), decoded and analysed for its
        rhythm on the loader thread, and reaches the audio thread prepared. */
    void loadAuxRecording (const juce::File& recording, const juce::File& assetFolder);
    /** Message thread: D RHYTHM without a recording. */
    void clearAuxRecording();
    AuxRecordingState auxRecordingState() const;
    std::shared_ptr<const rhythm::PreparedLoop> auxLoop() const;
    /** Tests: waits until D's recording is no longer loading. */
    bool waitForAuxRecording (int timeoutMs) const;
    /** Tests: a prepared loop as if D's recording had just been analysed (message thread). */
    void setAuxLoopForTest (std::shared_ptr<const rhythm::PreparedLoop> loop);
    /** D KONTAKT's hosted instrument (message thread: load, editor, state). One per instance. */
    ExternalInstrument& auxInstrument() noexcept { return auxKontakt; }
    const ExternalInstrument& auxInstrument() const noexcept { return auxKontakt; }
    /** D's telemetry (any thread): its envelope (0..1), its loop's playhead (source seconds;
        -1 idle), its output peak (0..1), whether its bus runs. */
    float auxEnvelope() const noexcept { return synth.auxEnvelope(); }
    double auxPlayhead() const noexcept { return synth.auxPlayheadSeconds(); }
    float auxPeak() const noexcept { return synth.auxPeak(); }
    bool auxRunning() const noexcept { return synth.auxActive(); }

    /** SATELLITE's live place of card 0..2 (the panel's path display; read only, any thread). */
    Synth::SatelliteView satelliteView (int card) const noexcept { return synth.satelliteView (card); }
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

    /** The FX route (docs/FX_ROUTING_IMPLEMENTATION.md). Message thread. setFxRoute compiles and
        hands it to the audio thread; an invalid graph (or one holding an effect this build cannot
        play) is refused - false, nothing changes. CLASSIC (custom false): the original chain. */
    bool setFxRoute (const andor::analog::fx::Graph& route);

    /** USER MACROS V1.1 (docs/USER_MACROS_V1_1.md). Message thread. The names and assignments
        (the sound's state, saved with it); setUserMacros compiles the active assignments into
        the bounded table the audio thread reads (no lock, no allocation there) and tells the
        interface. Undo: the caller's (UserMacroEdit). */
    const UserMacroState& userMacros() const noexcept { return userMacroState; }
    void setUserMacros (const UserMacroState& state);
    /** Called (message thread) after the USER macros changed: an edit, undo, a preset, a session. */
    std::function<void()> onUserMacrosChanged;
    /** Moves on every change of the USER macros (message thread). */
    std::uint64_t userMacroRevision() const noexcept { return userMacroRevisionCount; }
    /** Tests: the offset (normalised) USER macros add to a parameter now (audio thread's smoothed
        value; read after rendering). */
    double userMacroOffsetOf (const juce::String& parameterId) const noexcept
    {
        const int i = params::readerIndexOf (parameterId);
        return i >= 0 && i < params::Reader::maxValues ? userMacroOffset[static_cast<std::size_t> (i)] : 0.0;
    }
    /** Tests: the table the audio thread reads now (message thread; the latest published). */
    const usermacro::Snapshot& userMacroTable() const noexcept { return userMacroCompiled; }
    andor::analog::fx::Graph fxRoute() const;
    /** Called (message thread) whenever the route changes: an edit, a session or preset. */
    std::function<void()> onFxRouteChanged;
    /** A new effect's processor (null for the original four, or before prepare): its meters. */
    const andor::analog::fx::Processor* fxProcessor (andor::analog::fx::InstanceId id) const;
    /** The voice filter's telemetry (Synth::FilterTap; any thread reads it); null before
        prepare. The FILTER rack's live graph. */
    const Synth::FilterTap* filterTap (int slot) const noexcept { return prepared ? &synth.filterTap (slot) : nullptr; }
    static constexpr int voiceSlots = Synth::maxVoices;
    /** The highest cutoff the filters reach at this rate (MIDI note). */
    float cutoffNoteHigh() const noexcept { return synth.cutoffNoteHigh(); }
    /** The impulse CONVOLUTION `slot` plays (null: none yet). */
    std::shared_ptr<const andor::analog::fx::Impulse> convolutionImpulse (int slot) const;
    /** Builds the impulse each CONVOLUTION needs when IMPULSE, DECAY, SIZE, REVERSE, NORMALIZE or
        the rate changed (not on the audio thread: prepare() and a 10 Hz timer call it). */
    void updateConvolutionImpulses();
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

    // Cinematic V6 gate 3: card C's second layer's own source (a file or an instrument patch),
    // kept apart from C1's: choosing one never replaces the other's. Saved as the engine's
    // Hybrid2Source child.
    /** Message thread: its definition (null: none), its recordings in `assetFolder`. */
    void setHybrid2Definition (std::unique_ptr<juce::XmlElement> definition, const juce::File& assetFolder);
    std::unique_ptr<juce::XmlElement> hybrid2Definition() const;
    SampleSourceState hybrid2State() const;
    bool waitForHybrid2 (int timeoutMs) const;
    /** Message thread: a recording for it (one zone over every key, rooted at its analysed
        pitch), copied into `assetFolder`; decoded and analysed on the loader thread. */
    void loadHybrid2File (const juce::File& recording, const juce::File& assetFolder);
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
    andor::analog::fxparams::Reader fxReader;   ///< the FX library's slots (analog.fx.*)
    struct ImpulseTimer final : juce::Timer
    {
        AnalogEngine* engine = nullptr;
        void timerCallback() override
        {
            engine->updateConvolutionImpulses();
            engine->external.collectGarbage();   // (EXPERIMENT: retired hosted instances)
            engine->auxKontakt.collectGarbage(); // (D KONTAKT's)
        }
    } impulseTimer;
    struct ImpulseKey
    {
        int impulse = -1;
        float decay = 0.0f, size = 0.0f;
        bool reverse = false, normalize = false;
        double rate = 0.0;
        bool operator== (const ImpulseKey&) const = default;
    };
    std::mutex impulseMutex;
    std::array<ImpulseKey, andor::analog::fx::capacity[static_cast<std::size_t> (andor::analog::fx::Type::convolution)]> impulseKeys {};
    std::uint64_t impulseGeneration = 0;
    mutable std::mutex routeMutex;   ///< (a host may save from another thread)
    andor::analog::fx::Graph route = andor::analog::fx::classicGraph();
    std::uint64_t routeGeneration = 1;
    params::Reader reader;
    osp::ShaperPattern shaperPattern = osp::RhythmicShaper::patternSteps (osp::ShaperParams().pattern);   ///< audio thread
    osp::ShaperPattern shaperPatternB = osp::RhythmicShaper::patternSteps (osp::ShaperParams().pattern);  ///< MOVEMENT lane B's (audio thread)
    std::vector<float> scratch;
    // EXPERIMENT (docs/experiments/KONTAKT_EXTERNAL_MORPH.md): card C's hosted instrument.
    ExternalInstrument external;
    juce::MidiBuffer externalMidi;              ///< card C's lane, this block (preallocated)
    std::vector<float> externalL, externalR;    ///< its stereo bus, this block
    std::int64_t externalClock = 0;
    /** Cinematic V6 SUB: the musician's own MIDI for this block (before the arpeggiator), from
        setPlayedKeys(); null: the notes process() is given are the musician's. */
    const juce::MidiBuffer* playedMidi = nullptr;
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

    // USER MACROS: the state (message thread), its compiled table and its hand-over, and the
    // audio thread's smoothing (each macro's value; each target's summed offset, so adding or
    // removing an assignment glides instead of stepping).
    void applyUserMacros (params::Reader::Values& values, int samples) noexcept;
    UserMacroState userMacroState;
    usermacro::Snapshot userMacroCompiled;
    usermacro::TripleBuffer<usermacro::Snapshot> userMacroTables;
    std::array<double, usermacro::macroCount> userMacroValue {};
    std::array<double, params::Reader::maxValues> userMacroWant {}, userMacroOffset {};
    std::array<bool, params::Reader::maxValues> userMacroTouched {};
    bool userMacroLive = false, userMacroPrimed = false;
    std::uint64_t userMacroRevisionCount = 1;
    double userMacroRate = 48000.0;
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
    // Card C's second layer's (as the above, its own).
    mutable std::mutex source2Mutex;
    std::unique_ptr<juce::XmlElement> source2Definition;
    SampleSourceState source2State;
    std::uint64_t source2Generation = 0;
    osp::ModelExchange<LoadedSource> source2Exchange;
    const LoadedSource* source2Playing = nullptr;      ///< audio thread
    std::shared_ptr<const SampleSource> source2Loaded;   ///< its picture
    static void outlineOf (const SampleSource* source, std::vector<float>& cycle);
    bool source2FileLoading = false;   ///< (a recording being made into its source: under source2Mutex)
    std::shared_ptr<int> lifetime = std::make_shared<int> (0);   ///< (the loader's answers on the message thread check it)
    // RHYTHMIC MORPH: card C's recording analysed for its rhythm on the loader thread, handed to
    // the audio thread as a prepared loop (never analysed there).
    struct LoadedLoop
    {
        std::uint64_t generation = 0;
        std::shared_ptr<const rhythm::PreparedLoop> loop;   ///< null: none
    };
    mutable std::mutex loopMutex;
    std::uint64_t loopGeneration = 0;
    std::shared_ptr<const rhythm::PreparedLoop> loopLatest;   ///< (the card's display)
    osp::ModelExchange<LoadedLoop> loopExchange;               ///< (published under loopMutex)
    const LoadedLoop* loopPlaying = nullptr;                   ///< audio thread
    std::atomic<bool> loopAnalysing { false };
    std::atomic<float>* rhythmEnabledValue = nullptr;
    std::atomic<float>* rhythmAudioValue = nullptr;
    void analyseRhythm (std::shared_ptr<const osp::plugin::LoadedInstrument> source);
    void publishLoop (std::shared_ptr<const rhythm::PreparedLoop> loop, std::uint64_t generation);
    void loadHybrid2 (std::unique_ptr<juce::XmlElement> definition, juce::File assetFolder, std::uint64_t generation);

    // D / AUX: its RHYTHM recording (decoded and analysed on the loader thread, handed over as a
    // prepared loop, as card C's), its KONTAKT instance and its MIDI lane.
    mutable std::mutex auxMutex;
    std::uint64_t auxGeneration = 0;
    AuxRecordingState auxState;
    juce::String auxAsset;                                   ///< the stored file's name ("" none)
    juce::String auxLoading;                                 ///< a replacement on its way (its name; "" none)
    std::shared_ptr<const rhythm::PreparedLoop> auxLatest;   ///< (the strip's display)
    osp::ModelExchange<LoadedLoop> auxExchange;              ///< (published under auxMutex)
    const LoadedLoop* auxPlaying = nullptr;                  ///< audio thread
    void publishAuxLoop (std::shared_ptr<const rhythm::PreparedLoop> loop, std::uint64_t generation, AuxRecordingState state, juce::String asset);
    void decodeAuxRecording (juce::File stored, juce::String name, juce::String asset, std::uint64_t generation, bool keepOnFailure);
    void failAuxLoad (std::uint64_t generation, const juce::String& why);
    ExternalInstrument auxKontakt;
    juce::MidiBuffer auxMidi;                 ///< D KONTAKT's lane, this block (preallocated)
    std::vector<float> auxExtL, auxExtR;      ///< its stereo bus, this block
    std::atomic<float>* auxEnabledValue = nullptr;
    std::atomic<float>* auxModeValue = nullptr;
    bool auxKontaktActive() const noexcept
    {
        return auxEnabledValue != nullptr && auxEnabledValue->load() >= 0.5f && auxModeValue != nullptr
               && juce::roundToInt (auxModeValue->load()) == static_cast<int> (Patch::Aux::Mode::kontakt)
               && auxKontakt.state() == ExternalInstrument::State::ready;
    }
};

} // namespace andor::analog
