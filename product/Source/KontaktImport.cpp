#include "KontaktImport.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

namespace andor::analog::import
{

namespace
{
    struct Truncated : std::runtime_error
    {
        Truncated() : std::runtime_error ("the file ends early") {}
    };

    /** Little-endian reads over a byte range; out of range throws (caught as "unreadable"). */
    class Reader
    {
    public:
        Reader (const juce::uint8* d, size_t n) : data (d), size (n) {}
        explicit Reader (const juce::MemoryBlock& m) : data (static_cast<const juce::uint8*> (m.getData())), size (m.getSize()) {}

        size_t remaining() const noexcept { return size - at; }
        size_t position() const noexcept { return at; }
        bool atEnd() const noexcept { return at >= size; }
        const juce::uint8* here() const noexcept { return data + at; }

        void skip (size_t n)
        {
            need (n);
            at += n;
        }
        juce::uint8 u8()
        {
            need (1);
            return data[at++];
        }
        juce::uint16 u16()
        {
            need (2);
            const auto v = static_cast<juce::uint16> (data[at] | (data[at + 1] << 8));
            at += 2;
            return v;
        }
        juce::uint32 u32()
        {
            need (4);
            const auto v = static_cast<juce::uint32> (data[at]) | (static_cast<juce::uint32> (data[at + 1]) << 8) | (static_cast<juce::uint32> (data[at + 2]) << 16)
                           | (static_cast<juce::uint32> (data[at + 3]) << 24);
            at += 4;
            return v;
        }
        juce::int32 s32() { return static_cast<juce::int32> (u32()); }
        juce::uint64 u64()
        {
            const auto lo = static_cast<juce::uint64> (u32());
            return lo | (static_cast<juce::uint64> (u32()) << 32);
        }
        float f32()
        {
            const auto bits = u32();
            float f;
            std::memcpy (&f, &bits, 4);
            return f;
        }
        juce::String utf16()
        {
            const auto n = u32();
            need (static_cast<size_t> (n) * 2);
            juce::String s;
            for (juce::uint32 i = 0; i < n; ++i)
            {
                const auto c = static_cast<juce::juce_wchar> (data[at] | (data[at + 1] << 8));
                at += 2;
                s << juce::String::charToString (c);
            }
            return s;
        }
        Reader sub (size_t n)
        {
            need (n);
            Reader r (data + at, n);
            at += n;
            return r;
        }

    private:
        void need (size_t n) const
        {
            if (n > size - at)
                throw Truncated();
        }
        const juce::uint8* data;
        size_t size;
        size_t at = 0;
    };

    /** FastLZ (levels 1 and 2), as NI compresses a container's sub-tree. */
    juce::MemoryBlock fastlz (const juce::uint8* in, size_t inSize, size_t outSize)
    {
        juce::MemoryBlock out (outSize, true);
        auto* o = static_cast<juce::uint8*> (out.getData());
        if (inSize == 0)
            throw std::runtime_error ("empty compressed block");
        const int level = (in[0] >> 5) + 1;
        size_t ip = 0, op = 0;
        auto next = [&]() -> juce::uint32 {
            if (ip >= inSize)
                throw Truncated();
            return in[ip++];
        };
        juce::uint32 ctrl = next() & 31u;
        for (;;)
        {
            if (ctrl >= 32)
            {
                juce::uint32 len = (ctrl >> 5) - 1;
                juce::uint32 ofs = (ctrl & 31u) << 8;
                if (len == 6)
                {
                    if (level == 1)
                        len += next();
                    else
                        for (juce::uint32 c = 255; c == 255;)
                        {
                            c = next();
                            len += c;
                        }
                }
                juce::int64 ref = static_cast<juce::int64> (op) - ofs;
                const auto code = next();
                ref -= code;
                if (level == 2 && code == 255 && ofs == (31u << 8))
                {
                    ofs = next() << 8;
                    ofs += next();
                    ref = static_cast<juce::int64> (op) - ofs - 8191;
                }
                --ref;
                if (ref < 0 || op + len + 3 > outSize)
                    throw std::runtime_error ("corrupt compressed block");
                for (juce::uint32 k = 0; k < len + 3; ++k)
                    o[op++] = o[static_cast<size_t> (ref++)];
            }
            else
            {
                const auto n = static_cast<size_t> (ctrl) + 1;
                if (ip + n > inSize || op + n > outSize)
                    throw std::runtime_error ("corrupt compressed block");
                std::memcpy (o + op, in + ip, n);
                ip += n;
                op += n;
            }
            if (ip >= inSize)
                break;
            ctrl = next();
        }
        if (op != outSize)
            throw std::runtime_error ("compressed block has the wrong size");
        return out;
    }

