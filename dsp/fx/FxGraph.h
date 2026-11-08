#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace andor::analog::fx
{

/**
    The FX routing's graph (docs/FX_ROUTING_IMPLEMENTATION.md): what follows the voices, as the
    musician arranged it. A value type, built and checked on the message thread; the audio
    thread only ever sees a compiled, immutable copy (FxEngine).

    CLASSIC (custom == false) is not a graph that runs: it is the original post-voice chain
    (osp::PostProcessor::process), exactly as before. classicGraph() is how it is *drawn*, and the
    starting point of the first edit, which makes the route CUSTOM.
*/

/** The processors (append only: saved in sessions by key). */
enum class Type : int
{
    drive, movement, echo, space,                                   // the original four (one each)
    spread, compressor, eq, filter, phaser, flanger, shifter, convolution, utility,
    count
};
constexpr int typeCount = static_cast<int> (Type::count);

/** How many of each a graph may hold (fixed pools: their host parameters are registered once). */
constexpr std::array<int, typeCount> capacity { 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 1, 1, 2 };

/** Its saved key ("eq") and its name for people ("EQUALIZER"). */
const char* typeKey (Type t) noexcept;
const char* typeName (Type t) noexcept;
bool typeFromKey (const std::string& key, Type& t) noexcept;
/** The original four: their processors are the instrument's own (osp::PostProcessor). */
constexpr bool isLegacy (Type t) noexcept { return static_cast<int> (t) < 4; }

/** One processor of a pool: a type and its slot (0-based; saved as "eq.1" for slot 0). */
struct InstanceId
{
    Type type = Type::drive;
    int slot = 0;
    bool operator== (const InstanceId& o) const noexcept { return type == o.type && slot == o.slot; }
    bool operator!= (const InstanceId& o) const noexcept { return ! (*this == o); }
    std::string key() const;                       ///< "eq.1"
    static bool fromKey (const std::string& key, InstanceId& id) noexcept;
    int flatIndex() const noexcept;                ///< 0 .. totalInstances - 1
};

constexpr int totalInstances()
{
    int n = 0;
    for (const int c : capacity)
        n += c;
    return n;
}

/** The parallel group's law: LINEAR (the lanes' outputs weighted, the weights summing to one -
    two untouched lanes give the input back exactly) or SEND (the input once plus what each lane
    adds to it: y = x + sum g (lane (x) - x) - how CLASSIC's ECHO and SPACE sends combine). */
enum class Law : int { linear, send };

/** The spectral and mid / side splitters (Advanced): each lane hears its band (or M / S). */
enum class SplitKind : int { none, bands2, bands3, midSide };

struct Node
{
    enum class Kind : int { effect, parallel, split };
    Kind kind = Kind::effect;
    // effect
    InstanceId fx;
    bool bypassed = false;             ///< (the original four's bypass; a new effect's is its ON parameter)
    // parallel / split: two (three for bands3) lanes, each a chain
    std::vector<std::vector<Node>> lanes;
    std::vector<float> gains;          ///< per lane, linear
    Law law = Law::linear;
    SplitKind split = SplitKind::none;
    float lowHz = 250.0f, highHz = 2500.0f;   ///< the splitters' crossovers
    int slope = 24;                    ///< dB / octave: 12 or 24

    static Node effect (InstanceId id) { Node n; n.fx = id; return n; }
    static Node parallel (std::vector<Node> a, std::vector<Node> b, Law law = Law::linear);
    static Node splitter (SplitKind kind, std::vector<std::vector<Node>> lanes);
};

struct Graph
{
    static constexpr int schema = 1;
    bool custom = false;
    std::vector<Node> chain;

    bool operator== (const Graph& o) const;
    bool operator!= (const Graph& o) const { return ! (*this == o); }
};

/** CLASSIC as drawn: DRIVE -> MOVEMENT -> (ECHO || SPACE), the pair a SEND group. */
Graph classicGraph();

/** Every effect the graph holds, in order. */
std::vector<InstanceId> instancesOf (const Graph& g);
bool contains (const Graph& g, InstanceId id);

/** Empty when the graph is valid; else why not, in words. Checks: each processor at most
    once, slots within the pools, at most 16 effects, lanes 2 (3 for a three-band split), gains
    finite and >= 0, nesting at most 4 deep, crossovers in range and ordered. */
std::string validate (const Graph& g);

/** The saved form (one line of words; stable: append only) and back. fromText returns false
    (leaving `g` alone) on anything it does not understand. */
std::string toText (const Graph& g);
bool fromText (const std::string& text, Graph& g);

constexpr int maxEffects = 16;
constexpr int maxDepth = 4;

} // namespace andor::analog::fx
