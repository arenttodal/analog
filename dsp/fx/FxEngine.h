#pragma once

#include "FxGraph.h"
#include "FxProcessor.h"

#include "engine/PostProcessor.h"
#include "model/ModelExchange.h"

#include <array>
#include <memory>
#include <vector>

namespace andor::analog::fx
{

struct Impulse;

/** A delay that aligns one path to another (a parallel lane, a group's dry): a ring, sized in
    prepare / compile, read at any delay below its size. */
struct AlignLine
{
    std::vector<float> l, r;
    int pos = 0;
    void allocate (int capacity);
    void clear() noexcept;
    /** In place: writes the block and replaces it with itself `delay` samples ago. */
    void run (float* left, float* right, int n, int delay) noexcept;
};

/** A graph ready for the audio thread: immutable once published (ModelExchange); its alignment
    delays are the audio thread's (allocated here, written only while it plays this graph). */
struct CompiledGraph
{
    /** The longest a parallel lane can be delayed to meet the slowest one (at 192 kHz: two
        RETROs and a spectral effect in one lane fit). */
    static constexpr int alignCapacity = 16384;
    std::uint64_t generation = 0;
    Graph graph;
    int latency = 0;   ///< (unused: the latency is planned live from the processors' maxLatency)
    std::vector<int> groupBase;               ///< a group's first line (pre-order): its lanes, then its dry
    mutable std::vector<AlignLine> aligns;
};

/** Builds the audio thread's copy of a valid graph (empty pointer: not valid). Message thread. */
std::shared_ptr<const CompiledGraph> compile (const Graph& g, std::uint64_t generation);

/**
    Runs the post-voice effects: CLASSIC calls osp::PostProcessor::process, untouched (the
    original chain, bit for bit); CUSTOM walks the compiled graph, the original four through
    PostProcessor's stage entry points (customDrive ... customSpace) and the new effects through
    their own processors (FxProcessor).

    Graph changes arrive through a lock-free exchange and take effect at a block boundary with a
    short duck (5 ms out, 5 ms in): the processors keep their state across it, so tails go on.
    Bypass (a node's or an effect's ON) glides over 20 ms; a bypassed ECHO, SPACE or other wet
    processor stops hearing new input while its tail drains.

    prepare() allocates; process() never allocates, locks or waits.
*/
class FxEngine
{
public:
    FxEngine();
    ~FxEngine();

    void prepare (double sampleRate, int maximumBlockSize);
    void reset() noexcept;

    /** Message thread: the route to play (null or a CLASSIC graph: the original chain). */
    void setGraph (std::shared_ptr<const CompiledGraph> graph);
    /** Message thread: frees graphs (and impulses) the audio thread no longer reads. */
    void collectGarbage();

    /** Audio thread: the new effects' settings (taken by value; no allocation). */
    void setSettings (const Settings& settings) noexcept;

    /** Audio thread: one block, in place, after the voices (the PostProcessor's slot). */
    void process (osp::PostProcessor& legacy, float* left, float* right, int numSamples) noexcept;

    /** The route now playing is CUSTOM (any thread; for the interface and tests). */
    bool playingCustom() const noexcept { return customNow.load (std::memory_order_relaxed); }
    /** Samples the CUSTOM route adds after the voices (0 in CLASSIC): the longest path's
        effects at their maxLatency; every shorter lane and bypassed effect is delayed to it. */
    int latencySamples() const noexcept { return latencyNow.load (std::memory_order_relaxed); }

    /** The processor of a new effect's slot (null for the original four). For meters. */
    const Processor* processor (InstanceId id) const noexcept;

    /** Message thread: the impulse CONVOLUTION `slot` plays next (crossfaded in), and the one it
        was last handed (null: none yet). Impulses are built at rate() (Impulse::make). */
    void setImpulse (int slot, std::shared_ptr<const Impulse> impulse);
    std::shared_ptr<const Impulse> impulse (int slot) const;
    double rate() const noexcept { return sampleRate; }

private:
    struct Buffers
    {
        std::vector<float> inL, inR, accL, accR, workL, workR, wetL, wetR, dry;
    };

    void runCustom (osp::PostProcessor& legacy, const CompiledGraph& g, float* left, float* right, int n) noexcept;
    void runChain (osp::PostProcessor& legacy, const std::vector<Node>& chain, float* left, float* right, int n, int depth, int& group) noexcept;
    void runEffect (osp::PostProcessor& legacy, const Node& node, float* left, float* right, int n, int depth) noexcept;
    void runGroup (osp::PostProcessor& legacy, const Node& node, float* left, float* right, int n, int depth, int& group) noexcept;
    int chainLatency (const std::vector<Node>& chain) const noexcept;
    int effectLatency (InstanceId id) const noexcept;

    double sampleRate = 48000.0;
    int maxBlock = 512;
    osp::ModelExchange<CompiledGraph> exchange;
    const CompiledGraph* current = nullptr;
    const CompiledGraph* waiting = nullptr;   ///< arrived, applied once the duck is down
    std::array<Buffers, maxDepth + 2> buffers;
    std::array<std::unique_ptr<Processor>, totalInstances()> processors;
    std::array<float, totalInstances()> active {};   ///< the bypass glide, 0..1
    std::array<AlignLine, totalInstances()> dryLines, padLines;   ///< a latent effect's dry (at its maxLatency) and its pad (maxLatency - latency)
    float bypassStep = 0.001f;
    float duck = 1.0f, duckStep = 0.004f;
    bool ducking = false, played = false;
    Settings settings;
    std::atomic<bool> customNow { false };
    std::atomic<int> latencyNow { 0 };
};

} // namespace andor::analog::fx