    //==========================================================================
    // The NI container: items, data chunks; we look for the preset chunk (109).

    struct ContainerScan
    {
        juce::MemoryBlock preset;
        bool encrypted = false;
    };

    void readItem (Reader& r, ContainerScan& scan, int depth);

    void readDataChunk (Reader& r, ContainerScan& scan, int depth)
    {
        const auto size = r.u64();
        if (size < 8)
            throw std::runtime_error ("bad chunk size");
        auto b = r.sub (static_cast<size_t> (size - 8));
        b.skip (4);   // domain
        const auto type = b.u32();
        b.u32();      // version
        if (type != 1)
            readDataChunk (b, scan, depth);
        if (type == 109)
        {
            b.u32();
            b.u32();
            b.u32();   // items (one)
            const auto n = b.u32();
            b.u32();
            auto p = b.sub (n);
            scan.preset = juce::MemoryBlock (p.here(), p.remaining());
        }
        else if (type == 115)
        {
            b.u32();
            if (b.u8() > 0)
            {
                const auto plain = b.u32();
                const auto packed = b.u32();
                auto c = b.sub (packed);
                try
                {
                    const auto tree = fastlz (c.here(), c.remaining(), plain);
                    Reader t (tree);
                    readItem (t, scan, depth + 1);
                }
                catch (const std::exception&)
                {
                    scan.encrypted = true;   // (an encrypted sub-tree does not decompress)
                }
            }
            else
                readItem (b, scan, depth + 1);
        }
    }

    void readItem (Reader& r, ContainerScan& scan, int depth)
    {
        if (depth > 16)
            throw std::runtime_error ("nested too deep");
        const auto size = r.u64();
        if (size < 8)
            throw std::runtime_error ("bad item size");
        auto b = r.sub (static_cast<size_t> (size - 8));
        if (b.u32() != 1)
            throw std::runtime_error ("unknown item version");
        if (b.remaining() < 4 || std::memcmp (b.here(), "hsin", 4) != 0)
            throw std::runtime_error ("not an NI container");
        b.skip (4);
        b.u32();
        b.u32();
        b.skip (16);
        readDataChunk (b, scan, depth);
        b.u32();
        const auto children = b.u32();
        for (juce::uint32 i = 0; i < children; ++i)
        {
            b.u32();
            b.skip (4);
            b.u32();
            readItem (b, scan, depth + 1);
        }
    }

    //==========================================================================
    // Kontakt's preset chunks.

    constexpr int idGroup = 0x04, idScript = 0x06, idProgram = 0x28, idGroupList = 0x33, idZoneList = 0x34, idLoops = 0x39, idFiles = 0x3D, idFilesEx = 0x4B;

    struct Chunk
    {
        int id = -1, version = -1;
        juce::MemoryBlock pub;
        std::vector<Chunk> kids;
    };

    bool structured (int id) { return id == 0x03 || id == 0x29 || id == 0x28 || id == 0x06 || id == 0x17 || id == 0x32 || id == 0x3A || id == 0x45 || id == 0x47 || id == 0x4E; }

    Chunk readChunk (Reader& r, int depth);

    void readStructure (Reader& o, Chunk& c, size_t size, int depth)
    {
        if (o.u8() == 0)
        {
            const auto n = size > 0 ? std::min (size - 1, o.remaining()) : 0;
            auto p = o.sub (n);
            c.pub = juce::MemoryBlock (p.here(), p.remaining());
            return;
        }
        c.version = o.u16();
        o.skip (o.u32());   // private data
        auto p = o.sub (o.u32());
        c.pub = juce::MemoryBlock (p.here(), p.remaining());
        auto kids = o.sub (o.u32());
        while (! kids.atEnd())
            c.kids.push_back (readChunk (kids, depth + 1));
    }

