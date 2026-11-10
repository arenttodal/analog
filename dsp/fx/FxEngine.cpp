#include "FxEngine.h"
#include "Effects.h"

#include <algorithm>
#include <cstring>

namespace andor::analog::fx
{

void AlignLine::allocate (int capacity)
{
    l.assign (static_cast<std::size_t> (std::max (1, capacity)), 0.0f);
    r.assign (l.size(), 0.0f);
    pos = 0;
}

void AlignLine::clear() noexcept
{
    std::fill (l.begin(), l.end(), 0.0f);
    std::fill (r.begin(), r.end(), 0.0f);
    pos = 0;
}

void AlignLine::run (float* left, float* right, int n, int delay) noexcept
{
    const int size = static_cast<int> (l.size());
    delay = std::clamp (delay, 0, size - 1);
    if (delay == 0)
        return;
    for (int i = 0; i < n; ++i)
    {
        l[static_cast<std::size_t> (pos)] = left[i];
        r[static_cast<std::size_t> (pos)] = right[i];
        int at = pos - delay;
        if (at < 0)
            at += size;
        left[i] = l[static_cast<std::size_t> (at)];
        right[i] = r[static_cast<std::size_t> (at)];
        if (++pos == size)
            pos = 0;
    }
}

namespace
{
    /** A group's alignment lines, pre-order (the order the engine walks them). */
    /** Whether `chain` holds `id` (no allocation: the audio thread asks). */
    bool holds (const std::vector<Node>& chain, InstanceId id) noexcept
    {
        for (const auto& n : chain)
        {
            if (n.kind == Node::Kind::effect ? n.fx == id : false)
                return true;
            for (const auto& lane : n.lanes)
                if (holds (lane, id))
                    return true;
        }
        return false;
    }

    void planGroups (const std::vector<Node>& chain, CompiledGraph& c)
    {
        for (const auto& n : chain)
        {
            if (n.kind == Node::Kind::effect)
                continue;
            c.groupBase.push_back (static_cast<int> (c.aligns.size()));
            c.aligns.resize (c.aligns.size() + n.lanes.size() + 1);
            for (const auto& lane : n.lanes)
                planGroups (lane, c);
        }
    }

