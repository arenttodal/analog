#include "FxGraph.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace andor::analog::fx
{

namespace
{
    constexpr const char* keys[typeCount] { "drive", "movement", "echo", "space", "spread", "comp", "eq", "filter", "phaser", "flanger", "shift", "conv", "utility" };
    constexpr const char* names[typeCount] { "DRIVE", "MOVEMENT", "ECHO", "SPACE", "STEREO SPREAD", "COMPRESSOR", "EQUALIZER", "FX FILTER",
                                             "PHASER", "FLANGER", "FREQ SHIFTER", "CONVOLUTION", "UTILITY" };
}

const char* typeKey (Type t) noexcept { return keys[static_cast<int> (t)]; }
const char* typeName (Type t) noexcept { return names[static_cast<int> (t)]; }

bool typeFromKey (const std::string& key, Type& t) noexcept
{
    for (int i = 0; i < typeCount; ++i)
        if (key == keys[i])
        {
            t = static_cast<Type> (i);
            return true;
        }
    return false;
}

std::string InstanceId::key() const { return std::string (typeKey (type)) + "." + std::to_string (slot + 1); }

bool InstanceId::fromKey (const std::string& key, InstanceId& id) noexcept
{
    const auto dot = key.rfind ('.');
    if (dot == std::string::npos || dot + 1 >= key.size())
        return false;
    Type t;
    if (! typeFromKey (key.substr (0, dot), t))
        return false;
    int n = 0;
    for (std::size_t i = dot + 1; i < key.size(); ++i)
    {
        if (key[i] < '0' || key[i] > '9')
            return false;
        n = 10 * n + (key[i] - '0');
        if (n > 99)
            return false;
    }
    if (n < 1 || n > capacity[static_cast<std::size_t> (t)])
        return false;
    id = { t, n - 1 };
    return true;
}

int InstanceId::flatIndex() const noexcept
{
    int n = 0;
    for (int i = 0; i < static_cast<int> (type); ++i)
        n += capacity[static_cast<std::size_t> (i)];
    return n + slot;
}

Node Node::parallel (std::vector<Node> a, std::vector<Node> b, Law law)
{
    Node n;
    n.kind = Kind::parallel;
    n.lanes = { std::move (a), std::move (b) };
    n.law = law;
    n.gains = law == Law::send ? std::vector<float> { 1.0f, 1.0f } : std::vector<float> { 0.5f, 0.5f };
    return n;
}

Node Node::splitter (SplitKind kind, std::vector<std::vector<Node>> lanes)
{
    Node n;
    n.kind = Kind::split;
    n.split = kind;
    n.lanes = std::move (lanes);
    n.gains.assign (n.lanes.size(), 1.0f);
    return n;
}

namespace
{
    bool sameNodes (const std::vector<Node>& a, const std::vector<Node>& b);

    bool sameNode (const Node& a, const Node& b)
    {
        if (a.kind != b.kind)
            return false;
        if (a.kind == Node::Kind::effect)
            return a.fx == b.fx && a.bypassed == b.bypassed;
        if (a.law != b.law || a.split != b.split || a.lowHz != b.lowHz || a.highHz != b.highHz || a.slope != b.slope || a.gains != b.gains
            || a.lanes.size() != b.lanes.size())
            return false;
        for (std::size_t i = 0; i < a.lanes.size(); ++i)
            if (! sameNodes (a.lanes[i], b.lanes[i]))
                return false;
        return true;
    }

    bool sameNodes (const std::vector<Node>& a, const std::vector<Node>& b)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (! sameNode (a[i], b[i]))
                return false;
        return true;
    }

    void collect (const std::vector<Node>& chain, std::vector<InstanceId>& out)
    {
        for (const auto& n : chain)
        {
            if (n.kind == Node::Kind::effect)
                out.push_back (n.fx);
            else
                for (const auto& lane : n.lanes)
                    collect (lane, out);
        }
    }

    std::string check (const std::vector<Node>& chain, int depth)
    {
        if (depth > maxDepth)
            return "Too many groups inside each other (at most " + std::to_string (maxDepth) + ")";
        for (const auto& n : chain)
        {
            if (n.kind == Node::Kind::effect)
            {
                if (n.fx.slot < 0 || n.fx.slot >= capacity[static_cast<std::size_t> (n.fx.type)])
                    return std::string ("No such ") + typeName (n.fx.type) + " (slot " + std::to_string (n.fx.slot + 1) + ")";
                continue;
            }
            const std::size_t want = n.kind == Node::Kind::parallel ? 2 : (n.split == SplitKind::bands3 ? 3 : 2);
            if (n.kind == Node::Kind::split && n.split == SplitKind::none)
                return "A split without a kind";
            if (n.lanes.size() != want || n.gains.size() != want)
                return n.kind == Node::Kind::parallel ? "A parallel group has two lanes" : "A splitter has the wrong number of lanes";
            for (const float gain : n.gains)
                if (! std::isfinite (gain) || gain < 0.0f || gain > 4.0f)
                    return "A lane's level is out of range";
            if (n.kind == Node::Kind::split)
            {
                if (n.slope != 12 && n.slope != 24)
                    return "A crossover slope is 12 or 24 dB";
                if (n.split == SplitKind::bands2 && (n.lowHz < 40.0f || n.lowHz > 12000.0f))
                    return "The crossover is out of range (40 Hz .. 12 kHz)";
                if (n.split == SplitKind::bands3 && (n.lowHz < 40.0f || n.highHz > 12000.0f || ! (n.lowHz < n.highHz)))
                    return "The crossovers are out of range or out of order";
            }
            for (const auto& lane : n.lanes)
                if (auto why = check (lane, depth + 1); ! why.empty())
                    return why;
        }
        return {};
    }

    void write (const std::vector<Node>& chain, std::ostringstream& out)
    {
        for (const auto& n : chain)
        {
            if (n.kind == Node::Kind::effect)
            {
                out << " fx " << n.fx.key() << (n.bypassed ? " off" : " on");
                continue;
            }
            if (n.kind == Node::Kind::parallel)
                out << " par " << (n.law == Law::send ? "send" : "linear");
            else
                out << " split " << (n.split == SplitKind::bands2 ? "bands2" : n.split == SplitKind::bands3 ? "bands3" : "ms") << " " << n.lowHz << " "
                    << n.highHz << " " << n.slope;
            out << " " << n.lanes.size();
            for (std::size_t i = 0; i < n.lanes.size(); ++i)
            {
                out << " lane " << n.gains[i] << " {";
                write (n.lanes[i], out);
                out << " }";
            }
        }
    }

    struct Reader
    {
        std::vector<std::string> words;
        std::size_t at = 0;
        bool next (std::string& w)
        {
            if (at >= words.size())
                return false;
            w = words[at++];
            return true;
        }
        bool number (float& v)
        {
            std::string w;
            if (! next (w))
                return false;
            try
            {
                std::size_t used = 0;
                v = std::stof (w, &used);
                return used == w.size() && std::isfinite (v);
            }
            catch (...)
            {
                return false;
            }
        }
        bool chain (std::vector<Node>& out, bool nested, int depth)
        {
            if (depth > maxDepth + 1)
                return false;
            std::string w;
            while (next (w))
            {
                if (w == "}")
                    return nested;
                Node n;
                if (w == "fx")
                {
                    std::string key, state;
                    if (! next (key) || ! next (state) || ! InstanceId::fromKey (key, n.fx) || (state != "on" && state != "off"))
                        return false;
                    n.bypassed = state == "off";
                }
                else if (w == "par" || w == "split")
                {
                    std::string kind;
                    if (! next (kind))
                        return false;
                    if (w == "par")
                    {
                        n.kind = Node::Kind::parallel;
                        if (kind != "send" && kind != "linear")
                            return false;
                        n.law = kind == "send" ? Law::send : Law::linear;
                    }
                    else
                    {
                        n.kind = Node::Kind::split;
                        n.split = kind == "bands2" ? SplitKind::bands2 : kind == "bands3" ? SplitKind::bands3 : kind == "ms" ? SplitKind::midSide : SplitKind::none;
                        float slope = 24.0f;
                        if (n.split == SplitKind::none || ! number (n.lowHz) || ! number (n.highHz) || ! number (slope))
                            return false;
                        n.slope = static_cast<int> (slope);
                    }
                    float count = 0.0f;
                    if (! number (count) || count < 1.0f || count > 3.0f)
                        return false;
                    for (int i = 0; i < static_cast<int> (count); ++i)
                    {
                        std::string lane, open;
                        float gain = 0.0f;
                        if (! next (lane) || lane != "lane" || ! number (gain) || ! next (open) || open != "{")
                            return false;
                        n.gains.push_back (gain);
                        n.lanes.emplace_back();
                        if (! chain (n.lanes.back(), true, depth + 1))
                            return false;
                    }
                }
                else
                    return false;
                out.push_back (std::move (n));
            }
            return ! nested;
        }
    };
}

