// analog-render: ANDOR ANALOG's engine offline, for listening tests and plots.
//
//   analog-render --list
//   analog-render --patch "Warm Poly" --out warm.wav [--notes 48,55,60,64] [--seconds 4]
//                 [--hold 2.5] [--rate 48000] [--oversampling 4] [--seed 1] [--analog 0.5]
//   analog-render --all <folder>          every factory sound's demo phrase
//   analog-render --benchmark [--voices 16] [--seconds 10]

#include "FactoryPatches.h"
#include "Synth.h"
#include "Wav.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

using namespace andor::analog;

namespace
{
    struct Options
    {
        std::string patch = "Init", out = "analog.wav", all;
        std::vector<int> notes;
        double seconds = 4.0, hold = 2.5, rate = 48000.0;
        int oversampling = 4, voices = 16;
        long seed = -1;
        double analog = -1.0;
        bool list = false, benchmark = false;
    };

    std::vector<int> parseNotes (const std::string& text)
    {
        std::vector<int> notes;
        std::stringstream in (text);
        std::string item;
        while (std::getline (in, item, ','))
            notes.push_back (std::atoi (item.c_str()));
        return notes;
    }

    /** Renders the notes as a chord (held for `hold` seconds) or as a line of eighth notes at 120 BPM. */
    void renderPhrase (Synth& synth, const std::vector<int>& notes, bool line, double seconds, double hold, std::vector<float>& left, std::vector<float>& right)
    {
        const double rate = synth.sampleRate();
        const auto total = static_cast<std::size_t> (seconds * rate);
        left.assign (total, 0.0f);
        right.assign (total, 0.0f);
        struct Ev
        {
            std::size_t at;
            std::uint8_t bytes[3];
        };
        std::vector<Ev> events;
        for (std::size_t i = 0; i < notes.size(); ++i)
        {
            const double start = line ? 0.25 * static_cast<double> (i) : 0.0;
            const double stop = line ? start + 0.2 : hold;
            events.push_back ({ static_cast<std::size_t> (start * rate), { 0x90, static_cast<std::uint8_t> (notes[i]), 100 } });
            events.push_back ({ static_cast<std::size_t> (stop * rate), { 0x80, static_cast<std::uint8_t> (notes[i]), 0 } });
        }
        std::stable_sort (events.begin(), events.end(), [] (const Ev& a, const Ev& b) { return a.at < b.at; });
        constexpr std::size_t block = 512;
        std::size_t next = 0;
        for (std::size_t pos = 0; pos < total;)
        {
            while (next < events.size() && events[next].at <= pos)
                synth.handleMidi (events[next++].bytes, 3);
            std::size_t until = std::min (total, pos + block);
            if (next < events.size() && events[next].at < until)
                until = events[next].at;
            synth.render (left.data() + pos, right.data() + pos, static_cast<int> (until - pos));
            pos = until;
        }
    }

    const FactoryPatch* find (const std::string& name)
    {
        for (const auto& f : factoryPatches())
            if (f.name == name)
                return &f;
        return nullptr;
    }

    std::string fileName (std::string name)
    {
        for (auto& c : name)
            if (c == ' ')
                c = '-';
        return name + ".wav";
    }

    int benchmark (const Options& o)
    {
        Synth synth;
        synth.prepare (o.rate, 512, o.oversampling);
        auto patch = find ("Warm Poly")->patch;
        patch.polyphony = 16;
        synth.setPatch (patch);
        synth.reset();
        for (int v = 0; v < o.voices; ++v)
        {
            const std::uint8_t on[3] { 0x90, static_cast<std::uint8_t> (36 + 3 * v), 100 };
            synth.handleMidi (on, 3);
        }
        std::vector<float> l (512), r (512);
        const auto blocks = static_cast<int> (o.seconds * o.rate / 512.0);
        const auto start = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b)
            synth.render (l.data(), r.data(), 512);
        const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        std::printf ("%d voices, %.0f Hz, %dx oversampling: %.3f s for %.1f s of audio (%.1f %% of one core, %.2f %% per voice)\n", o.voices, o.rate,
                     o.oversampling, elapsed, o.seconds, 100.0 * elapsed / o.seconds, 100.0 * elapsed / o.seconds / std::max (1, o.voices));
        return 0;
    }
}

int main (int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto value = [&] { return i + 1 < argc ? std::string (argv[++i]) : std::string(); };
        if (a == "--list") o.list = true;
        else if (a == "--patch") o.patch = value();
        else if (a == "--out") o.out = value();
        else if (a == "--all") o.all = value();
        else if (a == "--notes") o.notes = parseNotes (value());
        else if (a == "--seconds") o.seconds = std::atof (value().c_str());
        else if (a == "--hold") o.hold = std::atof (value().c_str());
        else if (a == "--rate") o.rate = std::atof (value().c_str());
        else if (a == "--oversampling") o.oversampling = std::atoi (value().c_str());
        else if (a == "--seed") o.seed = std::atol (value().c_str());
        else if (a == "--analog") o.analog = std::atof (value().c_str());
        else if (a == "--voices") o.voices = std::atoi (value().c_str());
        else if (a == "--benchmark") o.benchmark = true;
        else
        {
            std::fprintf (stderr, "unknown option %s (see the comment at the top of tools/analog-render.cpp)\n", a.c_str());
            return 2;
        }
    }
    if (o.list)
    {
        for (const auto& f : factoryPatches())
            std::printf ("%-18s %s\n", f.name.c_str(), f.category.c_str());
        return 0;
    }
    if (o.benchmark)
        return benchmark (o);

    std::vector<const FactoryPatch*> todo;
    if (! o.all.empty())
    {
        std::filesystem::create_directories (o.all);
        for (const auto& f : factoryPatches())
            todo.push_back (&f);
    }
    else if (const auto* f = find (o.patch))
        todo.push_back (f);
    else
    {
        std::fprintf (stderr, "no factory sound called \"%s\" (--list)\n", o.patch.c_str());
        return 1;
    }

    for (const auto* f : todo)
    {
        auto patch = f->patch;
        if (o.seed > 0)
            patch.seed = static_cast<std::uint32_t> (o.seed);
        if (o.analog >= 0.0)
            patch.analogAmount = o.analog;
        Synth synth;
        synth.prepare (o.rate, 512, o.oversampling);
        synth.setPatch (patch);
        synth.reset();
        std::vector<float> left, right;
        const auto& notes = o.notes.empty() ? f->demoNotes : o.notes;
        renderPhrase (synth, notes, o.notes.empty() && f->demoAsLine, o.seconds, o.hold, left, right);
        const auto path = o.all.empty() ? o.out : (std::filesystem::path (o.all) / fileName (f->name)).string();
        float peak = 0.0f;
        for (std::size_t i = 0; i < left.size(); ++i)
            peak = std::max ({ peak, std::abs (left[i]), std::abs (right[i]) });
        if (! wav::writeStereo (path, left, right, o.rate))
        {
            std::fprintf (stderr, "could not write %s\n", path.c_str());
            return 1;
        }
        std::printf ("%-18s -> %s (peak %.1f dBFS)\n", f->name.c_str(), path.c_str(), 20.0 * std::log10 (std::max (peak, 1.0e-9f)));
    }
    return 0;
}