    bool hasSplitter (const std::vector<Node>& chain)
    {
        for (const auto& n : chain)
        {
            if (n.kind == Node::Kind::split)
                return true;
            for (const auto& lane : n.lanes)
                if (hasSplitter (lane))
                    return true;
        }
        return false;
    }
}

std::shared_ptr<const CompiledGraph> compile (const Graph& g, std::uint64_t generation)
{
    if (! validate (g).empty())
        return {};
    if (hasSplitter (g.chain) || hasSplitter (g.send))
        return {};   // (the band / M-S splitters are not played yet: refused, never approximated)
    for (const auto& id : instancesOf (g))
        if (! implemented (id.type))
            return {};
    auto c = std::make_shared<CompiledGraph>();
    c->generation = generation;
    c->graph = g;
    c->latency = 0;
    planGroups (c->graph.chain, *c);
    planGroups (c->graph.send, *c);   // (MAIN + SEND: SEND's groups after MAIN's, in the order they run)
    for (auto& line : c->aligns)
        line.allocate (CompiledGraph::alignCapacity);
    return c;
}

FxEngine::FxEngine() = default;
FxEngine::~FxEngine() = default;

void FxEngine::prepare (double rate, int maximumBlockSize)
{
    sampleRate = rate;
    maxBlock = std::max (1, maximumBlockSize);
    for (auto& b : buffers)
        for (auto* v : { &b.inL, &b.inR, &b.accL, &b.accR, &b.workL, &b.workR, &b.wetL, &b.wetR, &b.dry })
            v->assign (static_cast<std::size_t> (maxBlock), 0.0f);
    for (int t = 4; t < typeCount; ++t)
        for (int s = 0; s < capacity[static_cast<std::size_t> (t)]; ++s)
        {
            const InstanceId id { static_cast<Type> (t), s };
            auto& p = processors[static_cast<std::size_t> (id.flatIndex())];
            if (p == nullptr)
                p = makeProcessor (id.type);   // (kept across prepares: a CONVOLUTION keeps its impulse)
            if (p != nullptr)
            {
                p->prepare (rate, maxBlock);
                p->set (settings, s);
            }
            // A latent effect's dry (its bypass, its glide) and its pad, at this rate.
            const int most = p != nullptr ? p->maxLatency() : 0;
            dryLines[static_cast<std::size_t> (id.flatIndex())].allocate (most > 0 ? most + 1 : 1);
            padLines[static_cast<std::size_t> (id.flatIndex())].allocate (most > 0 ? most + 1 : 1);
        }
    sendL.assign (static_cast<std::size_t> (maxBlock), 0.0f);
    sendR.assign (static_cast<std::size_t> (maxBlock), 0.0f);
    mainAlign.allocate (CompiledGraph::alignCapacity);
    sendAlign.allocate (CompiledGraph::alignCapacity);
    bypassStep = static_cast<float> (1.0 / (0.02 * rate));
    duckStep = static_cast<float> (1.0 / (0.005 * rate));
    reset();
}

void FxEngine::reset() noexcept
{
    for (auto& p : processors)
        if (p != nullptr)
            p->reset();
    for (int i = 0; i < totalInstances(); ++i)
        active[static_cast<std::size_t> (i)] = 1.0f;
    for (auto* lines : { &dryLines, &padLines })
        for (auto& line : *lines)
            line.clear();
    if (current != nullptr)
        for (auto& line : current->aligns)
            line.clear();
    mainAlign.clear();
    sendAlign.clear();
    duck = 1.0f;
    ducking = false;
    played = false;
}

void FxEngine::setGraph (std::shared_ptr<const CompiledGraph> graph)
{
    if (graph == nullptr)
    {
        // CLASSIC: an empty CLASSIC graph (the exchange only carries real objects).
        auto classic = std::make_shared<CompiledGraph>();
        classic->generation = 0;
        graph = classic;
    }
    exchange.publish (std::move (graph));
}

void FxEngine::setSettings (const Settings& s) noexcept
{
    settings = s;
    applySettings();
}

void FxEngine::applySettings() noexcept
{
    // On SEND, a CONVOLUTION gives its wet alone (its MIX would hand SEND's input back dry).
    if (current != nullptr && current->graph.mainSend)
        for (int slot = 0; slot < capacity[static_cast<std::size_t> (Type::convolution)]; ++slot)
            if (! holds (current->graph.chain, InstanceId { Type::convolution, slot }))
                settings.conv[static_cast<std::size_t> (slot)].mix = 1.0f;
    for (int t = 4; t < typeCount; ++t)
        for (int slot = 0; slot < capacity[static_cast<std::size_t> (t)]; ++slot)
            if (auto& p = processors[static_cast<std::size_t> (InstanceId { static_cast<Type> (t), slot }.flatIndex())]; p != nullptr)
                p->set (settings, slot);
}

const Processor* FxEngine::processor (InstanceId id) const noexcept
{
    return processors[static_cast<std::size_t> (id.flatIndex())].get();
}

void FxEngine::collectGarbage()
{
    exchange.collectGarbage();
    for (int s = 0; s < capacity[static_cast<std::size_t> (Type::convolution)]; ++s)
        if (auto* c = dynamic_cast<Convolution*> (processors[static_cast<std::size_t> (InstanceId { Type::convolution, s }.flatIndex())].get()))
            c->collectGarbage();
}

void FxEngine::setImpulse (int slot, std::shared_ptr<const Impulse> ir)
{
    if (slot >= 0 && slot < capacity[static_cast<std::size_t> (Type::convolution)])
        if (auto* c = dynamic_cast<Convolution*> (processors[static_cast<std::size_t> (InstanceId { Type::convolution, slot }.flatIndex())].get()))
            c->setImpulse (std::move (ir));
}

std::shared_ptr<const Impulse> FxEngine::impulse (int slot) const
{
    if (slot >= 0 && slot < capacity[static_cast<std::size_t> (Type::convolution)])
        if (const auto* c = dynamic_cast<const Convolution*> (processors[static_cast<std::size_t> (InstanceId { Type::convolution, slot }.flatIndex())].get()))
            return c->impulse();
    return {};
}

void FxEngine::process (osp::PostProcessor& legacy, float* left, float* right, int numSamples, const float* sendLeft, const float* sendRight) noexcept
{
    if (const auto* next = exchange.takePending(); next != nullptr && next != current)
    {
        const bool sameSound = current == nullptr ? ! next->graph.custom : (! current->graph.custom && ! next->graph.custom) || current->graph == next->graph;
        if (sameSound || ! played)
        {
            current = next;   // (CLASSIC again, the same route, or nothing played yet: nothing to duck)
            waiting = nullptr;
            ducking = false;
            applySettings();
        }
        else
        {
            waiting = next;
            ducking = true;
        }
    }
    played = true;
    int done = 0;
    while (done < numSamples)
    {
        float* l = left + done;
        float* r = right + done;
        const int remaining = numSamples - done;
        const bool custom = current != nullptr && current->graph.custom;
        int run = remaining;
        if (custom)
            run = legacy.customControl (std::min (remaining, maxBlock));
        else if (ducking)
            run = std::min (remaining, 32);
        if (custom && current->graph.mainSend)
            runMainSend (legacy, *current, l, r, run, sendLeft != nullptr ? sendLeft + done : nullptr, sendRight != nullptr ? sendRight + done : nullptr);
        else if (custom)
            runCustom (legacy, *current, l, r, run);
        else
            legacy.process (l, r, run);   // CLASSIC: the original chain, as it always was

        if (ducking || duck < 1.0f)
        {
            for (int i = 0; i < run; ++i)
            {
                duck = ducking ? std::max (0.0f, duck - duckStep) : std::min (1.0f, duck + duckStep);
                l[i] *= duck;
                r[i] *= duck;
            }
            if (ducking && duck <= 0.0f)
            {
                current = waiting;   // switched at silence; the processors keep their state
                waiting = nullptr;
                ducking = false;
                applySettings();   // (a CONVOLUTION moved to or from SEND)
            }
        }
        done += run;
    }
    if (current != nullptr)
        exchange.publishOldestInUse (waiting != nullptr ? std::min (current->generation, waiting->generation) : current->generation);
    customNow.store (current != nullptr && current->graph.custom, std::memory_order_relaxed);
    mainSendNow.store (current != nullptr && current->graph.mainSend, std::memory_order_relaxed);
    int latency = 0;
    if (current != nullptr && current->graph.custom)
        latency = current->graph.mainSend ? std::max (chainLatency (current->graph.chain), chainLatency (current->graph.send)) : chainLatency (current->graph.chain);
    latencyNow.store (latency, std::memory_order_relaxed);
}

int FxEngine::effectLatency (InstanceId id) const noexcept
{
    const auto* p = isLegacy (id.type) ? nullptr : processors[static_cast<std::size_t> (id.flatIndex())].get();
    return p != nullptr ? p->maxLatency() : 0;
}

int FxEngine::chainLatency (const std::vector<Node>& chain) const noexcept
{
    // A chain: its effects add up; a group: its slowest lane (the others are aligned to it).
    int total = 0;
    for (const auto& node : chain)
    {
        if (node.kind == Node::Kind::effect)
            total += effectLatency (node.fx);
        else
        {
            int slowest = 0;
            for (const auto& lane : node.lanes)
                slowest = std::max (slowest, chainLatency (lane));
            total += slowest;
        }
    }
    return total;
}

void FxEngine::runCustom (osp::PostProcessor& legacy, const CompiledGraph& g, float* left, float* right, int n) noexcept
{
    legacy.customReimagined (left, right, n);   // (locked upstream: before every routed effect)
    int group = 0;
    runChain (legacy, g.graph.chain, left, right, n, 0, group);
}

void FxEngine::runMainSend (osp::PostProcessor& legacy, const CompiledGraph& g, float* left, float* right, int n, const float* sendLeft,
                            const float* sendRight) noexcept
{
    // MAIN: the voices' sum (KALEIDOSCOPE's stage first, as CUSTOM), its inserts.
    legacy.customReimagined (left, right, n);
    int group = 0;
    runChain (legacy, g.graph.chain, left, right, n, 0, group);
    // SEND: the cards' sends alone, its chain wet-only (nothing dry ever returns).
    const auto bytes = sizeof (float) * static_cast<std::size_t> (n);
    if (sendLeft != nullptr && sendRight != nullptr)
    {
        std::memcpy (sendL.data(), sendLeft, bytes);
        std::memcpy (sendR.data(), sendRight, bytes);
    }
    else
    {
        std::fill (sendL.begin(), sendL.begin() + n, 0.0f);
        std::fill (sendR.begin(), sendR.begin() + n, 0.0f);
    }
    sendRoot = true;
    const bool wet = runChain (legacy, g.graph.send, sendL.data(), sendR.data(), n, 0, group);
    sendRoot = false;
    if (! wet)
        return;   // (a SEND with no spatial processor returns nothing)
    // The two met at the slower path's latency.
    const int lm = chainLatency (g.graph.chain), ls = chainLatency (g.graph.send), most = std::max (lm, ls);
    if (most > lm)
        mainAlign.run (left, right, n, most - lm);
    if (most > ls)
        sendAlign.run (sendL.data(), sendR.data(), n, most - ls);
    for (int i = 0; i < n; ++i)
    {
        left[i] += sendL[static_cast<std::size_t> (i)];
        right[i] += sendR[static_cast<std::size_t> (i)];
    }
}

bool FxEngine::runChain (osp::PostProcessor& legacy, const std::vector<Node>& chain, float* left, float* right, int n, int depth, int& group, bool wet) noexcept
{
    for (const auto& node : chain)
    {
        if (node.kind == Node::Kind::effect)
        {
            runEffect (legacy, node, left, right, n, depth);
            wet = wet || (sendRoot && isSpatial (node.fx.type));
        }
        else
            wet = runGroup (legacy, node, left, right, n, depth, group, wet) || wet;
    }
    return wet;
}

void FxEngine::runEffect (osp::PostProcessor& legacy, const Node& node, float* left, float* right, int n, int depth) noexcept
{
    const auto id = node.fx;
    const auto index = static_cast<std::size_t> (id.flatIndex());
    const bool target = isLegacy (id.type) ? ! node.bypassed : isOn (settings, id);
    float& a = active[index];
    auto& b = buffers[static_cast<std::size_t> (depth)];
    const bool gliding = a != (target ? 1.0f : 0.0f);
    // The glide's value at each sample (in `dry`): only when it moves.
    if (gliding)
        for (int i = 0; i < n; ++i)
        {
            a = target ? std::min (1.0f, a + bypassStep) : std::max (0.0f, a - bypassStep);
            b.dry[static_cast<std::size_t> (i)] = a;
        }

    switch (id.type)
    {
        case Type::echo:
        case Type::space:
        {
            // Wet processors: y = dry x + wet, the input gated by the glide so a bypassed one
            // drains its tail. Each runs every block it is in the graph (its level and sleep).
            float* wl = b.wetL.data();
            float* wr = b.wetR.data();
            float* dryGain = b.accL.data();
            if (gliding)
            {
                for (int i = 0; i < n; ++i)
                {
                    b.inL[static_cast<std::size_t> (i)] = b.dry[static_cast<std::size_t> (i)] * left[i];
                    b.inR[static_cast<std::size_t> (i)] = b.dry[static_cast<std::size_t> (i)] * right[i];
                }
            }
            const float* inL = gliding ? b.inL.data() : left;
            const float* inR = gliding ? b.inR.data() : right;
            const float input = gliding ? 1.0f : a;
            if (sendRoot)
            {
                // On SEND: its wet alone (the macro's level, once); what came in is not passed on.
                if (id.type == Type::echo)
                    legacy.customEcho (inL, inR, wl, wr, n, input);
                else
                    legacy.customSpace (inL, inR, wl, wr, dryGain, n, input);
                std::memcpy (left, wl, sizeof (float) * static_cast<std::size_t> (n));
                std::memcpy (right, wr, sizeof (float) * static_cast<std::size_t> (n));
                return;
            }
            if (id.type == Type::echo)
            {
                legacy.customEcho (inL, inR, wl, wr, n, input);
                for (int i = 0; i < n; ++i)
                {
                    left[i] += wl[i];
                    right[i] += wr[i];
                }
            }
            else
            {
                legacy.customSpace (inL, inR, wl, wr, dryGain, n, input);
                for (int i = 0; i < n; ++i)
                {
                    const float g = gliding ? b.dry[static_cast<std::size_t> (i)] : a;
                    const float d = 1.0f + g * (dryGain[i] - 1.0f);
                    left[i] = d * left[i] + wl[i];
                    right[i] = d * right[i] + wr[i];
                }
            }
            return;
        }
        default:
            break;
    }

    // Inserts: y = x + a (f (x) - x); not run while fully bypassed. A latent one (RETRO,
    // CONVOLUTION, BLUR, a through-zero FLANGER) delays its dry by its maxLatency and pads its
    // wet up to it: on, off or gliding, the route's latency is the same.
    auto* p = processors[index].get();
    const int most = p != nullptr ? p->maxLatency() : 0;
    const int pad = p != nullptr ? std::max (0, most - p->latency()) : 0;
    const auto bytes = sizeof (float) * static_cast<std::size_t> (n);
    if (most > 0)
    {
        std::memcpy (b.inL.data(), left, bytes);
        std::memcpy (b.inR.data(), right, bytes);
        dryLines[index].run (b.inL.data(), b.inR.data(), n, most);   // (always fed: no jump into a glide)
    }
    const bool wetOnly = sendRoot && isSpatial (id.type);   // (SEND's CONVOLUTION: its wet alone)
    if (a <= 0.0f && ! gliding)
    {
        if (wetOnly)
        {
            std::fill (left, left + n, 0.0f);
            std::fill (right, right + n, 0.0f);
            return;
        }
        if (most > 0)
        {
            std::memcpy (left, b.inL.data(), bytes);
            std::memcpy (right, b.inR.data(), bytes);
        }
        return;
    }
    const bool tail = p != nullptr && p->hasTail();
    if (! gliding && a >= 1.0f)
    {
        if (id.type == Type::drive)
            legacy.customDrive (left, right, n);
        else if (id.type == Type::movement)
            legacy.customMovement (left, right, n);
        else if (p != nullptr)
            p->process (left, right, n);
        if (pad > 0)
            padLines[index].run (left, right, n, pad);
        return;
    }
    // Gliding (or a tail draining): process a copy, crossfade.
    std::memcpy (b.workL.data(), left, sizeof (float) * static_cast<std::size_t> (n));
    std::memcpy (b.workR.data(), right, sizeof (float) * static_cast<std::size_t> (n));
    if (tail)
        for (int i = 0; i < n; ++i)
        {
            const float g = gliding ? b.dry[static_cast<std::size_t> (i)] : a;
            b.workL[static_cast<std::size_t> (i)] *= g;
            b.workR[static_cast<std::size_t> (i)] *= g;
        }
    if (id.type == Type::drive)
        legacy.customDrive (b.workL.data(), b.workR.data(), n);
    else if (id.type == Type::movement)
        legacy.customMovement (b.workL.data(), b.workR.data(), n);
    else if (p != nullptr)
        p->process (b.workL.data(), b.workR.data(), n);
    if (pad > 0)
        padLines[index].run (b.workL.data(), b.workR.data(), n, pad);
    const float* dl = most > 0 ? b.inL.data() : left;   // (the dry, aligned to the wet)
    const float* dr = most > 0 ? b.inR.data() : right;
    for (int i = 0; i < n; ++i)
    {
        const float g = gliding ? b.dry[static_cast<std::size_t> (i)] : a;
        const auto k = static_cast<std::size_t> (i);
        if (wetOnly)
        {
            left[i] = b.workL[k];
            right[i] = b.workR[k];
        }
        else if (tail)
        {
            left[i] = (1.0f - g) * dl[i] + b.workL[k];
            right[i] = (1.0f - g) * dr[i] + b.workR[k];
        }
        else
        {
            left[i] = dl[i] + g * (b.workL[k] - dl[i]);
            right[i] = dr[i] + g * (b.workR[k] - dr[i]);
        }
    }
}

bool FxEngine::runGroup (osp::PostProcessor& legacy, const Node& node, float* left, float* right, int n, int depth, int& group, bool wet) noexcept
{
    auto& b = buffers[static_cast<std::size_t> (depth)];
    const auto bytes = sizeof (float) * static_cast<std::size_t> (n);
    // This group's alignment lines (pre-order, as compile() planned them): each lane delayed to
    // the slowest one, the dry (a send's) to it too.
    const int base = group < static_cast<int> (current->groupBase.size()) ? current->groupBase[static_cast<std::size_t> (group)] : -1;
    ++group;
    int slowest = 0;
    for (const auto& lane : node.lanes)
        slowest = std::max (slowest, chainLatency (lane));
    std::memcpy (b.inL.data(), left, bytes);
    std::memcpy (b.inR.data(), right, bytes);
    std::fill (b.accL.begin(), b.accL.begin() + n, 0.0f);
    std::fill (b.accR.begin(), b.accR.begin() + n, 0.0f);
    // On SEND's root a group sums its lanes (no dry under them), and a lane that never met a
    // spatial processor adds nothing.
    const bool send = node.law == Law::send && ! sendRoot;
    bool anyWet = false;
    if (send)
    {
        // The dry under the sends (and what each send's difference is taken from), delayed to
        // the lanes: `wet` is free at this depth while a group runs.
        std::memcpy (b.wetL.data(), left, bytes);
        std::memcpy (b.wetR.data(), right, bytes);
        if (base >= 0)
            current->aligns[static_cast<std::size_t> (base) + node.lanes.size()].run (b.wetL.data(), b.wetR.data(), n, slowest);
    }
    for (std::size_t lane = 0; lane < node.lanes.size(); ++lane)
    {
        std::memcpy (b.workL.data(), b.inL.data(), bytes);
        std::memcpy (b.workR.data(), b.inR.data(), bytes);
        const bool laneWet = runChain (legacy, node.lanes[lane], b.workL.data(), b.workR.data(), n, depth + 1, group, wet);
        if (base >= 0)
            current->aligns[static_cast<std::size_t> (base) + lane].run (b.workL.data(), b.workR.data(), n, slowest - chainLatency (node.lanes[lane]));
        anyWet = anyWet || laneWet;
        if (sendRoot && ! laneWet)
            continue;
        const float g = node.gains[lane];
        if (send)
            for (int i = 0; i < n; ++i)
            {
                b.accL[static_cast<std::size_t> (i)] += g * (b.workL[static_cast<std::size_t> (i)] - b.wetL[static_cast<std::size_t> (i)]);
                b.accR[static_cast<std::size_t> (i)] += g * (b.workR[static_cast<std::size_t> (i)] - b.wetR[static_cast<std::size_t> (i)]);
            }
        else
            for (int i = 0; i < n; ++i)
            {
                b.accL[static_cast<std::size_t> (i)] += g * b.workL[static_cast<std::size_t> (i)];
                b.accR[static_cast<std::size_t> (i)] += g * b.workR[static_cast<std::size_t> (i)];
            }
    }
    if (send)
        for (int i = 0; i < n; ++i)
        {
            left[i] = b.wetL[static_cast<std::size_t> (i)] + b.accL[static_cast<std::size_t> (i)];
            right[i] = b.wetR[static_cast<std::size_t> (i)] + b.accR[static_cast<std::size_t> (i)];
        }
    else
        for (int i = 0; i < n; ++i)
        {
            left[i] = b.accL[static_cast<std::size_t> (i)];
            right[i] = b.accR[static_cast<std::size_t> (i)];
        }
    return anyWet;
}

} // namespace andor::analog::fx