bool Graph::operator== (const Graph& o) const { return custom == o.custom && sameNodes (chain, o.chain); }

Graph classicGraph()
{
    Graph g;
    g.chain = { Node::effect ({ Type::drive, 0 }), Node::effect ({ Type::movement, 0 }),
                Node::parallel ({ Node::effect ({ Type::echo, 0 }) }, { Node::effect ({ Type::space, 0 }) }, Law::send) };
    return g;
}

std::vector<InstanceId> instancesOf (const Graph& g)
{
    std::vector<InstanceId> out;
    collect (g.chain, out);
    return out;
}

bool contains (const Graph& g, InstanceId id)
{
    for (const auto& i : instancesOf (g))
        if (i == id)
            return true;
    return false;
}

std::string validate (const Graph& g)
{
    if (auto why = check (g.chain, 0); ! why.empty())
        return why;
    const auto all = instancesOf (g);
    if (static_cast<int> (all.size()) > maxEffects)
        return "At most " + std::to_string (maxEffects) + " effects";
    for (std::size_t i = 0; i < all.size(); ++i)
        for (std::size_t j = i + 1; j < all.size(); ++j)
            if (all[i] == all[j])
                return std::string (typeName (all[i].type)) + " is in the chain twice";
    return {};
}

std::string toText (const Graph& g)
{
    std::ostringstream out;
    out << "v" << Graph::schema << (g.custom ? " custom" : " classic");
    write (g.chain, out);
    return out.str();
}

