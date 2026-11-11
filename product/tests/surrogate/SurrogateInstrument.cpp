// ANDOR Test Instrument: a small VST3 instrument that stands in for Kontakt in the Kontakt
// external SoundSource experiment's tests (docs/experiments/KONTAKT_EXTERNAL_MORPH.md). It is
// NOT Kontakt and is never shipped; it lets the real VST3 hosting path (scan, instantiate,
// bus layout, prepare, MIDI, stereo audio, editor, state, latency) be exercised where Kontakt
// is not installed.
//
// Its two "instruments", chosen in its own editor (as a Kontakt instrument is chosen in
// Kontakt's):
//   LEGATO STRINGS - monophonic legato: a note that starts while another is held glides to its
//                    pitch (80 ms) without a new attack; a note after a gap attacks anew; a
//                    released note falls back to the one still held. Overlap decides legato,
//                    as in sampled legato libraries.
//   EVOLVING PAD   - polyphonic (8), a slow filter drift: an evolving chord texture.
// Its state (the instrument, a reported latency for tests) round-trips through
// getStateInformation / setStateInformation.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cmath>

namespace
{
    constexpr double twoPi = 6.283185307179586;

    struct Voice
    {
        int note = -1;
        double hz = 0.0, targetHz = 0.0;
        std::array<double, 5> phase {};
        double env = 0.0;
        bool gate = false;
        double lpL = 0.0, lpR = 0.0, lp2L = 0.0, lp2R = 0.0;
        double age = 0.0;
    };

    double noteHz (int note) { return 440.0 * std::pow (2.0, (note - 69) / 12.0); }
}

class SurrogateInstrument final : public juce::AudioProcessor
{
public:
    enum Program { legatoStrings = 0, evolvingPad = 1 };

    SurrogateInstrument()
        : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    const juce::String getName() const override { return "ANDOR Test Instrument"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void prepareToPlay (double sampleRate, int) override
    {
        rate = sampleRate;
        for (auto& v : voices)
            v = Voice {};
        held.clear();
        setLatencySamples (latency.load());
    }
    void releaseResources() override {}

    int program() const noexcept { return currentProgram.load(); }
    void setProgram (int p)
    {
        currentProgram.store (juce::jlimit (0, 1, p));
    }
    juce::String programName() const { return program() == legatoStrings ? "LEGATO STRINGS" : "EVOLVING PAD"; }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
        int done = 0;
        for (const auto m : midi)
        {
            render (l, r, done, m.samplePosition);
            done = juce::jmax (done, m.samplePosition);
            handle (m.getMessage());
        }
        render (l, r, done, buffer.getNumSamples());
        // (a reported latency is honoured: the output is late by it)
        const int lat = latency.load();
        if (lat > 0)
            delayBy (buffer, lat);
    }

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        juce::XmlElement xml ("AndorTestInstrument");
        xml.setAttribute ("program", program());
        xml.setAttribute ("latency", latency.load());
        copyXmlToBinary (xml, dest);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size); xml != nullptr && xml->hasTagName ("AndorTestInstrument"))
        {
            setProgram (xml->getIntAttribute ("program", 0));
            latency.store (juce::jlimit (0, 4096, xml->getIntAttribute ("latency", 0)));
            setLatencySamples (latency.load());
        }
    }

