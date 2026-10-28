// ANDOR ANALOG through the ANDOR Instrument Shell: its parameters and patches, its factory
// presets, MIDI at its own samples, state recall, and the rule that every control the
// interface enables moves the sound while every one it disables does not.
// (Compiled into andor_analog_tests with the shell's own tests, tests/shell/ShellTests.cpp.)

#include "AnalogEngine.h"
#include "AnalogParameters.h"
#include "CeramicSkin.h"
#include "FactoryPatches.h"
#include "ShellEditor.h"
#include "ShellProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <map>

using andor::shell::ShellProcessor;
using namespace andor::analog;
using Catch::Approx;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int block = 512;

    struct Note
    {
        int sample, note, velocity;   ///< velocity 0: off
    };

    /** The test phrase: a chord, a pitch bend, a legato line, releases. */
    const std::vector<Note> phrase { { 0, 57, 90 }, { 0, 64, 90 }, { 0, 69, 90 }, { 9000, 72, 90 }, { 14000, 57, 0 }, { 14000, 64, 0 },
                                     { 15000, 74, 90 }, { 16000, 72, 0 }, { 20000, 69, 0 }, { 24000, 74, 0 } };

    juce::AudioBuffer<float> renderShell (ShellProcessor& p, double seconds, const std::vector<Note>& notes = phrase, bool bend = true)
    {
        p.prepareToPlay (rate, block);
        const int total = static_cast<int> (seconds * rate);
        juce::AudioBuffer<float> out (2, total), buffer (2, block);
        for (int pos = 0; pos < total; pos += block)
        {
            juce::MidiBuffer midi;
            for (const auto& n : notes)
                if (n.sample >= pos && n.sample < pos + block)
                    midi.addEvent (n.velocity > 0 ? juce::MidiMessage::noteOn (1, n.note, static_cast<juce::uint8> (n.velocity))
                                                  : juce::MidiMessage::noteOff (1, n.note),
                                   n.sample - pos);
            if (bend && pos <= 6000 && 6000 < pos + block)
                midi.addEvent (juce::MidiMessage::pitchWheel (1, 12000), 6000 - pos);
            p.processBlock (buffer, midi);
            const int n = std::min (block, total - pos);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, buffer, ch, 0, n);
        }
        return out;
    }

    double differenceRms (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        double sum = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
            {
                const double d = a.getSample (ch, i) - b.getSample (ch, i);
                sum += d * d;
            }
        return std::sqrt (sum / (2.0 * a.getNumSamples()));
    }

    bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
                if (! juce::exactlyEqual (a.getSample (ch, i), b.getSample (ch, i)))
                    return false;
        return true;
    }

    void setValue (ShellProcessor& p, const juce::String& id, float value)
    {
        auto* param = p.parameters.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    void apply (ShellProcessor& p, const Patch& patch)
    {
        for (const auto& [id, value] : params::valuesOf (patch))
            setValue (p, id, value);
    }

    /** Every parameter value a patch sets, read back through the parameters, within their steps. */
    void checkSameValues (const std::map<juce::String, float>& a, const std::map<juce::String, float>& b, ShellProcessor& p)
    {
        REQUIRE (a.size() == b.size());
        for (const auto& [id, value] : a)
        {
            INFO (id);
            const auto range = p.parameters.getParameterRange (id);
            const float step = std::max (range.interval, 1.0e-4f * (range.end - range.start));
            CHECK (b.at (id) == Approx (value).margin (step));
        }
    }
}

TEST_CASE ("analog: the parameters' defaults are the INIT patch", "[analog][parameters]")
{
    ShellProcessor p;
    params::Reader reader;
    reader.attach (p.parameters);
    checkSameValues (params::valuesOf (Patch()), params::valuesOf (reader.read()), p);
}

TEST_CASE ("analog: every factory sound survives the trip through the parameters", "[analog][parameters]")
{
    ShellProcessor p;
    params::Reader reader;
    reader.attach (p.parameters);
    for (const auto& factory : factoryPatches())
    {
        INFO (factory.name);
        apply (p, factory.patch);
        checkSameValues (params::valuesOf (factory.patch), params::valuesOf (reader.read()), p);
    }
}