bool fromText (const std::string& text, Graph& g)
{
    Reader r;
    std::istringstream in (text);
    for (std::string w; in >> w;)
        r.words.push_back (w);
    std::string version, mode;
    if (! r.next (version) || version != "v1" || ! r.next (mode) || (mode != "custom" && mode != "classic"))
        return false;
    Graph read;
    read.custom = mode == "custom";
    if (! r.chain (read.chain, false, 0) || ! validate (read).empty())
        return false;
    g = std::move (read);
    return true;
}

//==============================================================================
std::vector<Node>* chainAt (Graph& g, const ChainPath& path)
{
    auto* chain = &g.chain;
    for (const auto& [node, lane] : path)
    {
        if (node < 0 || node >= static_cast<int> (chain->size()))
            return nullptr;
        auto& n = (*chain)[static_cast<std::size_t> (node)];
        if (n.kind == Node::Kind::effect || lane < 0 || lane >= static_cast<int> (n.lanes.size()))
            return nullptr;
        chain = &n.lanes[static_cast<std::size_t> (lane)];
    }
    return chain;
}

namespace
{
    bool find (const std::vector<Node>& chain, InstanceId id, ChainPath& path, int& index)
    {
        for (int i = 0; i < static_cast<int> (chain.size()); ++i)
        {
            const auto& n = chain[static_cast<std::size_t> (i)];
            if (n.kind == Node::Kind::effect)
            {
                if (n.fx == id)
                {
                    index = i;
                    return true;
                }
                continue;
            }
            for (int lane = 0; lane < static_cast<int> (n.lanes.size()); ++lane)
            {
                path.push_back ({ i, lane });
                if (find (n.lanes[static_cast<std::size_t> (lane)], id, path, index))
                    return true;
                path.pop_back();
            }
        }
        return false;
    }

    /** Groups with an empty lane dissolve into their other lane (a parallel pair), and
        splitters keep their lanes (an empty band passes its band). */
    void tidy (std::vector<Node>& chain)
    {
        for (std::size_t i = 0; i < chain.size();)
        {
            auto& n = chain[i];
            if (n.kind == Node::Kind::effect)
            {
                ++i;
                continue;
            }
            for (auto& lane : n.lanes)
                tidy (lane);
            if (n.kind == Node::Kind::parallel)
            {
                const bool emptyA = n.lanes[0].empty(), emptyB = n.lanes.size() < 2 || n.lanes[1].empty();
                if (emptyA || emptyB)
                {
                    auto keep = emptyA ? (n.lanes.size() > 1 ? n.lanes[1] : std::vector<Node>()) : n.lanes[0];
                    chain.erase (chain.begin() + static_cast<std::ptrdiff_t> (i));
                    chain.insert (chain.begin() + static_cast<std::ptrdiff_t> (i), keep.begin(), keep.end());
                    i += keep.size();
                    continue;
                }
            }
            ++i;
        }
    }

