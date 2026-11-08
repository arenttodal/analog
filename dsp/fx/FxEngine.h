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

/** A graph ready for the audio thread: immutable once published (ModelExchange). */
struct CompiledGraph
{
    std::uint64_t generation = 0;
    Graph graph;
    int latency = 0;   ///< samples the longest path adds (lanes are aligned to it)
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
    /** Message thread: frees graphs the audio thread no longer reads. */
    void collectGarbage() { exchange.collectGarbage(); }

    /** Audio thread: the new effects' settings (taken by value; no allocation). */
    void setSettings (const Settings& settings) noexcept;

    /** Audio thread: one block, in place, after the voices (the PostProcessor's slot). */
    void process (osp::PostProcessor& legacy, float* left, float* right, int numSamples) noexcept;

    /** The route now playing is CUSTOM (any thread; for the interface and tests). */
    bool playingCustom() const noexcept { return customNow.load (std::memory_order_relaxed); }
    /** Samples the CUSTOM route adds after the voices (0 in CLASSIC). */
    int latencySamples() const noexcept { return latencyNow.load (std::memory_order_relaxed); }

    /** The processor of a new effect's slot (null for the original four). For meters. */
    const Processor* processor (InstanceId id) const noexcept;

private:
    struct Buffers
    {
        std::vector<float> inL, inR, accL, accR, workL, workR, wetL, wetR, dry;
    };

    void runCustom (osp::PostProcessor& legacy, const Graph& g, float* left, float* right, int n) noexcept;
    void runChain (osp::PostProcessor& legacy, const std::vector<Node>& chain, float* left, float* right, int n, int depth) noexcept;
    void runEffect (osp::PostProcessor& legacy, const Node& node, float* left, float* right, int n, int depth) noexcept;
    void runGroup (osp::PostProcessor& legacy, const Node& node, float* left, float* right, int n, int depth) noexcept;

    double sampleRate = 48000.0;
    int maxBlock = 512;
    osp::ModelExchange<CompiledGraph> exchange;
    const CompiledGraph* current = nullptr;
    const CompiledGraph* waiting = nullptr;   ///< arrived, applied once the duck is down
    std::array<Buffers, maxDepth + 2> buffers;
    std::array<std::unique_ptr<Processor>, totalInstances()> processors;
    std::array<float, totalInstances()> active {};   ///< the bypass glide, 0..1
    float bypassStep = 0.001f;
    float duck = 1.0f, duckStep = 0.004f;
    bool ducking = false, played = false;
    Settings settings;
    std::atomic<bool> customNow { false };
    std::atomic<int> latencyNow { 0 };
};

} // namespace andor::analog::fx
