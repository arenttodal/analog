// CINEMATIC AFTERIMAGES VOL. 2 (content/cinematic-afterimages-v2): the catalogue is complete and
// consistent; every file loads through ANDOR ANALOG's own preset loader and restores exactly the
// definition's state; every preset sounds in its range, stays unclipped and finite. Hidden cases
// calibrate the loudness trims, write the files and the manifest, and render the previews and the
// QA measurements (CA2_RENDER_DIR).

#include "../../content/cinematic-afterimages-v2/source/Pack.h"

#include "AnalogEngine.h"
#include "AnalogParameters.h"
#include "ShellProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

using andor::shell::ShellProcessor;

namespace
{
    struct Event
    {
        double seconds;
        juce::MidiMessage message;
    };

    /** A host's transport at 120 BPM from bar 1 (what a DAW gives: SHAPER, the arpeggiator and the
        synced echo follow it). */
    struct Transport final : juce::AudioPlayHead
    {
        double rate = 48000.0;
        juce::int64 sample = 0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (120.0);
            info.setIsPlaying (true);
            info.setTimeInSamples (sample);
            info.setTimeInSeconds (static_cast<double> (sample) / rate);
            info.setPpqPosition (static_cast<double> (sample) / rate * 2.0);
            info.setPpqPositionOfLastBarStart (std::floor (static_cast<double> (sample) / rate * 2.0 / 4.0) * 4.0);
            info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            return info;
        }
    };

    juce::AudioBuffer<float> render (ShellProcessor& p, double rate, double seconds, const std::vector<Event>& events, int block = 256)
    {
        Transport transport;
        transport.rate = rate;
        p.setPlayHead (&transport);
        p.prepareToPlay (rate, block);
        const int total = static_cast<int> (seconds * rate);
        juce::AudioBuffer<float> out (2, total), buffer (2, block);
        for (int pos = 0; pos < total; pos += block)
        {
            juce::MidiBuffer midi;
            for (const auto& e : events)
                if (const int s = static_cast<int> (std::llround (e.seconds * rate)); s >= pos && s < pos + block)
                    midi.addEvent (e.message, s - pos);
            buffer.clear();
            transport.sample = pos;
            p.processBlock (buffer, midi);
            const int n = std::min (block, total - pos);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, buffer, ch, 0, n);
        }
        p.setPlayHead (nullptr);
        return out;
    }

    double rmsDb (const juce::AudioBuffer<float>& b, double rate, double from, double to)
    {
        const int a = std::clamp (static_cast<int> (from * rate), 0, b.getNumSamples()), z = std::min (b.getNumSamples(), static_cast<int> (to * rate));
        double sum = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = a; i < z; ++i)
                sum += static_cast<double> (b.getSample (ch, i)) * b.getSample (ch, i);
        return 10.0 * std::log10 (std::max (sum / std::max (1, 2 * (z - a)), 1.0e-20));
    }

    /** The level of what sounds (100 ms windows within 20 dB of the loudest, their mean power):
        a sparse pluck and a held pad compared by what you hear, not by their silences. */
    double gatedRmsDb (const juce::AudioBuffer<float>& b, double rate)
    {
        const int w = static_cast<int> (0.1 * rate);
        std::vector<double> windows;
        for (int start = 0; start + w <= b.getNumSamples(); start += w)
        {
            double sum = 0.0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = start; i < start + w; ++i)
                    sum += static_cast<double> (b.getSample (ch, i)) * b.getSample (ch, i);
            windows.push_back (sum / (2.0 * w));
        }
        if (windows.empty())
            return -200.0;
        const double loudest = *std::max_element (windows.begin(), windows.end());
        double sum = 0.0;
        int n = 0;
        for (const double x : windows)
            if (x >= loudest * 0.01)
            {
                sum += x;
                ++n;
            }
        return 10.0 * std::log10 (std::max (sum / std::max (1, n), 1.0e-20));
    }

    double peakDb (const juce::AudioBuffer<float>& b)
    {
        const float peak = std::max (b.getMagnitude (0, 0, b.getNumSamples()), b.getMagnitude (1, 0, b.getNumSamples()));
        return 20.0 * std::log10 (std::max (1.0e-9, static_cast<double> (peak)));
    }

    bool finite (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (ch, i)))
                    return false;
        return true;
    }

    auto on (double t, int note, int velocity) { return Event { t, juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (velocity)) }; }
    auto off (double t, int note) { return Event { t, juce::MidiMessage::noteOff (1, note) }; }

    /** The preset's centre of gravity: a third of the way up its range (basses low, keys high). */
    int rootOf (const ca2::Spec& s) { return s.lowKey + (s.highKey - s.lowKey) / 3; }

    struct Take
    {
        const char* name;
        double seconds;
        std::vector<Event> events;
    };

    /** The preset's main phrase (what its preview plays and the calibration measures): an
        original line or chord shape in its own range, its own character's gesture. */
    Take phraseOf (const ca2::Spec& s)
    {
        const int r = rootOf (s);
        std::vector<Event> e;
        switch (s.phrase)
        {
            case ca2::Phrase::bassLine:
            {
                // An original rolling bass figure (root, fifth, octave drop, a fourth), quarter notes at 120.
                const int steps[] { 0, 0, 7, 5, 0, -2, 3, 0 };
                for (int i = 0; i < 8; ++i)
                {
                    e.push_back (on (0.5 * i, r + steps[i], i % 4 == 0 ? 112 : 90));
                    e.push_back (off (0.5 * i + 0.42, r + steps[i]));
                }
                return { "phrase", 5.0, e };
            }
            case ca2::Phrase::lead:
            {
                // An original eight-note line: a rising question, a falling answer.
                const int steps[] { 0, 3, 7, 10, 8, 7, 3, 5 };
                const double lengths[] { 0.5, 0.5, 0.5, 1.0, 0.5, 0.5, 0.5, 1.5 };
                double t = 0.0;
                for (int i = 0; i < 8; ++i)
                {
                    e.push_back (on (t, r + 12 + steps[i], 70 + 6 * (i % 4)));
                    e.push_back (off (t + lengths[i] * 0.95, r + 12 + steps[i]));
                    t += lengths[i];
                }
                return { "phrase", t + 3.0, e };
            }
            case ca2::Phrase::chords:
            case ca2::Phrase::arpChord:
            {
                // Two chords (minor, then its relative major's colour), two bars each.
                const std::vector<int> a { r, r + 3, r + 7, r + 12 }, b { r - 4, r, r + 3, r + 7 };
                for (const int n : a) { e.push_back (on (0.0, n, 92)); e.push_back (off (3.9, n)); }
                for (const int n : b) { e.push_back (on (4.0, n, 92)); e.push_back (off (7.9, n)); }
                return { "phrase", 11.0, e };
            }
            case ca2::Phrase::drone:
            {
                // A held root and fifth, then the fifth moves up to the sixth.
                e.push_back (on (0.0, r, 96));
                e.push_back (on (0.0, r + 7, 88));
                e.push_back (off (4.0, r + 7));
                e.push_back (on (4.0, r + 8, 88));
                e.push_back (off (8.0, r));
                e.push_back (off (8.0, r + 8));
                return { "phrase", 13.0, e };
            }
            case ca2::Phrase::keys:
            {
                // A sparse broken chord, falling.
                const int steps[] { 12, 7, 3, 0, 7, 3, -5, 0 };
                for (int i = 0; i < 8; ++i)
                {
                    e.push_back (on (0.375 * i, r + steps[i], 80 + 5 * (i % 3)));
                    e.push_back (off (0.375 * i + 0.3, r + steps[i]));
                }
                return { "phrase", 7.0, e };
            }
        }
        return { "phrase", 1.0, e };
    }

    /** The other takes: notes at velocities 45, 85, 120; a long note with its tail; a bend and the
        wheel rising. */
    std::vector<Take> takesOf (const ca2::Spec& s)
    {
        const int r = rootOf (s) + (s.phrase == ca2::Phrase::lead ? 12 : 0);
        std::vector<Take> t { phraseOf (s) };
        t.push_back ({ "notes", 6.0, { on (0.0, r, 45), off (1.5, r), on (2.0, r, 85), off (3.5, r), on (4.0, r, 120), off (5.5, r) } });
        t.push_back ({ "long", 12.0, { on (0.0, r, 96), off (6.0, r) } });
        std::vector<Event> gesture { on (0.0, r, 96) };
        for (int i = 0; i <= 20; ++i)
        {
            const double x = i / 20.0;
            gesture.push_back ({ 0.5 + 1.0 * x, juce::MidiMessage::pitchWheel (1, 8192 + juce::roundToInt (4096.0 * std::sin (x * 3.14159265))) });
            gesture.push_back ({ 2.0 + 2.0 * x, juce::MidiMessage::controllerEvent (1, 1, juce::roundToInt (127.0 * x)) });
            gesture.push_back ({ 4.5 + 1.5 * x, juce::MidiMessage::channelPressureChange (1, juce::roundToInt (127.0 * x)) });
        }
        gesture.push_back (off (6.5, r));
        t.push_back ({ "expression", 9.0, gesture });
        return t;
    }

    std::map<juce::String, float> valuesOf (ShellProcessor& p)
    {
        std::map<juce::String, float> v;
        for (auto* base : p.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (base))
                v[ranged->getParameterID()] = ranged->convertFrom0to1 (ranged->getValue());
        return v;
    }

    /** The first parameter that differs (empty: none), within float rounding. */
    std::string differenceOf (const std::map<juce::String, float>& a, const std::map<juce::String, float>& b)
    {
        for (const auto& [id, value] : a)
        {
            const auto it = b.find (id);
            if (it == b.end())
                return (id + " missing").toStdString();
            if (std::abs (it->second - value) > 1.0e-4f + 1.0e-5f * std::max (std::abs (value), std::abs (it->second)))
                return (id + ": " + juce::String (value, 6) + " vs " + juce::String (it->second, 6)).toStdString();
        }
        return {};
    }

    std::string routeOf (ShellProcessor& p)
    {
        using namespace andor::analog;
        const auto& g = dynamic_cast<AnalogEngine&> (p.productEngine()).fxRoute();
        return fx::mainText (g) + " | " + fx::sendText (g);
    }

    /** The calibration's target loudness (sounding RMS of the phrase take) per family: basses a
        little hotter, atmospheres and keys a little lower (they live under a mix). */
    double targetOf (ca2::Family f)
    {
        static const double t[] { -17.0, -17.0, -19.0, -19.0, -19.0, -20.0, -19.0, -20.0, -21.0, -21.0 };
        return t[std::clamp (static_cast<int> (f), 0, 9)];
    }

    juce::File outputDir (const char* variable)
    {
        const char* dir = std::getenv (variable);
        return dir != nullptr ? juce::File (dir) : juce::File();
    }
}

