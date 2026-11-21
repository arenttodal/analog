// CINEMATIC AFTERIMAGES VOL. 1: the collection's files are the definitions' (CinematicPresets.cpp),
// they load in ANDOR ANALOG through its own preset loader, sound, stay unclipped, answer
// velocity, reload the same, and play at 44.1, 48 and 96 kHz. A hidden case writes the files and
// renders the audio previews (ANALOG_CINEMATIC_RENDER_DIR).

#include "CinematicPresets.h"

#include "AnalogParameters.h"
#include "ShellProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>

using andor::shell::ShellProcessor;

namespace
{
    struct Event
    {
        double seconds;
        juce::MidiMessage message;
    };

    /** A host's transport, playing at 120 BPM from bar 1 (what a DAW gives the plugin: SHAPER,
        the arpeggiator and the synced echo follow it). */
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

    juce::AudioBuffer<float> render (ShellProcessor& p, double rate, double seconds, const std::vector<Event>& events)
    {
        constexpr int block = 256;
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
        const int a = static_cast<int> (from * rate), z = std::min (b.getNumSamples(), static_cast<int> (to * rate));
        double sum = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = a; i < z; ++i)
                sum += static_cast<double> (b.getSample (ch, i)) * b.getSample (ch, i);
        return 10.0 * std::log10 (std::max (sum / std::max (1, 2 * (z - a)), 1.0e-20));
    }

    float peakOf (const juce::AudioBuffer<float>& b)
    {
        return std::max (b.getMagnitude (0, 0, b.getNumSamples()), b.getMagnitude (1, 0, b.getNumSamples()));
    }

    bool finite (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (ch, i)))
                    return false;
        return true;
    }

    /** A chord from the preset's own range (its lower middle): root, fifth, octave, tenth. */
    std::vector<int> chordFor (const cinematic::Spec& s)
    {
        const int root = s.lowKey + (s.highKey - s.lowKey) / 3;
        if (s.voices <= 1)
            return { root };
        return { root, root + 7, root + 12, root + 16 };
    }

    std::vector<Event> held (const std::vector<int>& notes, int velocity, double length)
    {
        std::vector<Event> e;
        for (const int n : notes)
            e.push_back ({ 0.0, juce::MidiMessage::noteOn (1, n, static_cast<juce::uint8> (velocity)) });
        for (const int n : notes)
            e.push_back ({ length, juce::MidiMessage::noteOff (1, n) });
        return e;
    }

    std::map<juce::String, float> valuesOf (ShellProcessor& p)
    {
        std::map<juce::String, float> v;
        for (auto* base : p.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (base))
                v[ranged->getParameterID()] = ranged->getValue();
        return v;
    }

    void dry (ShellProcessor& p)
    {
        // DRY: the instrument without its rooms (SPACE and ECHO out).
        for (const char* id : { "space", "echo" })
            if (auto* param = p.apvts().getParameter (id))
                param->setValueNotifyingHost (0.0f);
    }
}

TEST_CASE ("cinematic afterimages: the collection's files are its definitions, ten of them, each named once", "[cinematic]")
{
    const auto& list = cinematic::collection();
    REQUIRE (list.size() == 10);
    juce::StringArray names;
    for (const auto& s : list)
    {
        INFO (s.name);
        CHECK_FALSE (names.contains (s.name));
        names.add (s.name);
        const auto file = cinematic::fileFor (s);
        REQUIRE (file.existsAsFile());   // (ANALOG_CINEMATIC_WRITE=1 with the hidden case writes them)
        const auto wanted = cinematic::presetXml (s);
        const auto stored = juce::parseXML (file);
        REQUIRE (stored != nullptr);
        CHECK (stored->isEquivalentTo (wanted.get(), false));
    }
}

