#include "FxEngine.h"

#include <algorithm>
#include <cstring>

namespace andor::analog::fx
{

std::shared_ptr<const CompiledGraph> compile (const Graph& g, std::uint64_t generation)
{
    if (! validate (g).empty())
        return {};
    for (const auto& id : instancesOf (g))
        if (! implemented (id.type))
            return {};
    auto c = std::make_shared<CompiledGraph>();
    c->generation = generation;
    c->graph = g;
    c->latency = 0;   // (every processor so far adds none; see FxProcessor::latency)
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
            p = makeProcessor (id.type);
            if (p != nullptr)
            {
                p->prepare (rate, maxBlock);
                p->set (settings, s);
            }
        }
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
    for (int t = 4; t < typeCount; ++t)
        for (int slot = 0; slot < capacity[static_cast<std::size_t> (t)]; ++slot)
            if (auto& p = processors[static_cast<std::size_t> (InstanceId { static_cast<Type> (t), slot }.flatIndex())]; p != nullptr)
                p->set (settings, slot);
}

const Processor* FxEngine::processor (InstanceId id) const noexcept
{
    return processors[static_cast<std::size_t> (id.flatIndex())].get();
}

void FxEngine::process (osp::PostProcessor& legacy, float* left, float* right, int numSamples) noexcept
{
    if (const auto* next = exchange.takePending(); next != nullptr && next != current)
    {
        if (current == nullptr && (! next->graph.custom || ! played))
            current = next;   // (the first route before any sound, or CLASSIC: nothing to duck)
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
        if (custom)
            runCustom (legacy, current->graph, l, r, run);
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
            }
        }
        done += run;
    }
    if (current != nullptr)
        exchange.publishOldestInUse (waiting != nullptr ? std::min (current->generation, waiting->generation) : current->generation);
    customNow.store (current != nullptr && current->graph.custom, std::memory_order_relaxed);
    latencyNow.store (current != nullptr && current->graph.custom ? current->latency : 0, std::memory_order_relaxed);
}

void FxEngine::runCustom (osp::PostProcessor& legacy, const Graph& g, float* left, float* right, int n) noexcept
{
    legacy.customReimagined (left, right, n);   // (locked upstream: before every routed effect)
    runChain (legacy, g.chain, left, right, n, 0);
}

void FxEngine::runChain (osp::PostProcessor& legacy, const std::vector<Node>& chain, float* left, float* right, int n, int depth) noexcept
{
    for (const auto& node : chain)
    {
        if (node.kind == Node::Kind::effect)
            runEffect (legacy, node, left, right, n, depth);
        else
            runGroup (legacy, node, left, right, n, depth);
    }
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

    // Inserts: y = x + a (f (x) - x); not run while fully bypassed.
    if (a <= 0.0f && ! gliding)
        return;
    auto* p = processors[index].get();
    const bool tail = p != nullptr && p->hasTail();
    if (! gliding && a >= 1.0f)
    {
        if (id.type == Type::drive)
            legacy.customDrive (left, right, n);
        else if (id.type == Type::movement)
            legacy.customMovement (left, right, n);
        else if (p != nullptr)
            p->process (left, right, n);
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
    for (int i = 0; i < n; ++i)
    {
        const float g = gliding ? b.dry[static_cast<std::size_t> (i)] : a;
        if (tail)
        {
            left[i] = (1.0f - g) * left[i] + b.workL[static_cast<std::size_t> (i)];
            right[i] = (1.0f - g) * right[i] + b.workR[static_cast<std::size_t> (i)];
        }
        else
        {
            left[i] += g * (b.workL[static_cast<std::size_t> (i)] - left[i]);
            right[i] += g * (b.workR[static_cast<std::size_t> (i)] - right[i]);
        }
    }
}

void FxEngine::runGroup (osp::PostProcessor& legacy, const Node& node, float* left, float* right, int n, int depth) noexcept
{
    auto& b = buffers[static_cast<std::size_t> (depth)];
    const auto bytes = sizeof (float) * static_cast<std::size_t> (n);
    std::memcpy (b.inL.data(), left, bytes);
    std::memcpy (b.inR.data(), right, bytes);
    std::fill (b.accL.begin(), b.accL.begin() + n, 0.0f);
    std::fill (b.accR.begin(), b.accR.begin() + n, 0.0f);
    for (std::size_t lane = 0; lane < node.lanes.size(); ++lane)
    {
        std::memcpy (b.workL.data(), b.inL.data(), bytes);
        std::memcpy (b.workR.data(), b.inR.data(), bytes);
        runChain (legacy, node.lanes[lane], b.workL.data(), b.workR.data(), n, depth + 1);
        const float g = node.gains[lane];
        if (node.law == Law::send)
            for (int i = 0; i < n; ++i)
            {
                b.accL[static_cast<std::size_t> (i)] += g * (b.workL[static_cast<std::size_t> (i)] - b.inL[static_cast<std::size_t> (i)]);
                b.accR[static_cast<std::size_t> (i)] += g * (b.workR[static_cast<std::size_t> (i)] - b.inR[static_cast<std::size_t> (i)]);
            }
        else
            for (int i = 0; i < n; ++i)
            {
                b.accL[static_cast<std::size_t> (i)] += g * b.workL[static_cast<std::size_t> (i)];
                b.accR[static_cast<std::size_t> (i)] += g * b.workR[static_cast<std::size_t> (i)];
            }
    }
    if (node.law == Law::send)
        for (int i = 0; i < n; ++i)
        {
            left[i] = b.inL[static_cast<std::size_t> (i)] + b.accL[static_cast<std::size_t> (i)];
            right[i] = b.inR[static_cast<std::size_t> (i)] + b.accR[static_cast<std::size_t> (i)];
        }
    else
        for (int i = 0; i < n; ++i)
        {
            left[i] = b.accL[static_cast<std::size_t> (i)];
            right[i] = b.accR[static_cast<std::size_t> (i)];
        }
}

} // namespace andor::analog::fx