TEST_CASE ("analog: the factory presets are installed, listed and load exactly", "[analog][presets]")
{
    ShellProcessor p;
    const auto list = p.presetList();
    for (const auto& factory : factoryPatches())
    {
        INFO (factory.name);
        const auto entry = std::find_if (list.begin(), list.end(), [&] (const auto& e) { return e.name == juce::String (factory.name); });
        REQUIRE (entry != list.end());
        p.openPresetEntry (*entry);
        CHECK (p.presetDisplayName() == juce::String (factory.name));
        params::Reader reader;
        reader.attach (p.parameters);
        checkSameValues (params::valuesOf (factory.patch), params::valuesOf (reader.read()), p);
    }
    // A second instance does not write them again (same version), and a musician's preset
    // beside them is kept.
    const auto own = p.presetFolder().getChildFile ("Mine.andoranalog");
    REQUIRE (p.savePreset (own));
    ShellProcessor q;
    CHECK (own.existsAsFile());
    CHECK (q.presetList().size() == list.size() + 1);
}

TEST_CASE ("analog: the plugin plays exactly what the engine renders from the same patch", "[analog][engine]")
{
    ShellProcessor p;
    apply (p, factoryPatches()[1].patch);
    const auto shell = renderShell (p, 0.6, phrase, false);

    params::Reader reader;
    reader.attach (p.parameters);
    Synth synth;
    synth.prepare (rate, block, 4);
    synth.setPatch (reader.read());
    synth.reset();
    juce::AudioBuffer<float> direct (2, shell.getNumSamples());
    for (int pos = 0; pos < direct.getNumSamples(); pos += block)
    {
        synth.setPatch (reader.read());
        synth.setTransport ({});
        int done = pos;
        const int end = std::min (pos + block, direct.getNumSamples());
        for (const auto& n : phrase)
            if (n.sample >= pos && n.sample < end)
            {
                if (n.sample > done)
                    synth.render (direct.getWritePointer (0) + done, direct.getWritePointer (1) + done, n.sample - done);
                done = n.sample;
                const std::uint8_t bytes[3] { static_cast<std::uint8_t> (n.velocity > 0 ? 0x90 : 0x80), static_cast<std::uint8_t> (n.note),
                                              static_cast<std::uint8_t> (n.velocity) };
                synth.handleMidi (bytes, 3);
            }
        synth.render (direct.getWritePointer (0) + done, direct.getWritePointer (1) + done, end - done);
    }
    CHECK (identical (shell, direct));
}

TEST_CASE ("analog: a note starts on its own sample inside a block; the latency is reported", "[analog][engine]")
{
    ShellProcessor p;
    setValue (p, "space", 0.0f);
    const auto out = renderShell (p, 0.1, { { 300, 69, 127 } }, false);
    int first = -1;
    for (int i = 0; i < out.getNumSamples() && first < 0; ++i)
        if (std::abs (out.getSample (0, i)) > 1.0e-6f)
            first = i;
    CHECK (p.getLatencySamples() > 0);
    CHECK (p.getLatencySamples() < 64);
    CHECK (first >= 300);
    CHECK (first <= 300 + p.getLatencySamples() + 4);
}

TEST_CASE ("analog: a session recalls the sound exactly", "[analog][state]")
{
    juce::MemoryBlock session;
    juce::AudioBuffer<float> before;
    {
        ShellProcessor p;
        apply (p, factoryPatches()[4].patch);
        setValue (p, params::resonance, 47.0f);
        before = renderShell (p, 0.5);
        p.getStateInformation (session);
    }
    ShellProcessor q;
    q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
    CHECK (q.parameters.getRawParameterValue (params::resonance)->load() == Approx (47.0f));
    CHECK (identical (before, renderShell (q, 0.5)));
}

TEST_CASE ("analog: the card shows the oscillators, and its picture follows them", "[analog][editor]")
{
    ShellProcessor p;
    osp::plugin::InstrumentHost::GeneratedSource a, b;
    REQUIRE (p.generatedSource (0, a));
    CHECK (a.name == "OSCILLATORS");
    setValue (p, params::osc1Wave, 1.0f);   // pulse
    REQUIRE (p.generatedSource (0, b));
    CHECK (a.revision != b.revision);
    CHECK (a.cycle != b.cycle);
}