TEST_CASE ("cinematic afterimages: every preset loads through ANALOG's preset loader, sounds, stays unclipped, answers velocity and reloads the same", "[cinematic]")
{
    for (const auto& s : cinematic::collection())
    {
        INFO (s.name);
        ShellProcessor p;
        REQUIRE (p.loadPreset (cinematic::fileFor (s)));
        const auto loaded = valuesOf (p);
        const auto chord = chordFor (s);
        const double rate = 48000.0;
        const auto loud = render (p, rate, 3.0, held (chord, 120, 2.2));
        REQUIRE (finite (loud));
        const double level = rmsDb (loud, rate, 0.3, 2.2);
        UNSCOPED_INFO (s.name << ": chord " << level << " dB RMS, peak " << juce::Decibels::gainToDecibels (peakOf (loud)) << " dBFS");
        CHECK (level > -40.0);              // it sounds
        CHECK (peakOf (loud) < 0.97f);      // no clipping (headroom left at the hardest velocity)
        // Velocity: a soft chord quieter (every preset but the organ, which plays as keys).
        ShellProcessor q;
        REQUIRE (q.loadPreset (cinematic::fileFor (s)));
        const auto soft = render (q, rate, 3.0, held (chord, 35, 2.2));
        const double softLevel = rmsDb (soft, rate, 0.3, 2.2);
        if (s.name != "Gravity Cathedral")
            CHECK (softLevel < level - 2.0);
        // Reloading gives the same settings (and the same sound from the same start).
        ShellProcessor r;
        REQUIRE (r.loadPreset (cinematic::fileFor (s)));
        CHECK (valuesOf (r) == loaded);
    }
}

TEST_CASE ("cinematic afterimages: every preset plays at 44.1, 48 and 96 kHz (finite, unclipped, the same level within 2 dB)", "[cinematic]")
{
    for (const auto& s : cinematic::collection())
    {
        INFO (s.name);
        double at48 = 0.0;
        for (const double rate : { 48000.0, 44100.0, 96000.0 })
        {
            INFO ("rate " << rate);
            ShellProcessor p;
            REQUIRE (p.loadPreset (cinematic::fileFor (s)));
            dry (p);   // (the instrument itself; the rooms' tails differ by design across rates)
            const auto out = render (p, rate, 2.0, held (chordFor (s), 100, 1.5));
            REQUIRE (finite (out));
            CHECK (peakOf (out) < 0.97f);
            const double level = rmsDb (out, rate, 0.3, 1.5);
            CHECK (level > -45.0);
            if (rate == 48000.0)
                at48 = level;
            else
                CHECK (std::abs (level - at48) < 2.0);
        }
    }
}