    Chunk readChunk (Reader& r, int depth)
    {
        if (depth > 32)
            throw std::runtime_error ("nested too deep");
        Chunk c;
        c.id = r.u16();
        const auto size = r.u32();
        auto o = r.sub (size);
        if (c.id == idGroupList || c.id == idZoneList)
        {
            const auto n = o.u32();
            for (juce::uint32 i = 0; i < n; ++i)
            {
                Chunk k;
                k.id = c.id == idZoneList ? static_cast<int> (o.u32()) : idGroup;
                readStructure (o, k, 0, depth + 1);
                c.kids.push_back (std::move (k));
            }
        }
        else if (structured (c.id))
            readStructure (o, c, size, depth);
        else
            c.pub = juce::MemoryBlock (o.here(), o.remaining());
        return c;
    }

    juce::String readPath (Reader& b)
    {
        juce::String path;
        const auto segments = b.u32();
        for (juce::uint32 s = 0; s < segments; ++s)
        {
            const auto type = b.u8();
            switch (type)
            {
                case 0: { b.skip (2); path << "/"; break; }
                case 1: { const auto drive = b.utf16(); path << (drive.isEmpty() ? juce::String ("/") : drive + ":/"); break; }
                case 2: path << b.utf16() << "/"; break;
                case 3: path << "../"; break;
                case 4: case 8: case 9: path << b.utf16(); break;
                case 6: break;
                default: throw std::runtime_error ("unknown path segment");
            }
        }
        return path;
    }

    juce::StringArray readFiles (const Chunk& c)
    {
        Reader b (c.pub);
        juce::StringArray samples;
        if (c.id == idFilesEx)
        {
            if (b.u16() == 3)
            {
                const auto n = b.s32();
                b.skip (8);
                for (int i = 0; i < n - 1; ++i)
                {
                    samples.add (readPath (b));
                    if (! b.atEnd())
                        b.skip (32);
                }
                return samples;
            }
        }
        const auto special = b.s32();
        for (int i = 0; i < special; ++i)
            readPath (b);
        if (! b.atEnd())
        {
            const auto n = b.s32();
            for (int i = 0; i < n; ++i)
                samples.add (readPath (b));
        }
        return samples;
    }

    double cents (double ratio) { return ratio > 0.0 ? std::round (1200.0 * std::log2 (ratio) * 100.0) / 100.0 : 0.0; }

    juce::var zoneFrom (const Chunk& c, const juce::StringArray& files)
    {
        Reader b (c.pub);
        auto* z = new juce::DynamicObject();
        juce::var zone (z);
        const auto start = b.u32();
        const auto end = b.u32();
        b.u32();
        z->setProperty ("velLo", b.u16());
        z->setProperty ("velHi", b.u16());
        z->setProperty ("keyLo", b.u16());
        z->setProperty ("keyHi", b.u16());
        b.skip (8);   // fades
        z->setProperty ("root", b.u16());
        z->setProperty ("volumeLinear", b.f32());
        z->setProperty ("pan", b.f32());
        z->setProperty ("tuneCents", cents (b.f32()));
        z->setProperty ("sampleStart", static_cast<int> (start));
        z->setProperty ("sampleEnd", static_cast<int> (end));
        if (c.version >= 0x9A)
        {
            b.skip (6);
            if (b.atEnd())
                return zone;
        }
        const auto fileIndex = static_cast<int> (b.u32());
        z->setProperty ("fileIndex", fileIndex);
        z->setProperty ("file", juce::isPositiveAndBelow (fileIndex, files.size()) ? files[fileIndex].fromLastOccurrenceOf ("/", false, false) : juce::String());
        b.u32();   // resolution
        z->setProperty ("rate", static_cast<int> (b.u32()));
        z->setProperty ("channels", b.u8());
        z->setProperty ("frames", static_cast<int> (b.u32()));
        juce::Array<juce::var> loops;
        for (const auto& k : c.kids)
            if (k.id == idLoops && k.pub.getSize() >= 2)
            {
                Reader l (k.pub);
                const auto enabled = l.u16();
                for (int i = 0; i < 8; ++i)
                    if ((enabled & (1 << i)) != 0)
                    {
                        if (l.u16() != 0x60)
                            throw std::runtime_error ("unknown loop record");
                        auto* loop = new juce::DynamicObject();
                        loop->setProperty ("mode", l.s32());
                        loop->setProperty ("start", static_cast<int> (l.u32()));
                        loop->setProperty ("length", static_cast<int> (l.u32()));
                        loop->setProperty ("count", static_cast<int> (l.u32()));
                        loop->setProperty ("alternating", l.u8() > 0);
                        loop->setProperty ("tuning", l.f32());
                        loop->setProperty ("xfade", static_cast<int> (l.u32()));
                        if (! l.atEnd())
                            l.skip (1);
                        loops.add (juce::var (loop));
                    }
            }
        z->setProperty ("loops", loops);
        return zone;
    }