TEST_CASE ("analog: every control the interface enables moves the sound; every one it disables does not", "[analog][bindings]")
{
    // The context: everything that can be heard is in play (both oscillators with every
    // wave, Poly Mod on all three destinations, the LFO brought in, DRIVE, SPACE, ECHO).
    Patch context;
    context.osc1Wave = Osc1Wave::sawPulse;
    context.osc2Wave = Osc2Wave::all;
    context.polyModEnv = 0.3;
    context.polyModOsc2 = 0.25;
    context.polyModToPitch1 = context.polyModToWidth1 = context.polyModToCutoff = true;
    context.movement = 0.6;
    context.lfoCutoff = 6.0;
    context.drive = 0.3;
    context.space = 0.3;
    context.echo = 0.3;
    context.effects.echoTimeMs = 90.0;
    context.effects.echoDivision = 0;
    context.ampRelease = 0.08;
    context.ampSustain = 0.6;   // (at 100 % the decay has nowhere to go)
    // A few controls act only in a mode: they are tried in it.
    std::map<juce::String, std::function<void (Patch&)>> modes {
        { params::unisonDetune, [] (Patch& c) { c.unison = true; } },
        { "glide", [] (Patch& c) { c.voiceMode = VoiceMode::mono; } },
        { params::lfoDivision, [] (Patch& c) { c.lfoSync = true; } },
        { "echo.time", [] (Patch& c) { c.effects.echoSync = false; } },
    };

    const auto bound = params::engineParameterIds();
    ShellProcessor p;
    int checkedBound = 0, checkedUnbound = 0;
    for (auto* base : p.getParameters())
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (base);
        REQUIRE (param != nullptr);
        const auto id = param->getParameterID();
        INFO (id);
        auto patch = context;
        if (const auto mode = modes.find (id); mode != modes.end())
            mode->second (patch);
        p.resetSettings();
        apply (p, patch);
        const auto reference = renderShell (p, 0.6);

        // Another value: the far side of the range from where it is.
        const float now = param->getValue();
        float other = now < 0.5f ? 0.85f : 0.15f;
        if (id == params::polyphony)
            other = 0.0f;   // 1 voice (the chord is three notes)
        param->setValueNotifyingHost (other);
        const auto changed = renderShell (p, 0.6);
        if (bound.contains (id))
        {
            const double d = differenceRms (reference, changed);
            UNSCOPED_INFO (id << ": " << 20.0 * std::log10 (std::max (d, 1.0e-12)) << " dB");
            CHECK (d > 1.0e-5);
            ++checkedBound;
        }
        else
        {
            CHECK (identical (reference, changed));
            ++checkedUnbound;
        }
    }
    CHECK (checkedBound == bound.size() - 2);   // (the two wheels are not parameters)
    CHECK (checkedUnbound > 300);
}

TEST_CASE ("analog: screenshots of the instrument and its six popovers (ANALOG_SCREENSHOT_DIR)", "[.][screenshots]")
{
    const char* dir = std::getenv ("ANALOG_SCREENSHOT_DIR");
    REQUIRE (dir != nullptr);
    const juce::File out (dir);
    out.createDirectory();
    const auto before = osp::plugin::skin::look();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<andor::shell::ShellEditor&> (*editor);
    auto shoot = [&] (const juce::String& name, float scale = 1.0f) {
        ui.refreshNow();
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
        const auto file = out.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    editor->setSize (1448, 1086);
    shoot ("analog-init");
    shoot ("analog-init@2x", 2.0f);
    const char* names[] = { "blend", "drive", "tone", "motion", "space", "echo" };
    for (int m = 0; m < 6; ++m)
    {
        ui.openPopup (m);
        REQUIRE (ui.openPopupPanel() != nullptr);
        shoot (juce::String ("analog-popover-") + names[m]);
        ui.closePopup();
    }
    ui.openPopup (andor::shell::ShellEditor::advancedPopup);
    shoot ("analog-advanced");
    ui.closePopup();
    // A sound with character on the card: Sync Bite's picture, keys held.
    apply (p, factoryPatches()[8].patch);
    p.keyboardState.noteOn (1, 60, 0.8f);
    p.keyboardState.noteOn (1, 64, 0.8f);
    shoot ("analog-sync-bite-keys");
    editor->setSize (869, juce::roundToInt (869.0f * 1086.0f / 1448.0f));
    shoot ("analog-window-869");
    osp::plugin::skin::setLook (before);
}