TEST_CASE ("cinematic afterimages vol. 2: the catalogue - numbered from 1 without gaps, every name and ID once, ten families of up to ten", "[ca2]")
{
    const auto& list = ca2::collection();
    REQUIRE (! list.empty());
    std::set<std::string> names, ids;
    std::map<int, int> perFamily;
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        const auto& s = list[i];
        INFO (s.id() << " " << s.name);
        CHECK (s.number == static_cast<int> (i) + 1);
        CHECK (names.insert (s.name).second);
        CHECK (ids.insert (s.id()).second);
        CHECK (static_cast<int> (s.family) == (s.number - 1) / 10);   // (CA2-001..010 family A, ...)
        CHECK (! s.description.empty());
        CHECK (! s.play.empty());
        CHECK (! s.role.empty());
        CHECK (! s.identity.empty());
        CHECK (s.lowKey < s.highKey);
        ++perFamily[static_cast<int> (s.family)];
    }
    for (const auto& [family, count] : perFamily)
        CHECK (count <= 10);
    if (list.size() == 100)
        for (int f = 0; f < 10; ++f)
            CHECK (perFamily[f] == 10);
}

TEST_CASE ("cinematic afterimages vol. 2: every file loads through the preset loader and restores its definition exactly; it sounds, unclipped and finite", "[ca2]")
{
    for (const auto& s : ca2::collection())
    {
        INFO (s.id() << " " << s.name);
        const auto file = ca2::fileFor (s);
        REQUIRE (file.existsAsFile());   // ([ca2-write] writes them)
        ShellProcessor wanted;
        ca2::configure (wanted, s);
        ShellProcessor loaded;
        REQUIRE (loaded.loadPreset (file));
        CHECK (differenceOf (valuesOf (wanted), valuesOf (loaded)) == std::string());
        CHECK (routeOf (wanted) == routeOf (loaded));
        const auto take = phraseOf (s);
        const auto audio = render (loaded, 48000.0, take.seconds, take.events);
        REQUIRE (finite (audio));
        const double level = rmsDb (audio, 48000.0, 0.0, take.seconds);
        CHECK (level > -40.0);
        CHECK (peakDb (audio) < -0.3);
    }
}

