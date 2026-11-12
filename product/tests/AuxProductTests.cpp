// D / AUX (docs/aux): the product side - the strip under the cards (a view: shown and hidden
// from the main menu, kept with the session and never with a preset, the cards and the sound
// untouched), its modes, menu and rack, D's recording (loaded on the loader thread, kept in the
// sample store, missing handled), its MIDI lane and latency, and the screenshots.
#include "AnalogEngine.h"
#include "AnalogParameters.h"
#include "AuxUI.h"
#include "CeramicSkin.h"
#include "DropOwner.h"
#include "ShellEditor.h"
#include "ShellProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>

using andor::shell::ShellProcessor;
using andor::analog::AnalogEngine;
using andor::analog::AuxStripView;
namespace params = andor::analog::params;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int block = 512;

    void set (ShellProcessor& p, const juce::String& id, float v)
    {
        auto* param = p.parameters.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (v));
    }

    AnalogEngine& engineOf (ShellProcessor& p) { return dynamic_cast<AnalogEngine&> (p.productEngine()); }

    /** A held chord from the start, released at `releaseAt` seconds. */
    juce::AudioBuffer<float> render (ShellProcessor& p, double seconds, double releaseAt = 0.4)
    {
        p.prepareToPlay (rate, block);
        const int total = static_cast<int> (seconds * rate);
        const int off = static_cast<int> (releaseAt * rate);
        juce::AudioBuffer<float> out (2, total), buffer (2, block);
        for (int pos = 0; pos < total; pos += block)
        {
            juce::MidiBuffer midi;
            for (const int note : { 57, 64, 69 })
            {
                if (pos == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)), 0);
                if (off >= pos && off < pos + block)
                    midi.addEvent (juce::MidiMessage::noteOff (1, note), off - pos);
            }
            p.processBlock (buffer, midi);
            const int n = std::min (block, total - pos);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, buffer, ch, 0, n);
        }
        return out;
    }

    bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        if (a.getNumSamples() != b.getNumSamples())
            return false;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
                if (std::memcmp (a.getReadPointer (ch) + i, b.getReadPointer (ch) + i, sizeof (float)) != 0)
                    return false;
        return true;
    }

    double rms (const juce::AudioBuffer<float>& a, int from = 0, int to = -1)
    {
        to = to < 0 ? a.getNumSamples() : to;
        double sum = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = from; i < to; ++i)
                sum += static_cast<double> (a.getSample (ch, i)) * a.getSample (ch, i);
        return std::sqrt (sum / std::max (1, 2 * (to - from)));
    }

    /** A loop of ticks, 4 bars at 100 BPM, written as a WAV (the loader decodes and analyses it). */
    juce::File writeLoop (const juce::File& dir, const juce::String& name)
    {
        const auto file = dir.getChildFile (name);
        file.deleteFile();
        constexpr double beat = 60.0 / 100.0;
        const int length = static_cast<int> (std::lround (16 * beat * rate));
        juce::AudioBuffer<float> x (2, length);
        x.clear();
        for (int k = 0; k < 16; ++k)
        {
            const int first = static_cast<int> (std::lround (k * beat * rate));
            for (int i = 0; i < static_cast<int> (0.03 * rate) && first + i < length; ++i)
            {
                const double t = i / rate;
                const auto v = static_cast<float> ((k % 4 == 0 ? 0.8 : 0.5) * std::exp (-t / 0.006) * std::sin (2.0 * juce::MathConstants<double>::pi * 2000.0 * t));
                x.setSample (0, first + i, v);
                x.setSample (1, first + i, v);
            }
        }
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), rate, 2, 24, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();
        writer->writeFromAudioSampleBuffer (x, 0, length);
        return file;
    }

    struct Scratch
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("analog-aux-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        Scratch() { dir.createDirectory(); }
        ~Scratch() { dir.deleteRecursively(); }
    };

    andor::shell::ShellEditor& editorOf (std::unique_ptr<juce::AudioProcessorEditor>& e) { return dynamic_cast<andor::shell::ShellEditor&> (*e); }

    /** The strip's timer's work at once (no message loop in the tests). */
    void refreshStrip (andor::shell::ShellEditor& ui)
    {
        ui.refreshNow();
        if (auto* strip = ui.auxStripComponent())
            AuxStripView::refresh (*strip);
    }
}