private:
    void handle (const juce::MidiMessage& m)
    {
        if (m.isNoteOn())
            noteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff())
            noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            held.clear();
            for (auto& v : voices)
            {
                v.gate = false;
                if (m.isAllSoundOff())
                    v.env = 0.0;
            }
        }
    }

    void noteOn (int note, float velocity)
    {
        held.removeFirstMatchingValue (note);
        held.add (note);
        level = 0.25 + 0.75 * velocity;
        if (program() == legatoStrings)
        {
            auto& v = voices[0];
            if (v.gate && held.size() > 1)
            {
                v.targetHz = noteHz (note);   // legato: glide, no new attack
                v.note = note;
                return;
            }
            v.note = note;
            v.hz = v.targetHz = noteHz (note);
            v.gate = true;
            v.age = 0.0;
            return;
        }
        // The pad: a free voice (else the quietest).
        auto* best = &voices[0];
        for (auto& v : voices)
            if (! v.gate && v.env < best->env)
                best = &v;
        best->note = note;
        best->hz = best->targetHz = noteHz (note);
        best->gate = true;
        best->age = 0.0;
    }

    void noteOff (int note)
    {
        held.removeFirstMatchingValue (note);
        if (program() == legatoStrings)
        {
            auto& v = voices[0];
            if (v.note != note)
                return;
            if (! held.isEmpty())
            {
                v.note = held.getLast();   // back to the note still held, legato
                v.targetHz = noteHz (v.note);
            }
            else
                v.gate = false;
            return;
        }
        for (auto& v : voices)
            if (v.note == note && v.gate)
                v.gate = false;
    }

    void render (float* l, float* r, int from, int to)
    {
        const bool legato = program() == legatoStrings;
        const double attack = 1.0 - std::exp (-1.0 / ((legato ? 0.09 : 0.35) * rate));
        const double release = 1.0 - std::exp (-1.0 / ((legato ? 0.35 : 0.8) * rate));
        const double glide = 1.0 - std::exp (-1.0 / (0.025 * rate));   // (80 ms to within 4 %)
        static constexpr std::array<double, 5> detune { -0.12, -0.05, 0.0, 0.06, 0.11 };   // semitones
        for (int i = from; i < to; ++i)
        {
            double outL = 0.0, outR = 0.0;
            drift += 0.07 / rate;
            for (int k = 0; k < (legato ? 1 : 8); ++k)
            {
                auto& v = voices[static_cast<std::size_t> (k)];
                v.env += (v.gate ? attack * (1.0 - v.env) : -release * v.env);
                if (v.env < 1.0e-5 && ! v.gate)
                    continue;
                v.hz += glide * (v.targetHz - v.hz);
                v.age += 1.0 / rate;
                double sL = 0.0, sR = 0.0;
                for (std::size_t j = 0; j < v.phase.size(); ++j)
                {
                    v.phase[j] += v.hz * std::pow (2.0, detune[j] / 12.0) / rate;
                    v.phase[j] -= std::floor (v.phase[j]);
                    const double saw = 2.0 * v.phase[j] - 1.0;
                    sL += saw * (j < 3 ? 0.8 : 0.4);
                    sR += saw * (j >= 2 ? 0.8 : 0.4);
                }
                // A bowed brightness: the cutoff opens with the attack (strings) or drifts (pad).
                const double cut = legato ? 900.0 + 2600.0 * v.env : 500.0 + 1400.0 * (0.5 + 0.5 * std::sin (twoPi * (drift + 0.13 * k)));
                const double a = 1.0 - std::exp (-twoPi * cut / rate);
                v.lpL += a * (sL - v.lpL);
                v.lpR += a * (sR - v.lpR);
                v.lp2L += a * (v.lpL - v.lp2L);
                v.lp2R += a * (v.lpR - v.lp2R);
                const double vib = legato ? 1.0 + 0.004 * std::sin (twoPi * 5.2 * v.age) * std::min (1.0, v.age / 0.6) : 1.0;
                outL += v.env * v.lp2L * vib;
                outR += v.env * v.lp2R * vib;
            }
            const double g = 0.12 * level;
            l[i] = static_cast<float> (g * outL);
            if (r != nullptr)
                r[i] = static_cast<float> (g * outR);
        }
    }

    void delayBy (juce::AudioBuffer<float>& buffer, int lat)
    {
        if (delayLine.getNumSamples() != lat)
        {
            delayLine.setSize (2, lat);
            delayLine.clear();
            delayPos = 0;
        }
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
            {
                const float x = buffer.getSample (ch, i);
                buffer.setSample (ch, i, delayLine.getSample (ch, delayPos));
                delayLine.setSample (ch, delayPos, x);
            }
            delayPos = (delayPos + 1) % lat;
        }
    }

    double rate = 48000.0, drift = 0.0, level = 1.0;
    std::array<Voice, 8> voices {};
    juce::Array<int> held;
    std::atomic<int> currentProgram { legatoStrings };
    std::atomic<int> latency { 0 };
    juce::AudioBuffer<float> delayLine;
    int delayPos = 0;
};

/** Its own editor: the instrument is chosen here (as in Kontakt's). */
class SurrogateEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SurrogateEditor (SurrogateInstrument& p) : juce::AudioProcessorEditor (p), owner (p)
    {
        title.setText ("ANDOR TEST INSTRUMENT (a stand-in for Kontakt)", juce::dontSendNotification);
        addAndMakeVisible (title);
        instrument.addItem ("LEGATO STRINGS", 1);
        instrument.addItem ("EVOLVING PAD", 2);
        instrument.setSelectedId (owner.program() + 1, juce::dontSendNotification);
        instrument.onChange = [this] { owner.setProgram (instrument.getSelectedId() - 1); };
        addAndMakeVisible (instrument);
        setSize (420, 120);
    }
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff23272a)); }
    void resized() override
    {
        title.setBounds (12, 10, getWidth() - 24, 24);
        instrument.setBounds (12, 50, 220, 26);
    }

private:
    SurrogateInstrument& owner;
    juce::Label title;
    juce::ComboBox instrument;
};

juce::AudioProcessorEditor* SurrogateInstrument::createEditor() { return new SurrogateEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SurrogateInstrument(); }