TEST_CASE ("cinematic afterimages vol. 2: calibrate the loudness trims (writes trims.inc; CA2_CALIBRATE=1)", "[.][ca2-calibrate]")
{
    REQUIRE (std::getenv ("CA2_CALIBRATE") != nullptr);
    juce::String table ("// Written by [ca2-calibrate]: { number, trim dB } - one gain for every source a preset plays,\n"
                        "// chosen so its phrase meets its family's loudness, never louder than -1 dBFS at its peak.\n");
    for (const auto& s : ca2::collection())
    {
        ShellProcessor p;
        ca2::configure (p, s, false);
        const auto take = phraseOf (s);
        const auto audio = render (p, 48000.0, take.seconds, take.events);
        const double level = gatedRmsDb (audio, 48000.0), peak = peakDb (audio);
        const double trim = std::clamp (std::min (targetOf (s.family) - level, -1.0 - peak), -24.0, 12.0);
        table << "{ " << s.number << ", " << juce::String (trim, 2) << " },   // " << s.name << ": " << juce::String (level, 1) << " dB (sounding RMS), "
              << juce::String (peak, 1) << " dBFS peak before\n";
    }
    ca2::contentFolder().getChildFile ("source/trims.inc").replaceWithText (table);
}

TEST_CASE ("cinematic afterimages vol. 2: write the presets (savePreset) and the manifest (CA2_WRITE=1)", "[.][ca2-write]")
{
    REQUIRE (std::getenv ("CA2_WRITE") != nullptr);
    const auto revision = juce::String (std::getenv ("CA2_REVISION") != nullptr ? std::getenv ("CA2_REVISION") : "");
    juce::Array<juce::var> entries;
    for (const auto& s : ca2::collection())
    {
        ShellProcessor p;
        ca2::configure (p, s);
        const auto file = ca2::fileFor (s);
        REQUIRE (p.savePreset (file));
        auto* e = new juce::DynamicObject();
        e->setProperty ("id", juce::String (s.id()));
        e->setProperty ("name", juce::String (s.name));
        e->setProperty ("family", juce::String (ca2::familyName (s.family)));
        e->setProperty ("category", juce::String (ca2::familySubtitle (s.family)));
        e->setProperty ("description", juce::String (s.description));
        e->setProperty ("howToPlay", juce::String (s.play));
        e->setProperty ("musicalRole", juce::String (s.role));
        e->setProperty ("designerNotes", juce::String (s.identity));
        e->setProperty ("range", juce::MidiMessage::getMidiNoteName (s.lowKey, true, true, 4) + " - " + juce::MidiMessage::getMidiNoteName (s.highKey, true, true, 4));
        e->setProperty ("lowKey", s.lowKey);
        e->setProperty ("highKey", s.highKey);
        e->setProperty ("previewPhrase", juce::String (std::array<const char*, 6> { "bass line", "lead line", "chords", "held chord (arpeggiated)", "drone", "broken chord" }[static_cast<std::size_t> (s.phrase)]));
        e->setProperty ("file", file.getRelativePathFrom (ca2::contentFolder()).replaceCharacter ('\\', '/'));
        e->setProperty ("fxRoute", juce::String (routeOf (p)));
        e->setProperty ("loudnessTrimDb", ca2::trimOf (s));
        e->setProperty ("status", "awaiting owner listening");
        e->setProperty ("sourceRevision", revision);
        entries.add (juce::var (e));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("collection", "Cinematic Afterimages Vol. 2");
    root->setProperty ("product", "ANDOR ANALOG");
    root->setProperty ("schemaVersion", 1);
    root->setProperty ("presetFormat", ".andoranalog (ShellProcessor::savePreset)");
    root->setProperty ("count", entries.size());
    root->setProperty ("presets", entries);
    ca2::contentFolder().getChildFile ("manifest.json").replaceWithText (juce::JSON::toString (juce::var (root), false));
}

TEST_CASE ("cinematic afterimages vol. 2: render the previews and the QA measurements (CA2_RENDER_DIR)", "[.][ca2-render]")
{
    const auto out = outputDir ("CA2_RENDER_DIR");
    REQUIRE (out != juce::File());
    out.createDirectory();
    const juce::String only = std::getenv ("CA2_ONLY") != nullptr ? std::getenv ("CA2_ONLY") : "";
    juce::Array<juce::var> qa;
    auto write = [&out] (const juce::String& name, const juce::AudioBuffer<float>& audio, double rate) {
        const auto file = out.getChildFile (name + ".wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), rate, 2, 24, {}, 0));
        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    };
    for (const auto& s : ca2::collection())
    {
        if (only.isNotEmpty() && ! juce::StringArray::fromTokens (only, ",", "").contains (juce::String (s.number)))
            continue;
        const auto file = ca2::fileFor (s);
        auto* q = new juce::DynamicObject();
        q->setProperty ("id", juce::String (s.id()));
        q->setProperty ("name", juce::String (s.name));
        const auto label = juce::String (s.id()) + " " + juce::File::createLegalFileName (juce::String (s.name));
        for (const auto& take : takesOf (s))
            for (const bool finished : { true, false })
            {
                ShellProcessor p;
                REQUIRE (p.loadPreset (file));
                if (! finished)
                    for (const char* id : { "space", "echo" })
                        if (auto* param = p.apvts().getParameter (id))
                            param->setValueNotifyingHost (0.0f);
                const auto audio = render (p, 48000.0, take.seconds, take.events);
                write (label + " - " + take.name + (finished ? "" : " (dry)"), audio, 48000.0);
                if (finished)
                {
                    q->setProperty (juce::String (take.name) + "RmsDb", juce::roundToInt (rmsDb (audio, 48000.0, 0.0, take.seconds) * 10.0) / 10.0);
                    q->setProperty (juce::String (take.name) + "SoundingRmsDb", juce::roundToInt (gatedRmsDb (audio, 48000.0) * 10.0) / 10.0);
                    q->setProperty (juce::String (take.name) + "PeakDbfs", juce::roundToInt (peakDb (audio) * 10.0) / 10.0);
                }
            }
        // Technical: sample rates, block sizes, determinism, mono compatibility, the tail's end.
        const auto take = phraseOf (s);
        std::map<int, double> levels;
        for (const double rate : { 44100.0, 48000.0, 96000.0 })
        {
            ShellProcessor p;
            REQUIRE (p.loadPreset (file));
            const auto audio = render (p, rate, take.seconds, take.events);
            levels[juce::roundToInt (rate)] = rmsDb (audio, rate, 0.0, take.seconds);
            q->setProperty ("finite" + juce::String (juce::roundToInt (rate)), finite (audio));
        }
        q->setProperty ("rateSpreadDb", juce::roundToInt ((std::max ({ levels[44100], levels[48000], levels[96000] }) - std::min ({ levels[44100], levels[48000], levels[96000] })) * 100.0) / 100.0);
        ShellProcessor a, b, c;
        REQUIRE (a.loadPreset (file));
        REQUIRE (b.loadPreset (file));
        REQUIRE (c.loadPreset (file));
        const auto first = render (a, 48000.0, take.seconds, take.events, 256);
        const auto again = render (b, 48000.0, take.seconds, take.events, 256);
        const auto small = render (c, 48000.0, take.seconds, take.events, 64);
        double maxDiff = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < first.getNumSamples(); ++i)
                maxDiff = std::max (maxDiff, static_cast<double> (std::abs (first.getSample (ch, i) - again.getSample (ch, i))));
        q->setProperty ("deterministic", maxDiff == 0.0);
        q->setProperty ("block64LevelDeltaDb", juce::roundToInt (std::abs (rmsDb (small, 48000.0, 0.0, take.seconds) - rmsDb (first, 48000.0, 0.0, take.seconds)) * 100.0) / 100.0);
        double mid = 0.0, side = 0.0;
        for (int i = 0; i < first.getNumSamples(); ++i)
        {
            const double l = first.getSample (0, i), r = first.getSample (1, i);
            mid += (l + r) * (l + r);
            side += (l - r) * (l - r);
        }
        q->setProperty ("monoSideToMidDb", juce::roundToInt (10.0 * std::log10 (std::max (side, 1.0e-20) / std::max (mid, 1.0e-20)) * 10.0) / 10.0);
        const auto tail = rmsDb (first, 48000.0, take.seconds - 0.5, take.seconds);
        q->setProperty ("tailEndDb", juce::roundToInt (tail * 10.0) / 10.0);
        qa.add (juce::var (q));
    }
    out.getChildFile ("qa.json").replaceWithText (juce::JSON::toString (juce::var (qa), false));
}