TEST_CASE ("AUX strip: shown and hidden from the main menu; the cards stay, what is under them moves, the sound never changes", "[analog][aux][ui]")
{
    ShellProcessor p;
    REQUIRE (p.hasAuxStrip());
    CHECK_FALSE (p.auxStripShown());   // (hidden by default: the instrument as before)
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = editorOf (editor);
    ui.refreshNow();
    CHECK (ui.auxStripComponent() == nullptr);
    const auto cards = ui.engineCard (0).getBounds();
    const auto cardC = ui.engineCard (1).getBounds();
    const int modY = ui.modulationPanel().getY();
    const float height = ui.instrumentHeight();
    const auto before = render (p, 0.5);

    ui.setAuxStripShown (true);
    ui.refreshNow();
    REQUIRE (ui.auxStripComponent() != nullptr);
    CHECK (ui.auxStripComponent()->isVisible());
    CHECK (p.auxStripShown());
    const float shift = ui.auxShift();
    CHECK (shift == Catch::Approx (76.0f + 18.0f));
    CHECK (ui.engineCard (0).getBounds() == cards);   // (the cards keep their places and size)
    CHECK (ui.engineCard (1).getBounds() == cardC);
    CHECK (ui.modulationPanel().getY() == modY + juce::roundToInt (shift));
    CHECK (ui.instrumentHeight() == Catch::Approx (height + shift));
    const auto strip = ui.auxStripComponent()->getBounds();
    CHECK (strip.getY() >= ui.engineCard (0).getBottom() + 12);
    CHECK (strip.getBottom() <= ui.modulationPanel().getY());
    CHECK (strip.getHeight() == 76);
    CHECK (identical (before, render (p, 0.5)));   // (a view: nothing sounds different)

    // Its own ... menu: Hide AUX, Open / Close AUX Rack, Reset.
    CHECK (AuxStripView::menuItems (*ui.auxStripComponent()) == juce::StringArray ({ "Hide AUX", "Open AUX Rack", "Reset" }));
    AuxStripView::runMenuItem (*ui.auxStripComponent(), "Hide AUX");
    ui.refreshNow();
    CHECK_FALSE (p.auxStripShown());
    CHECK_FALSE (ui.auxStripComponent()->isVisible());
    CHECK (ui.modulationPanel().getY() == modY);   // (every place exactly back)
    CHECK (ui.instrumentHeight() == Catch::Approx (height));
    CHECK (identical (before, render (p, 0.5)));
}

TEST_CASE ("AUX strip: hiding it never mutes D or changes its sound", "[analog][aux][ui]")
{
    ShellProcessor p;
    set (p, params::auxEnabled, 1.0f);
    set (p, params::auxMode, 1.0f);   // NOISE
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = editorOf (editor);
    ui.setAuxStripShown (true);
    const auto shown = render (p, 0.4);
    ui.setAuxStripShown (false);
    const auto hidden = render (p, 0.4);
    CHECK (rms (shown) > 1.0e-3);
    CHECK (identical (shown, hidden));
    CHECK (p.parameters.getParameter (params::auxEnabled)->getValue() >= 0.5f);
}

TEST_CASE ("AUX strip: its visibility is kept with the session, never with a preset", "[analog][aux][ui]")
{
    Scratch scratch;
    ShellProcessor p;
    p.setAuxStripShown (true);
    juce::MemoryBlock session;
    p.getStateInformation (session);
    const auto preset = scratch.dir.getChildFile ("shown.andoranalog");
    REQUIRE (p.savePreset (preset));

    ShellProcessor q;
    q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
    CHECK (q.auxStripShown());   // (a session: the view as it was left)

    ShellProcessor r;
    CHECK_FALSE (r.auxStripShown());
    REQUIRE (r.loadPreset (preset));
    CHECK_FALSE (r.auxStripShown());   // (a preset never shows it ...)
    r.setAuxStripShown (true);
    p.setAuxStripShown (false);
    const auto hiddenPreset = scratch.dir.getChildFile ("hidden.andoranalog");
    REQUIRE (p.savePreset (hiddenPreset));
    REQUIRE (r.loadPreset (hiddenPreset));
    CHECK (r.auxStripShown());   // (... nor hides it)
}