TEST_CASE ("cinematic afterimages: write the files, render the previews (ANALOG_CINEMATIC_RENDER_DIR)", "[.][cinematic-render]")
{
    if (std::getenv ("ANALOG_CINEMATIC_WRITE") != nullptr)
    {
        cinematic::collectionFolder().createDirectory();
        for (const auto& s : cinematic::collection())
            REQUIRE (cinematic::presetXml (s)->writeTo (cinematic::fileFor (s)));
    }
    const char* dir = std::getenv ("ANALOG_CINEMATIC_RENDER_DIR");
    if (dir == nullptr)
        return;
    const juce::File out (dir);
    out.createDirectory();
    juce::StringArray report { "preset | take | version | RMS dB | peak dBFS" };
    auto write = [&out] (const juce::String& name, const juce::AudioBuffer<float>& b, double rate) {
        const auto file = out.getChildFile (name + ".wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), rate, 2, 24, {}, 0));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    };
    for (const auto& s : cinematic::collection())
    {
        const int low = s.lowKey + 5, mid = (s.lowKey + s.highKey) / 2, high = s.highKey - 5;
        const int root = s.lowKey + (s.highKey - s.lowKey) / 3;
        // Original test material: single notes at three velocities, a short motif, chords low /
        // middle / high, a long note, a bend and a wheel performance.
        std::vector<std::pair<juce::String, std::pair<double, std::vector<Event>>>> takes;
        takes.push_back ({ "notes", { 6.0, [&] {
                                          std::vector<Event> e;
                                          int i = 0;
                                          for (const int v : { 35, 80, 120 })
                                          {
                                              e.push_back ({ 0.1 + 1.8 * i, juce::MidiMessage::noteOn (1, mid, static_cast<juce::uint8> (v)) });
                                              e.push_back ({ 1.3 + 1.8 * i, juce::MidiMessage::noteOff (1, mid) });
                                              ++i;
                                          }
                                          return e;
                                      }() } });
        takes.push_back ({ "motif", { 6.0, [&] {
                                          std::vector<Event> e;
                                          const int steps[] = { 0, 7, 5, 3, 5, 10, 7, 12 };
                                          const double lengths[] = { 0.5, 0.5, 0.25, 0.25, 0.5, 0.5, 0.75, 1.25 };
                                          double t = 0.1;
                                          for (int i = 0; i < 8; ++i)
                                          {
                                              const int n = std::clamp (root + steps[i], s.lowKey, s.highKey);
                                              e.push_back ({ t, juce::MidiMessage::noteOn (1, n, static_cast<juce::uint8> (90 + (i % 3) * 10)) });
                                              t += lengths[i] * (s.voices <= 1 ? 1.0 : 0.95);
                                              e.push_back ({ t - (s.voices <= 1 ? 0.0 : 0.02), juce::MidiMessage::noteOff (1, n) });
                                          }
                                          return e;
                                      }() } });
        takes.push_back ({ "chords", { 9.0, [&] {
                                           std::vector<Event> e;
                                           int i = 0;
                                           for (const int r : { low, root + 5, high - 12 })
                                           {
                                               const std::vector<int> chord = s.voices <= 1 ? std::vector<int> { r } : std::vector<int> { r, r + 4, r + 7, r + 11 };
                                               for (const int n : chord)
                                                   e.push_back ({ 0.1 + 2.8 * i, juce::MidiMessage::noteOn (1, std::min (n, s.highKey), static_cast<juce::uint8> (100)) });
                                               for (const int n : chord)
                                                   e.push_back ({ 2.3 + 2.8 * i, juce::MidiMessage::noteOff (1, std::min (n, s.highKey)) });
                                               ++i;
                                           }
                                           return e;
                                       }() } });
        takes.push_back ({ "long", { 9.0, { { 0.1, juce::MidiMessage::noteOn (1, root, static_cast<juce::uint8> (100)) }, { 6.5, juce::MidiMessage::noteOff (1, root) } } } });
        takes.push_back ({ "expression", { 8.0, [&] {
                                               std::vector<Event> e { { 0.1, juce::MidiMessage::noteOn (1, mid, static_cast<juce::uint8> (100)) } };
                                               for (int k = 0; k <= 40; ++k)   // a whole-tone bend up and back
                                               {
                                                   const double x = k / 40.0;
                                                   const int bend = 8192 + static_cast<int> (std::sin (x * 3.14159265) * 8191.0 * 2.0 / std::max (2.0, 2.0));
                                                   e.push_back ({ 0.8 + 1.2 * x, juce::MidiMessage::pitchWheel (1, std::clamp (bend, 0, 16383)) });
                                               }
                                               for (int k = 0; k <= 40; ++k)   // the wheel up, then aftertouch up
                                               {
                                                   const double x = k / 40.0;
                                                   e.push_back ({ 2.4 + 2.0 * x, juce::MidiMessage::controllerEvent (1, 1, static_cast<int> (127 * x)) });
                                                   e.push_back ({ 4.6 + 1.6 * x, juce::MidiMessage::channelPressureChange (1, static_cast<int> (127 * x)) });
                                               }
                                               e.push_back ({ 6.6, juce::MidiMessage::noteOff (1, mid) });
                                               return e;
                                           }() } });
        for (const auto& [take, spec] : takes)
            for (const bool finished : { true, false })
            {
                ShellProcessor p;
                REQUIRE (p.loadPreset (cinematic::fileFor (s)));
                if (! finished)
                    dry (p);
                const double rate = 48000.0;
                const auto audio = render (p, rate, spec.first, spec.second);
                const auto label = juce::File::createLegalFileName (juce::String (s.name)) + "_" + take + (finished ? "" : "_dry");
                write (label, audio, rate);
                report.add (juce::String (s.name) + " | " + take + " | " + (finished ? "finished" : "dry") + " | " + juce::String (rmsDb (audio, rate, 0.0, spec.first), 1)
                            + " | " + juce::String (juce::Decibels::gainToDecibels (peakOf (audio)), 1));
            }
    }
    out.getChildFile ("levels.txt").replaceWithText (report.joinIntoString ("\n"));
}