    /** The first script: its source and its saved persistent values ("$name value"). */
    void readScript (const Chunk& c, juce::String& source, juce::NamedValueSet& saved)
    {
        Reader b (c.pub);
        b.skip (2);
        auto text = b.sub (b.u32());
        source = juce::String::fromUTF8 (reinterpret_cast<const char*> (text.here()), static_cast<int> (text.remaining()));
        if (b.remaining() < 19)
            return;
        b.skip (15);
        const auto n = b.u32();
        for (juce::uint32 i = 0; i < n; ++i)
        {
            auto e = b.sub (b.u32());
            const auto entry = juce::String::fromUTF8 (reinterpret_cast<const char*> (e.here()), static_cast<int> (e.remaining()));
            saved.set (juce::Identifier (entry.upToFirstOccurrenceOf (" ", false, false).trimCharactersAtStart ("$%")), entry.fromFirstOccurrenceOf (" ", false, false));
        }
    }

    /** `declare const $NAME := value` in a script, or -1. */
    int scriptConstant (const juce::String& source, const juce::String& name)
    {
        for (const auto& line : juce::StringArray::fromLines (source))
        {
            const auto t = line.trim();
            if (t.startsWith ("declare const $" + name) && t.containsChar (':'))
            {
                const auto rest = t.fromFirstOccurrenceOf ("$" + name, false, false).trim();
                if (rest.startsWith (":="))
                    return rest.substring (2).trim().getIntValue();
            }
        }
        return -1;
    }
}

NkiReadResult readNki (const juce::File& nki)
{
    NkiReadResult result;
    juce::MemoryBlock data;
    if (! nki.loadFileAsData (data) || data.getSize() < 16)
    {
        result.error = "Could not read the file";
        return result;
    }
    try
    {
        ContainerScan scan;
        Reader top (data);
        readItem (top, scan, 0);
        if (scan.preset.getSize() == 0)
        {
            result.error = scan.encrypted ? "The instrument is encrypted (a commercial library): its zones cannot be read"
                                          : "No Kontakt instrument was found in the file";
            return result;
        }
        std::vector<Chunk> tops;
        Reader p (scan.preset);
        while (! p.atEnd())
            tops.push_back (readChunk (p, 0));
        const Chunk* program = nullptr;
        juce::StringArray files;
        for (const auto& c : tops)
        {
            if (c.id == idProgram && program == nullptr)
                program = &c;
            if ((c.id == idFiles || c.id == idFilesEx) && files.isEmpty())
                files = readFiles (c);
        }
        if (program == nullptr)
        {
            result.error = "The file holds no Kontakt program (a multi or a bank is not supported)";
            return result;
        }
        Reader pd (program->pub);
        const auto programName = pd.utf16();
        pd.skip (9);
        const auto programVolume = pd.f32();

        juce::String scriptSource;
        juce::NamedValueSet saved;
        for (const auto& k : program->kids)
            if (k.id == idScript && k.pub.getSize() > 6)
            {
                readScript (k, scriptSource, saved);
                break;
            }
        // Lanes: an Evenant Engine's script declares where each lane's groups start.
        struct Lane
        {
            const char* name;
            int start;
        };
        std::vector<Lane> lanes;
        const int sets = std::max (1, scriptConstant (scriptSource, "NUM_SETS"));
        for (const char* l : { "A", "C", "B", "D" })
            if (const int s = scriptConstant (scriptSource, juce::String (l) + "_START"); s >= 0)
                lanes.push_back ({ l, s });

        auto* m = new juce::DynamicObject();
        juce::var manifest (m);
        m->setProperty ("schemaVersion", 1);
        auto* source = new juce::DynamicObject();
        source->setProperty ("nki", nki.getFileName());
        source->setProperty ("program", programName);
        source->setProperty ("programVolumeLinear", programVolume);
        source->setProperty ("sampleFolder", files.isEmpty() ? juce::String() : files[0].upToFirstOccurrenceOf ("/", false, false));
        m->setProperty ("source", juce::var (source));
        auto* savedState = new juce::DynamicObject();
        for (const auto& v : saved)
            savedState->setProperty (v.name, v.value);
        m->setProperty ("savedState", juce::var (savedState));
        auto* laneObj = new juce::DynamicObject();
        for (const auto& l : lanes)
        {
            auto* e = new juce::DynamicObject();
            const int slot = saved.getWithDefault (juce::Identifier (juce::String ("ENGINE_") + l.name + "_SELECT"), 0).toString().getIntValue();
            e->setProperty ("firstGroup", l.start);
            e->setProperty ("selectedSlot", slot);
            e->setProperty ("selectedGroup", l.start + slot);
            laneObj->setProperty (l.name, juce::var (e));
        }
        m->setProperty ("lanes", juce::var (laneObj));

        // Groups (their list's order) and the zones each owns.
        const Chunk* groupList = nullptr;
        const Chunk* zoneList = nullptr;
        for (const auto& k : program->kids)
        {
            if (k.id == idGroupList)
                groupList = &k;
            if (k.id == idZoneList)
                zoneList = &k;
        }
        juce::Array<juce::var> groups;
        if (groupList != nullptr)
            for (int gi = 0; gi < static_cast<int> (groupList->kids.size()); ++gi)
            {
                Reader g (groupList->kids[static_cast<size_t> (gi)].pub);
                auto* group = new juce::DynamicObject();
                group->setProperty ("index", gi);
                group->setProperty ("name", g.utf16());
                const auto volume = g.f32();
                group->setProperty ("volumeLinear", volume);
                group->setProperty ("volumeDb", volume > 0.0f ? 20.0 * std::log10 (volume) : -100.0);
                group->setProperty ("pan", g.f32());
                group->setProperty ("tuneRatio", g.f32());
                juce::String lane;
                int slot = -1;
                for (const auto& l : lanes)
                    if (gi >= l.start && gi < l.start + sets)
                    {
                        lane = l.name;
                        slot = gi - l.start;
                    }
                group->setProperty ("lane", lane);
                group->setProperty ("slot", slot);
                juce::Array<juce::var> zones;
                if (zoneList != nullptr)
                    for (const auto& z : zoneList->kids)
                        if (z.id == gi)
                            zones.add (zoneFrom (z, files));
                group->setProperty ("zones", zones);
                groups.add (juce::var (group));
            }
        m->setProperty ("groups", groups);
        result.manifest = manifest;
    }
    catch (const std::exception& e)
    {
        result.manifest = juce::var();
        result.error = juce::String ("The file could not be read (") + e.what() + ")";
    }
    return result;
}

juce::File expectedSampleFolder (const juce::File& nki, const juce::var& manifest)
{
    const auto name = manifest.getProperty ("source", {}).getProperty ("sampleFolder", {}).toString();
    if (name.isEmpty())
        return {};
    const auto folder = nki.getParentDirectory().getChildFile (name);
    return folder.isDirectory() ? folder : juce::File();
}

} // namespace andor::analog::import