TEST_CASE ("AUX strip: the mode's knobs, the C-style mode dropdown, the rack from a double-click and Escape", "[analog][aux][ui]")
{
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = editorOf (editor);
    ui.setAuxStripShown (true);
    auto& strip = *ui.auxStripComponent();
    for (const auto& name : { "power", "mute", "solo", "mode", "display", "rack", "menu" })
        CHECK (AuxStripView::part (strip, name) != nullptr);
    CHECK (AuxStripView::knobCaptions (strip) == juce::StringArray ({ "RATE", "PITCH", "OCT", "MOD AMOUNT", "LEVEL" }));
    CHECK (AuxStripView::part (strip, "route") == nullptr);   // (Addendum 03: no ROUTE, no LOAD)
    CHECK (AuxStripView::part (strip, "load") == nullptr);
    set (p, params::auxMode, 1.0f);
    ui.refreshNow();
    refreshStrip (ui);
    CHECK (AuxStripView::shownMode (strip) == 1);
    CHECK (AuxStripView::knobCaptions (strip) == juce::StringArray ({ "COLOR", "TONE", "LEVEL" }));
    set (p, params::auxMode, 2.0f);
    refreshStrip (ui);
    CHECK (AuxStripView::knobCaptions (strip) == juce::StringArray ({ "LEVEL" }));

    const float height = ui.instrumentHeight();
    AuxStripView::doubleClick (strip);
    ui.refreshNow();
    CHECK (ui.effectsRack().isShown (osp::plugin::rack::Kind::aux));
    CHECK (ui.instrumentHeight() > height);
    CHECK (AuxStripView::menuItems (strip).contains ("Close AUX Rack"));
    CHECK (ui.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    ui.refreshNow();
    CHECK_FALSE (ui.effectsRack().isShown (osp::plugin::rack::Kind::aux));
    CHECK (ui.instrumentHeight() == Catch::Approx (height));
    CHECK (p.auxStripShown());   // (Escape closed one level: the rack, not the strip)

    // Hiding the strip takes its rack with it.
    AuxStripView::runMenuItem (strip, "Open AUX Rack");
    ui.refreshNow();
    CHECK (ui.effectsRack().isShown (osp::plugin::rack::Kind::aux));
    ui.setAuxStripShown (false);
    CHECK_FALSE (ui.effectsRack().isShown (osp::plugin::rack::Kind::aux));
}

TEST_CASE ("AUX strip: Reset returns D's parameters to their defaults (one undoable step)", "[analog][aux][ui]")
{
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = editorOf (editor);
    ui.setAuxStripShown (true);
    set (p, params::auxEnabled, 1.0f);
    set (p, params::auxMode, 1.0f);
    set (p, params::auxLevel, 0.0f);
    AuxStripView::runMenuItem (*ui.auxStripComponent(), "Reset");
    for (const auto& id : { params::auxEnabled, params::auxMode, params::auxLevel })
    {
        auto* param = p.parameters.getParameter (id);
        CHECK (param->getValue() == Catch::Approx (param->getDefaultValue()));
    }
}

TEST_CASE ("AUX: D RHYTHM's recording loads on the loader thread, is kept in the store, plays, and comes back with the session", "[analog][aux]")
{
    Scratch scratch;
    ShellProcessor p;
    auto& engine = engineOf (p);
    const auto file = writeLoop (scratch.dir, "ticks.wav");
    const auto store = p.sampleStoreFolder().getChildFile ("Imported");
    engine.loadAuxRecording (file, store);
    REQUIRE (engine.waitForAuxRecording (20000));
    const auto state = engine.auxRecordingState();
    CHECK (state.status == AnalogEngine::AuxRecordingState::Status::ready);
    CHECK (state.name == "ticks");
    REQUIRE (engine.auxLoop() != nullptr);
    CHECK (engine.auxLoop()->rhythmic());
    CHECK (engine.auxLoop()->analysis.bpm == Catch::Approx (100.0).margin (1.0));

    set (p, params::auxEnabled, 1.0f);
    set (p, params::auxMode, 0.0f);
    const auto out = render (p, 1.2);
    CHECK (rms (out) > 1.0e-3);

    // The session keeps the stored file; another instance plays it again.
    juce::MemoryBlock session;
    p.getStateInformation (session);
    ShellProcessor q;
    q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
    auto& other = engineOf (q);
    REQUIRE (other.waitForAuxRecording (20000));
    CHECK (other.auxRecordingState().status == AnalogEngine::AuxRecordingState::Status::ready);
    CHECK (other.auxLoop() != nullptr);
    CHECK (rms (render (q, 1.2)) > 1.0e-3);
}

TEST_CASE ("AUX: a missing or unreadable recording fails beautifully and is written back as it was", "[analog][aux]")
{
    Scratch scratch;
    ShellProcessor p;
    auto& engine = engineOf (p);
    // Not audio at all.
    const auto junk = scratch.dir.getChildFile ("junk.wav");
    junk.replaceWithText ("not audio");
    engine.loadAuxRecording (junk, scratch.dir.getChildFile ("store"));
    REQUIRE (engine.waitForAuxRecording (20000));
    // (Addendum 03: a failed load leaves D as it was - here without a loop - and says why)
    CHECK (engine.auxRecordingState().status == AnalogEngine::AuxRecordingState::Status::none);
    CHECK (engine.auxRecordingState().lastError.isNotEmpty());
    CHECK (engine.auxLoop() == nullptr);
    set (p, params::auxEnabled, 1.0f);
    CHECK (rms (render (p, 0.3)) >= 0.0);   // (plays on: D silent, nothing crashes)

    // A session whose stored file is gone.
    juce::XmlElement state ("STATE");
    auto* rec = state.createNewChildElement ("AuxRecording");
    rec->setAttribute ("asset", "0123456789abcdef01234567.wav");
    rec->setAttribute ("name", "gone");
    engine.restoreState (state, scratch.dir);
    REQUIRE (engine.waitForAuxRecording (20000));
    CHECK (engine.auxRecordingState().status == AnalogEngine::AuxRecordingState::Status::missing);
    juce::XmlElement saved ("STATE");
    engine.saveState (saved);
    const auto* back = saved.getChildByName ("AuxRecording");
    REQUIRE (back != nullptr);
    CHECK (back->getStringAttribute ("asset") == "0123456789abcdef01234567.wav");
    CHECK (back->getStringAttribute ("name") == "gone");
}

TEST_CASE ("AUX: D off and no Kontakt add no latency; the strip's and D's state are absent from a session without D", "[analog][aux]")
{
    ShellProcessor p;
    auto& engine = engineOf (p);
    p.prepareToPlay (rate, block);
    const int base = engine.latencySamples();
    set (p, params::auxEnabled, 1.0f);
    set (p, params::auxMode, 2.0f);   // KONTAKT, none loaded
    CHECK (engine.latencySamples() == base);
    CHECK (engine.auxInstrument().state() == andor::analog::ExternalInstrument::State::empty);
    juce::XmlElement state ("STATE");
    engine.saveState (state);
    CHECK (state.getChildByName ("AuxRecording") == nullptr);
    CHECK (state.getChildByName ("AuxKontakt") == nullptr);
}

TEST_CASE ("AUX: D NOISE follows the musician's keys before the arpeggiator", "[analog][aux]")
{
    ShellProcessor p;
    set (p, params::auxEnabled, 1.0f);
    set (p, params::auxMode, 1.0f);    // NOISE, gated by the keys
    set (p, params::auxLevel, 0.0f);
    // Only D heard: the cards muted.
    set (p, params::auxSolo, 1.0f);
    const auto out = render (p, 1.0, 0.4);
    const int at = static_cast<int> (0.2 * rate);
    const int late = static_cast<int> (0.9 * rate);
    const double held = rms (out, at, at + 4800);
    const double after = rms (out, late, late + 4800);
    CHECK (held > 1.0e-3);
    CHECK (after < 0.1 * held);   // (released: the gate closed, its release passed)
}

TEST_CASE ("AUX: screenshots - the strip in its three modes, its rack, hidden (ANALOG_SCREENSHOT_DIR)", "[.][screenshots][aux]")
{
    const auto dir = juce::SystemStats::getEnvironmentVariable ("ANALOG_SCREENSHOT_DIR", {});
    REQUIRE (dir.isNotEmpty());
    const juce::File out (dir);
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    ShellProcessor p;
    engineOf (p).setAuxLoopForTest ([] {
        constexpr double beat = 60.0 / 100.0;
        const auto length = static_cast<std::size_t> (std::lround (16 * beat * rate));
        std::vector<float> x (length, 0.0f);
        for (int k = 0; k < 16; ++k)
        {
            const auto first = static_cast<std::size_t> (std::lround (k * beat * rate));
            for (std::size_t i = 0; i < static_cast<std::size_t> (0.06 * rate) && first + i < length; ++i)
                x[first + i] += static_cast<float> ((k % 4 == 0 ? 0.8 : 0.45) * std::exp (-static_cast<double> (i) / rate / 0.02));
        }
        return andor::analog::rhythm::prepareLoop (x.data(), x.data(), x.size(), rate);
    }());
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = editorOf (editor);
    auto shoot = [&] (const juce::String& name, int width) {
        const float h = ui.instrumentHeight() * static_cast<float> (width) / osp::plugin::design::width;
        editor->setSize (width, juce::roundToInt (h));
        refreshStrip (ui);
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = out.getChildFile (name + "-" + juce::String (width) + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    for (const int w : { 869, 1448, 1810 })
        shoot ("aux-00-hidden", w);
    ui.setAuxStripShown (true);
    set (p, params::auxEnabled, 1.0f);
    const char* names[] = { "rhythm", "noise", "kontakt" };
    for (int mode = 0; mode < 3; ++mode)
    {
        set (p, params::auxMode, static_cast<float> (mode));
        refreshStrip (ui);
        for (const int w : { 869, 1448, 1810 })
            shoot (juce::String ("aux-0") + juce::String (mode + 1) + "-" + names[mode], w);
    }
    set (p, params::auxMode, 0.0f);
    refreshStrip (ui);
    ui.setRackExpanded (osp::plugin::rack::Kind::aux, true);
    for (const int w : { 869, 1448, 1810 })
        shoot ("aux-04-rack-rhythm", w);
    set (p, params::auxMode, 1.0f);
    refreshStrip (ui);
    shoot ("aux-05-rack-noise", 1448);
    set (p, params::auxMode, 2.0f);
    refreshStrip (ui);
    shoot ("aux-06-rack-kontakt", 1448);
    ui.setRackExpanded (osp::plugin::rack::Kind::aux, false);
    // A loop dragged over D's strip: the well shows where it will land (only D's).
    set (p, params::auxMode, 0.0f);
    if (auto* owner = dynamic_cast<osp::plugin::DropOwner*> (ui.auxStripComponent()))
    {
        owner->dropHover ({ "loop.wav" }, true);
        shoot ("aux-08-drop-hover", 1448);
        owner->dropHover ({ "loop.wav" }, false);
    }
    // Muted, and another card soloed (D not heard: the cards' veil and reasons).
    set (p, params::auxMute, 1.0f);
    shoot ("aux-09-muted", 1448);
    set (p, params::auxMute, 0.0f);
    set (p, "layerA.solo", 1.0f);
    shoot ("aux-10-not-soloed", 1448);
    set (p, "layerA.solo", 0.0f);
    set (p, params::auxSolo, 1.0f);
    shoot ("aux-11-soloed", 1448);
    set (p, params::auxSolo, 0.0f);
    set (p, params::auxEnabled, 0.0f);
    shoot ("aux-07-off", 1448);
}

TEST_CASE ("AUX: renders - D under a FILTER sweep, through SPACE / ECHO, with card C, and D off nulling (ANALOG_RENDER_DIR)", "[.][renders][aux]")
{
    const auto dir = juce::SystemStats::getEnvironmentVariable ("ANALOG_RENDER_DIR", {});
    REQUIRE (dir.isNotEmpty());
    const juce::File out (dir);
    out.createDirectory();
    auto write = [&] (const juce::String& name, const juce::AudioBuffer<float>& audio) {
        const auto file = out.getChildFile (name + ".wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), rate, 2, 24, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();
        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    };
    auto peakDb = [] (const juce::AudioBuffer<float>& a) { return juce::Decibels::gainToDecibels (std::max (a.getMagnitude (0, a.getNumSamples()), 1.0e-9f)); };
    auto rmsDb = [] (const juce::AudioBuffer<float>& a, int from, int to) { return juce::Decibels::gainToDecibels (static_cast<float> (std::max (rms (a, from, to), 1.0e-9))); };
    const auto tickLoop = [] {
        constexpr double beat = 60.0 / 100.0;
        const auto length = static_cast<std::size_t> (std::lround (16 * beat * rate));
        std::vector<float> x (length, 0.0f);
        juce::Random noise (7);
        for (int k = 0; k < 16; ++k)
        {
            const auto first = static_cast<std::size_t> (std::lround (k * beat * rate));
            for (std::size_t i = 0; i < static_cast<std::size_t> (0.08 * rate) && first + i < length; ++i)
                x[first + i] += static_cast<float> ((k % 4 == 0 ? 0.7 : 0.4) * std::exp (-static_cast<double> (i) / rate / 0.015) * (noise.nextFloat() * 2.0f - 1.0f));
        }
        return andor::analog::rhythm::prepareLoop (x.data(), x.data(), x.size(), rate);
    }();

    // 1. D NOISE alone (soloed), held, the FILTER macro swept from closed to open over 4 s.
    {
        ShellProcessor p;
        set (p, params::auxEnabled, 1.0f);
        set (p, params::auxMode, 1.0f);
        set (p, params::auxNoiseGate, 1.0f);   // FREE
        set (p, params::auxSolo, 1.0f);
        set (p, params::auxLevel, 0.0f);
        p.prepareToPlay (rate, block);
        const int total = static_cast<int> (4.0 * rate);
        juce::AudioBuffer<float> audio (2, total), buffer (2, block);
        auto* tone = p.parameters.getParameter (params::tone);
        for (int pos = 0; pos < total; pos += block)
        {
            tone->setValueNotifyingHost (0.1f + 0.85f * static_cast<float> (pos) / static_cast<float> (total));
            juce::MidiBuffer none;
            p.processBlock (buffer, none);
            const int n = std::min (block, total - pos);
            for (int ch = 0; ch < 2; ++ch)
                audio.copyFrom (ch, pos, buffer, ch, 0, n);
        }
        write ("aux-01-noise-filter-sweep", audio);
        const float early = rmsDb (audio, static_cast<int> (0.2 * rate), static_cast<int> (0.7 * rate));
        const float late = rmsDb (audio, static_cast<int> (3.3 * rate), static_cast<int> (3.8 * rate));
        WARN ("FILTER sweep on D NOISE: " << early << " dB (closed) -> " << late << " dB (open), peak " << peakDb (audio) << " dBFS");
        CHECK (late > early + 6.0f);
    }
    // 2. D RHYTHM through SPACE and ECHO (macros up), soloed: the effects carry it.
    {
        ShellProcessor p;
        engineOf (p).setAuxLoopForTest (tickLoop);
        set (p, params::auxEnabled, 1.0f);
        set (p, params::auxMode, 0.0f);
        set (p, params::auxSolo, 1.0f);
        set (p, "space", 70.0f);
        set (p, "echo", 60.0f);
        const auto audio = render (p, 6.0, 5.0);
        write ("aux-02-rhythm-space-echo", audio);
        WARN ("D RHYTHM + SPACE / ECHO: " << rmsDb (audio, 0, audio.getNumSamples()) << " dB RMS, peak " << peakDb (audio) << " dBFS");
    }
    // 3. Cards A / B / C (C NOISE) and D RHYTHM together.
    {
        ShellProcessor p;
        engineOf (p).setAuxLoopForTest (tickLoop);
        set (p, params::hybridType, 2.0f);
        set (p, params::auxEnabled, 1.0f);
        set (p, params::auxMode, 0.0f);
        const auto audio = render (p, 6.0, 5.0);
        write ("aux-03-cards-and-d", audio);
        WARN ("A/B/C + D: peak " << peakDb (audio) << " dBFS");
    }
    // 4. D off (never powered) against the instrument with D merely configured but off: null.
    {
        ShellProcessor a, b;
        engineOf (b).setAuxLoopForTest (tickLoop);
        set (b, params::auxMode, 1.0f);
        set (b, params::auxLevel, 6.0f);
        const auto x = render (a, 2.0), y = render (b, 2.0);
        juce::AudioBuffer<float> diff (2, x.getNumSamples());
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < x.getNumSamples(); ++i)
                diff.setSample (ch, i, x.getSample (ch, i) - y.getSample (ch, i));
        write ("aux-04-legacy-null-difference", diff);
        WARN ("D off null: max difference " << diff.getMagnitude (0, diff.getNumSamples()));
        CHECK (identical (x, y));
    }
}


//==============================================================================
// AUX Refinement Addendum 03.

namespace
{
    /** A drop as the host delivers it: on the editor, at a point of `target` (its centre by
        default), through the editor's own drop handling (the overlays above the parts and all). */
    void dropOn (andor::shell::ShellEditor& ui, juce::Component& target, const juce::File& file, juce::Point<int> at = { -1, -1 })
    {
        if (at.x < 0)
            at = target.getLocalBounds().getCentre();
        const auto p = ui.getLocalPoint (&target, at);
        const juce::StringArray files { file.getFullPathName() };
        REQUIRE (ui.isInterestedInFileDrag (files));
        ui.fileDragEnter (files, p.x, p.y);
        ui.fileDragMove (files, p.x, p.y);
        ui.filesDropped (files, p.x, p.y);
    }

    juce::String auxName (ShellProcessor& p) { return engineOf (p).auxRecordingState().name; }

    struct DropRig
    {
        Scratch scratch;
        ShellProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor { p.createEditorIfNeeded() };
        andor::shell::ShellEditor& ui = editorOf (editor);
        juce::File loopA = writeLoop (scratch.dir, "aux-loop.wav"), loopB = writeLoop (scratch.dir, "card-c-loop.wav");
        DropRig()
        {
            editor->setSize (1448, 1086);
            ui.setAuxStripShown (true);
            ui.refreshNow();
        }
        bool cardC() { p.waitForLoads (20000); p.pollLoads(); return p.currentInstrument (1) != nullptr; }
    };
}

TEST_CASE ("AUX 03 gate 1: a file dropped on D (strip, its well, the rack) loads D only; one dropped on card C loads C only", "[analog][aux][drop]")
{
    DropRig rig;
    auto& strip = *rig.ui.auxStripComponent();
    auto& well = *AuxStripView::part (strip, "display");
    // 1. The strip's well: D, never C.
    dropOn (rig.ui, well, rig.loopA);
    REQUIRE (engineOf (rig.p).waitForAuxRecording (20000));
    CHECK (auxName (rig.p) == "aux-loop");
    CHECK_FALSE (rig.cardC());
    // 2. Elsewhere on the strip (beside its knobs): D's too.
    dropOn (rig.ui, strip, rig.loopB, { strip.getWidth() - 4, 4 });
    REQUIRE (engineOf (rig.p).waitForAuxRecording (20000));
    CHECK (auxName (rig.p) == "card-c-loop");
    CHECK_FALSE (rig.cardC());
    // 3. The AUX rack (its graph and the rest of it): D.
    rig.ui.setRackExpanded (osp::plugin::rack::Kind::aux, true);
    rig.ui.refreshNow();
    juce::Component* rack = nullptr;
    std::function<void (juce::Component&)> find = [&] (juce::Component& c) {
        for (auto* child : c.getChildren())
        {
            if (dynamic_cast<osp::plugin::DropOwner*> (child) != nullptr && child->isVisible())
                rack = child;
            find (*child);
        }
    };
    find (rig.ui.effectsRack());
    REQUIRE (rack != nullptr);
    dropOn (rig.ui, *rack, rig.loopA);
    REQUIRE (engineOf (rig.p).waitForAuxRecording (20000));
    CHECK (auxName (rig.p) == "aux-loop");
    CHECK_FALSE (rig.cardC());
    // 4. Card C's well: C, and D unchanged.
    rig.ui.setRackExpanded (osp::plugin::rack::Kind::aux, false);
    rig.ui.refreshNow();
    dropOn (rig.ui, rig.ui.engineCard (1).display(), rig.loopB);
    CHECK (rig.cardC());
    CHECK (auxName (rig.p) == "aux-loop");
    // 5. Hidden and shown again, the rack opened and closed: the same ownership.
    rig.ui.setAuxStripShown (false);
    rig.ui.setAuxStripShown (true);
    rig.ui.refreshNow();
    dropOn (rig.ui, *AuxStripView::part (*rig.ui.auxStripComponent(), "display"), rig.loopB);
    REQUIRE (engineOf (rig.p).waitForAuxRecording (20000));
    CHECK (auxName (rig.p) == "card-c-loop");
    // 6. Saved and reloaded: each its own.
    juce::MemoryBlock session;
    rig.p.getStateInformation (session);
    ShellProcessor q;
    q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
    REQUIRE (engineOf (q).waitForAuxRecording (20000));
    CHECK (auxName (q) == "card-c-loop");
    q.waitForLoads (20000);
    q.pollLoads();
    CHECK (q.currentInstrument (1) != nullptr);
}

TEST_CASE ("AUX 03 gate 5: an unsupported file dropped on D changes nothing and says why; a failed load keeps the loop in use", "[analog][aux][drop]")
{
    DropRig rig;
    auto& well = *AuxStripView::part (*rig.ui.auxStripComponent(), "display");
    dropOn (rig.ui, well, rig.loopA);
    REQUIRE (engineOf (rig.p).waitForAuxRecording (20000));
    const auto before = engineOf (rig.p).auxLoop();
    REQUIRE (before != nullptr);
    // Not audio at all (a text file): refused by D, never handed to C.
    const auto text = rig.scratch.dir.getChildFile ("notes.txt");
    text.replaceWithText ("hello");
    dropOn (rig.ui, well, text);
    CHECK (engineOf (rig.p).auxLoop() == before);
    CHECK (auxName (rig.p) == "aux-loop");
    CHECK_FALSE (rig.cardC());
    // A .wav that is not one: the load fails; the loop in use plays on, the reason is kept.
    const auto broken = rig.scratch.dir.getChildFile ("broken.wav");
    broken.replaceWithText ("not audio");
    dropOn (rig.ui, well, broken);
    REQUIRE (engineOf (rig.p).waitForAuxRecording (20000));
    CHECK (engineOf (rig.p).auxLoop() == before);
    CHECK (auxName (rig.p) == "aux-loop");
    CHECK (engineOf (rig.p).auxRecordingState().status == AnalogEngine::AuxRecordingState::Status::ready);
    CHECK (engineOf (rig.p).auxRecordingState().lastError.isNotEmpty());
    CHECK_FALSE (rig.cardC());
}

TEST_CASE ("AUX 03 gates 3-4: the strip's zones align to the cards - B's well exactly; M / S are the cards' keys; RACK beside the menu", "[analog][aux][ui]")
{
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto& ui = editorOf (editor);
    editor->setSize (1448, 1086);
    ui.setAuxStripShown (true);
    ui.refreshNow();
    auto& strip = *ui.auxStripComponent();
    auto* well = AuxStripView::part (strip, "display");
    const auto wellB = ui.cardDisplayBounds (2);
    const auto wellD = well->getBounds() + strip.getPosition();
    CHECK (wellD.getX() == wellB.getX());
    CHECK (wellD.getWidth() == wellB.getWidth());
    // M / S: the cards' own key (osp::plugin::LayerStateButton), the cards' size.
    auto* mute = dynamic_cast<osp::plugin::LayerStateButton*> (AuxStripView::part (strip, "mute"));
    auto* solo = dynamic_cast<osp::plugin::LayerStateButton*> (AuxStripView::part (strip, "solo"));
    REQUIRE (mute != nullptr);
    REQUIRE (solo != nullptr);
    CHECK (mute->getWidth() == 27);
    CHECK (mute->getHeight() == 21);
    set (p, params::auxMute, 1.0f);
    CHECK (mute->isOn());
    set (p, params::auxSolo, 1.0f);
    CHECK (solo->isOn());
    // RACK: at the right end, immediately left of the menu; not over the well.
    auto* rack = AuxStripView::part (strip, "rack");
    auto* menu = AuxStripView::part (strip, "menu");
    CHECK (rack->getRight() < menu->getX());
    CHECK (menu->getX() - rack->getRight() <= 10);
    CHECK (rack->getX() > well->getRight());
    CHECK_FALSE (well->getBounds().intersects (rack->getBounds()));
    // The knobs in C's zone, before RACK, none overlapping it.
    for (const auto& caption : AuxStripView::knobCaptions (strip))
    {
        auto* k = AuxStripView::part (strip, caption);
        CHECK (k->getX() >= well->getRight());
        CHECK (k->getRight() <= rack->getX());
    }
    // The mode selector: top left of the well, the same left edge in every mode.
    std::vector<int> lefts;
    for (int mode = 0; mode < 3; ++mode)
    {
        set (p, params::auxMode, static_cast<float> (mode));
        refreshStrip (ui);
        auto* selector = AuxStripView::part (strip, "mode");
        lefts.push_back (selector->getX());
        CHECK (selector->getX() == well->getX() + 12);
        CHECK (selector->getY() == well->getY() + 6);
    }
    CHECK (lefts[0] == lefts[1]);
    CHECK (lefts[1] == lefts[2]);
}

TEST_CASE ("AUX 03 gate 2: D always goes through the FILTER - the old switch is unread and hidden; ROUTE is gone", "[analog][aux]")
{
    ShellProcessor p;
    set (p, params::auxEnabled, 1.0f);
    set (p, params::auxMode, 1.0f);
    set (p, params::auxNoiseGate, 1.0f);
    set (p, params::auxSolo, 1.0f);
    set (p, params::tone, 10.0f);
    const auto closed = render (p, 0.5);
    set (p, params::tone, 95.0f);
    const auto open = render (p, 0.5);
    CHECK (rms (open, 12000, 24000) > 3.0 * rms (closed, 12000, 24000));   // (the FILTER macro moves D)
    // An older experimental session with AROUND: through the FILTER now, as every other.
    set (p, params::tone, 10.0f);
    set (p, params::auxFilterFollow, 0.0f);
    CHECK (identical (closed, render (p, 0.5)));
    CHECK_FALSE (params::engineParameterIds().contains (params::auxFilterFollow));   // (not offered to a host)
}

TEST_CASE ("AUX 03 gate 6: PITCH and OCT are host parameters at 0; OCT +1 is PITCH +12; they move D RHYTHM", "[analog][aux][pitch]")
{
    const auto loop = [] {
        std::vector<float> x (static_cast<std::size_t> (2.0 * rate), 0.0f);
        for (int k = 0; k < 4; ++k)
            for (std::size_t i = 0; i < static_cast<std::size_t> (0.03 * rate); ++i)
                x[static_cast<std::size_t> (k * 0.5 * rate) + i] = static_cast<float> (0.6 * std::exp (-static_cast<double> (i) / rate / 0.006)
                                                                                         * std::sin (2.0 * juce::MathConstants<double>::pi * 1500.0 * static_cast<double> (i) / rate));
        return andor::analog::rhythm::prepareLoop (x.data(), x.data(), x.size(), rate);
    }();
    ShellProcessor a, b, c;
    for (auto* p : { &a, &b, &c })
    {
        engineOf (*p).setAuxLoopForTest (loop);
        set (*p, params::auxEnabled, 1.0f);
        set (*p, params::auxMode, 0.0f);
        set (*p, params::auxSolo, 1.0f);
    }
    REQUIRE (a.parameters.getParameter (params::auxPitch) != nullptr);
    REQUIRE (a.parameters.getParameter (params::auxOctave) != nullptr);
    CHECK (a.parameters.getParameter (params::auxPitch)->getDefaultValue() == Catch::Approx (0.5f));   // (0 st)
    CHECK (a.parameters.getParameter (params::auxOctave)->getDefaultValue() == Catch::Approx (0.5f));  // (0 oct)
    set (a, params::auxPitch, 12.0f);
    set (b, params::auxOctave, 1.0f);
    const auto up = render (a, 1.2);
    CHECK (identical (up, render (b, 1.2)));   // (one transposition, however it is set)
    CHECK_FALSE (identical (up, render (c, 1.2)));
    CHECK (params::engineParameterIds().contains (params::auxPitch));
    CHECK (params::engineParameterIds().contains (params::auxOctave));
}

TEST_CASE ("AUX 03 renders: D RHYTHM at PITCH -12 / 0 / +12, OCT -1 / +1, RATE 2x with PITCH +5, and PITCH automated (ANALOG_RENDER_DIR)", "[.][renders][aux03]")
{
    const auto dir = juce::SystemStats::getEnvironmentVariable ("ANALOG_RENDER_DIR", {});
    REQUIRE (dir.isNotEmpty());
    const juce::File out (dir);
    out.createDirectory();
    // A 100 BPM loop: a tonal hit on each beat (a 440 Hz pluck), a noise tick on the off-beats.
    const auto loop = [] {
        constexpr double beat = 0.6;
        std::vector<float> x (static_cast<std::size_t> (8 * beat * rate), 0.0f);
        juce::Random noise (3);
        for (int k = 0; k < 16; ++k)
        {
            const auto first = static_cast<std::size_t> (k * beat * 0.5 * rate);
            for (std::size_t i = 0; i < static_cast<std::size_t> (0.25 * rate) && first + i < x.size(); ++i)
            {
                const double t = static_cast<double> (i) / rate;
                x[first + i] += static_cast<float> (k % 2 == 0 ? 0.5 * std::exp (-t / 0.08) * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * t)
                                                              : 0.25 * std::exp (-t / 0.01) * (noise.nextFloat() * 2.0f - 1.0f));
            }
        }
        return andor::analog::rhythm::prepareLoop (x.data(), x.data(), x.size(), rate);
    }();
    auto renderWith = [&] (const juce::String& name, float pitch, float octave, float rateIndex, bool automate) {
        ShellProcessor p;
        engineOf (p).setAuxLoopForTest (loop);
        set (p, params::auxEnabled, 1.0f);
        set (p, params::auxMode, 0.0f);
        set (p, params::auxSolo, 1.0f);
        set (p, params::auxLevel, 0.0f);
        set (p, params::tone, 95.0f);
        set (p, params::auxPitch, pitch);
        set (p, params::auxOctave, octave);
        set (p, params::auxRate, rateIndex);
        p.prepareToPlay (rate, block);
        const int total = static_cast<int> (6.0 * rate);
        juce::AudioBuffer<float> audio (2, total), buffer (2, block);
        for (int pos = 0; pos < total; pos += block)
        {
            if (automate)
                set (p, params::auxPitch, -12.0f + 24.0f * static_cast<float> (pos) / static_cast<float> (total));
            juce::MidiBuffer none;
            p.processBlock (buffer, none);
            const int n = std::min (block, total - pos);
            for (int ch = 0; ch < 2; ++ch)
                audio.copyFrom (ch, pos, buffer, ch, 0, n);
        }
        const auto file = out.getChildFile (name + ".wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), rate, 2, 24, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();
        writer->writeFromAudioSampleBuffer (audio, 0, total);
        // Where the beats start (above -30 dBFS after 40 ms of quiet): the grid, whatever the pitch.
        std::vector<double> starts;
        int quiet = 1 << 30;
        for (int i = 0; i < total; ++i)
        {
            const float v = std::abs (audio.getSample (0, i));
            if (v > 0.03f && quiet > static_cast<int> (0.04 * rate))
                starts.push_back (i / rate);
            quiet = v > 0.03f ? 0 : quiet + 1;
        }
        juce::String line;
        for (std::size_t k = 0; k < std::min<std::size_t> (6, starts.size()); ++k)
            line << juce::String (starts[k], 3) << " ";
        WARN (name << ": peak " << juce::Decibels::gainToDecibels (audio.getMagnitude (0, total)) << " dBFS; first hits at " << line << "s");
        return starts;
    };
    const auto plain = renderWith ("aux03-01-pitch-0", 0.0f, 0.0f, 2.0f, false);
    for (const auto& [name, pitch, octave] : { std::tuple<const char*, float, float> { "aux03-02-pitch-minus12", -12.0f, 0.0f },
                                               { "aux03-03-pitch-plus12", 12.0f, 0.0f },
                                               { "aux03-04-oct-minus1", 0.0f, -1.0f },
                                               { "aux03-05-oct-plus1", 0.0f, 1.0f } })
    {
        const auto starts = renderWith (name, pitch, octave, 2.0f, false);
        REQUIRE (starts.size() >= 3);
        // Every hit heard lands on the unpitched loop's grid (a slower slice's tail may hide the
        // quiet before the next hit from this detector - never move the hit).
        for (const double t : starts)
        {
            double nearest = 1.0e9;
            for (const double q : plain)
                nearest = std::min (nearest, std::abs (q - t));
            INFO (name << ": hit at " << t << " s");
            CHECK (nearest < 0.006);
        }
    }
    renderWith ("aux03-06-rate-2x-pitch-plus5", 5.0f, 0.0f, 3.0f, false);
    renderWith ("aux03-07-pitch-automated", 0.0f, 0.0f, 2.0f, true);
}