    bool commit (Graph& g, Graph next)
    {
        tidy (next.chain);
        next.custom = true;
        if (! validate (next).empty())
            return false;
        g = std::move (next);
        return true;
    }
}

bool locate (const Graph& g, InstanceId id, ChainPath& path, int& index)
{
    path.clear();
    return find (g.chain, id, path, index);
}

bool removeEffect (Graph& g, InstanceId id)
{
    Graph next = g;
    ChainPath path;
    int index = 0;
    if (! locate (next, id, path, index))
        return false;
    auto* chain = chainAt (next, path);
    chain->erase (chain->begin() + index);
    return commit (g, std::move (next));
}

bool insertEffect (Graph& g, const ChainPath& path, int index, Node node)
{
    Graph next = g;
    auto* chain = chainAt (next, path);
    if (chain == nullptr || index < 0 || index > static_cast<int> (chain->size()))
        return false;
    chain->insert (chain->begin() + index, std::move (node));
    return commit (g, std::move (next));
}

bool moveEffect (Graph& g, InstanceId id, const ChainPath& path, int index)
{
    Graph next = g;
    ChainPath from;
    int at = 0;
    if (! locate (next, id, from, at) || chainAt (next, path) == nullptr)
        return false;
    // Take it out first, then put it where the gap was (counted before the move): the target's
    // path or index shifts when the removal was before it in the same chain or an outer one.
    auto* source = chainAt (next, from);
    Node node = (*source)[static_cast<std::size_t> (at)];
    source->erase (source->begin() + at);
    auto target = path;
    int place = index;
    if (from == target)
    {
        if (place > at)
            --place;
    }
    else if (from.size() < target.size() && std::equal (from.begin(), from.end(), target.begin()) && target[from.size()].first > at)
        --target[from.size()].first;
    auto* chain = chainAt (next, target);
    if (chain == nullptr || place < 0 || place > static_cast<int> (chain->size()))
        return false;
    chain->insert (chain->begin() + place, std::move (node));
    return commit (g, std::move (next));
}

bool stackEffect (Graph& g, InstanceId id, InstanceId targetId)
{
    if (id == targetId)
        return false;
    Graph next = g;
    ChainPath from, to;
    int at = 0, target = 0;
    if (! locate (next, id, from, at) || ! locate (next, targetId, to, target))
        return false;
    Node node = (*chainAt (next, from))[static_cast<std::size_t> (at)];
    // Take it out (marking where the target is afterwards), then stack.
    chainAt (next, from)->erase (chainAt (next, from)->begin() + at);
    if (! locate (next, targetId, to, target))
        return false;
    auto* chain = chainAt (next, to);
    if (! to.empty())
    {
        // Inside a pair already: after the target, in its lane.
        chain->insert (chain->begin() + target + 1, node);
        return commit (g, std::move (next));
    }
    auto pair = Node::parallel ({ (*chain)[static_cast<std::size_t> (target)] }, { node }, Law::linear);
    (*chain)[static_cast<std::size_t> (target)] = pair;
    return commit (g, std::move (next));
}

bool nudgeEffect (Graph& g, InstanceId id, int delta)
{
    Graph next = g;
    ChainPath path;
    int at = 0;
    if (! locate (next, id, path, at))
        return false;
    auto* chain = chainAt (next, path);
    const int to = at + (delta < 0 ? -1 : 1);
    if (to < 0 || to >= static_cast<int> (chain->size()))
        return false;
    std::swap ((*chain)[static_cast<std::size_t> (at)], (*chain)[static_cast<std::size_t> (to)]);
    return commit (g, std::move (next));
}

bool unstackEffect (Graph& g, InstanceId id)
{
    Graph next = g;
    ChainPath path;
    int at = 0;
    if (! locate (next, id, path, at) || path.empty())
        return false;
    Node node = (*chainAt (next, path))[static_cast<std::size_t> (at)];
    auto groupPath = path;
    const int groupIndex = groupPath.back().first;
    groupPath.pop_back();
    chainAt (next, path)->erase (chainAt (next, path)->begin() + at);
    auto* outer = chainAt (next, groupPath);
    outer->insert (outer->begin() + groupIndex + 1, node);
    return commit (g, std::move (next));
}

bool setBypassed (Graph& g, InstanceId id, bool bypassed)
{
    Graph next = g;
    ChainPath path;
    int at = 0;
    if (! locate (next, id, path, at))
        return false;
    (*chainAt (next, path))[static_cast<std::size_t> (at)].bypassed = bypassed;
    return commit (g, std::move (next));
}

} // namespace andor::analog::fx
