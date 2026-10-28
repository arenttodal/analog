// ANDOR OSP's engines on the live synth: GRANULAR on each voice's own recording (and KEY DNA),
// the REIMAGINED modes, MOVEMENT (DRIFT in the voices, the rest on the mix), DRIVE, and
// LFO 1's shapes and modes. Each must be audible when on, silent in effect when off,
// deterministic, independent of block sizes and allocation-free.

#include "TestUtil.h"

#include "FactoryPatches.h"

#include <catch2/catch_test_macros.hpp>

using namespace testutil;

namespace
{
    bool identical (const Stereo& a, const Stereo& b)
    {
        if (a.left.size() != b.left.size())
            return false;
        for (std::size_t i = 0; i < a.left.size(); ++i)
            if (a.left[i] != b.left[i] || a.right[i] != b.right[i])
                return false;
        return true;
    }
    double difference (const Stereo& a, const Stereo& b)
    {
        double d = 0.0;
        for (std::size_t i = 0; i < std::min (a.left.size(), b.left.size()); ++i)
            d += std::abs (a.left[i] - b.left[i]) + std::abs (a.right[i] - b.right[i]);
        return d / static_cast<double> (a.left.size());
    }

    /** A sustained, slightly bright sound: two detuned saws through a half-open filter. */
    Patch livePatch()
    {
        auto p = measurementPatch();
        p.osc2Level = 0.8;
        p.blend = 0.5;
        p.tone = math::hzToTone (3000.0);
        return p;
    }
    Stereo play (const Patch& p, double seconds = 1.2, std::vector<Event> events = { on (0, 57), on (0, 64) }, int block = 256)
    {
        auto s = makeSynth (p);
        return render (*s, seconds, std::move (events), block);
    }

    /** Zero crossings per second of a signal's first channel (a pitch estimate for a plain wave). */
    double crossingsPerSecond (const Stereo& s, double from, double to)
    {
        const auto a = static_cast<std::size_t> (from * s.rate), z = static_cast<std::size_t> (to * s.rate);
        int n = 0;
        for (std::size_t i = a + 1; i < z; ++i)
            n += (s.left[i - 1] < 0.0f) != (s.left[i] < 0.0f) ? 1 : 0;
        return n / (to - from);
    }
}

TEST_CASE ("live: GRANULAR sustains a held note from the voice's own sound", "[live][granular]")
{
    auto p = livePatch();
    p.granular = true;
    auto s = makeSynth (p);
    const auto out = render (*s, 2.0, { on (0, 60) });
    CHECK (out.finite());
    CHECK (out.rms (0.05, 0.3) > 0.01);   // it speaks at once (grains from just behind the sound)
    CHECK (out.rms (1.5, 2.0) > 0.01);    // and keeps sounding while held
    CHECK (s->grainCount (0) > 0);
    CHECK (out.peak() < 0.8f);
    // It is not the oscillators as they are.
    CHECK (difference (out, play (livePatch(), 2.0, { on (0, 60) })) > 1.0e-3);
}

TEST_CASE ("live: every GRANULAR setting moves the sound", "[live][granular]")
{
    auto base = livePatch();
    base.granular = true;
    base.grains.spread = 0.0;
    base.lfoAmount = 1.0;   // the sound moves, so where the grains come from is heard
    base.lfoPitch1 = base.lfoPitch2 = 3.0;
    base.lfo.rateHz = 1.3;
    const auto reference = play (base, 1.6);
    struct Setting
    {
        const char* name;
        void (*set) (Patch&);
    };
    const Setting settings[] = {
        { "POS", [] (Patch& p) { p.grains.position = 0.05; } },
        { "SIZE", [] (Patch& p) { p.grains.sizeSeconds = 0.04; } },
        { "DENS", [] (Patch& p) { p.grains.density = 35.0; } },
        { "TUNE", [] (Patch& p) { p.grains.tuneSemitones = 7.0; } },
        { "SPREAD", [] (Patch& p) { p.grains.spread = 0.8; } },
    };
    for (const auto& setting : settings)
    {
        INFO (setting.name);
        auto p = base;
        setting.set (p);
        CHECK (difference (play (p, 1.6), reference) > 1.0e-3);
    }
}

TEST_CASE ("live: TUNE plays the grains an octave up, as ANDOR OSP's granulator does", "[live][granular]")
{
    auto p = measurementPatch();
    p.osc1Wave = Osc1Wave::pulse;   // a square: two crossings per cycle
    p.tone = 1.0;
    p.granular = true;
    p.grains.spread = 0.0;
    p.grains.density = 40.0;
    p.grains.sizeSeconds = 0.08;
    const auto plain = play (p, 1.5, { on (0, 45) });
    p.grains.tuneSemitones = 12.0;
    const auto up = play (p, 1.5, { on (0, 45) });
    const double ratio = crossingsPerSecond (up, 0.5, 1.5) / crossingsPerSecond (plain, 0.5, 1.5);
    CHECK (ratio > 1.7);
    CHECK (ratio < 2.3);
}

TEST_CASE ("live: GRANULAR at its extremes stays finite and inside the guard", "[live][granular]")
{
    for (const double tune : { -12.0, 12.0 })
        for (const double size : { 0.02, 0.4 })
            for (const double density : { 4.0, 40.0 })
            {
                INFO ("tune " << tune << " size " << size << " density " << density);
                auto p = livePatch();
                p.granular = true;
                p.grains = { 1.0, size, density, tune, 1.0 };
                const auto out = play (p, 1.5, { on (0, 36), on (0, 60), on (0, 84), off (40000, 60) });
                CHECK (out.finite());
                CHECK (out.peak() < 0.9f);
            }
}

TEST_CASE ("live: KEY DNA gives each key its own grains; off, nothing changes", "[live][granular][keydna]")
{
    auto p = livePatch();
    p.granular = true;
    auto plain = makeSynth (p);
    const auto reference = render (*plain, 1.0, { on (0, 60) });

    osp::keydna::Settings neutral;   // off: an exact identity
    auto withNeutral = makeSynth (p);
    withNeutral->setKeyDna (neutral);
    CHECK (identical (render (*withNeutral, 1.0, { on (0, 60) }), reference));

    osp::keydna::Settings dna;
    dna.enabled = true;
    dna.individuality = 1.0;
    for (auto& param : dna.params)
        param.variation = 1.0;
    auto shaped = makeSynth (p);
    shaped->setKeyDna (dna);
    CHECK (difference (render (*shaped, 1.0, { on (0, 60) }), reference) > 1.0e-4);
}

TEST_CASE ("live: every REIMAGINED mode but MOSAIC changes the sound, in ONE SHOT and in GRANULAR", "[live][reimagined]")
{
    for (const bool granular : { false, true })
    {
        auto base = livePatch();
        base.granular = granular;
        const auto reference = play (base);
        for (int m = 0; m < osp::reimaginedModeCount; ++m)
        {
            const auto mode = osp::reimagined::modeFromIndex (m);
            INFO ((granular ? "GRANULAR " : "ONE SHOT ") << osp::reimagined::modeName (mode));
            auto p = base;
            p.reimagined = 0.8;
            p.reimaginedSettings.mode = mode;
            const auto out = play (p);
            CHECK (out.finite());
            CHECK (out.peak() < 0.9f);
            if (mode == osp::ReimaginedMode::mosaic)
                CHECK (identical (out, reference));   // no harmonic analysis of a live sound: it plays unchanged
            else
                CHECK (difference (out, reference) > 1.0e-3);
            // At 0 % every mode is the plain sound.
            p.reimagined = 0.0;
            CHECK (identical (play (p), reference));
        }
    }
}

TEST_CASE ("live: each REIMAGINED mode's own settings are heard", "[live][reimagined]")
{
    auto base = livePatch();
    base.reimagined = 0.9;
    struct Setting
    {
        osp::ReimaginedMode mode;
        const char* name;
        void (*set) (osp::ReimaginedSettings&);
    };
    const Setting settings[] = {
        { osp::ReimaginedMode::kaleidoscope, "FOCUS", [] (osp::ReimaginedSettings& s) { s.kaleidoscope.focus = 1.0; } },
        { osp::ReimaginedMode::kaleidoscope, "SPREAD", [] (osp::ReimaginedSettings& s) { s.kaleidoscope.spread = 1.0; } },
        { osp::ReimaginedMode::tapeFrame, "AGE", [] (osp::ReimaginedSettings& s) { s.tapeFrame.age = 1.0; } },
        { osp::ReimaginedMode::tapeFrame, "STABILITY", [] (osp::ReimaginedSettings& s) { s.tapeFrame.stability = 1.0; } },
        { osp::ReimaginedMode::toybox, "MOTION", [] (osp::ReimaginedSettings& s) { s.toybox.motion = 1.0; } },
        { osp::ReimaginedMode::toybox, "DIGITAL", [] (osp::ReimaginedSettings& s) { s.toybox.digital = 1.0; } },
        { osp::ReimaginedMode::mirage, "CLOCK", [] (osp::ReimaginedSettings& s) { s.mirage.clock = 1.0; } },
        { osp::ReimaginedMode::mirage, "FILTER", [] (osp::ReimaginedSettings& s) { s.mirage.filter = 1.0; } },
        { osp::ReimaginedMode::mirage, "TONE", [] (osp::ReimaginedSettings& s) { s.mirage.tone = osp::MirageTone::open; } },
    };
    for (const auto& setting : settings)
    {
        INFO (osp::reimagined::modeName (setting.mode) << " " << setting.name);
        auto p = base;
        p.reimaginedSettings.mode = setting.mode;
        const auto before = play (p);
        setting.set (p.reimaginedSettings);
        CHECK (difference (play (p), before) > 1.0e-4);
    }
    // TAPE FRAME's FRAME and TOYBOX's PLAY move where the grains are taken (GRANULAR).
    auto grains = base;
    grains.granular = true;
    grains.lfoAmount = 1.0;   // a moving sound, so where the grains come from is heard
    grains.lfo.rateHz = 1.3;
    grains.reimaginedSettings.mode = osp::ReimaginedMode::tapeFrame;
    auto longFrame = grains;
    longFrame.reimaginedSettings.tapeFrame.frame = osp::TapeFrameLength::longFrame;
    CHECK (difference (play (longFrame, 1.6), play (grains, 1.6)) > 1.0e-4);
    grains.reimaginedSettings.mode = osp::ReimaginedMode::toybox;
    auto chaos = grains;
    chaos.reimaginedSettings.toybox.play = osp::ToyboxPlay::chaos;
    CHECK (difference (play (chaos, 1.6), play (grains, 1.6)) > 1.0e-4);
}

TEST_CASE ("live: MOVEMENT's modes, DRIVE and its settings are heard", "[live][movement][drive]")
{
    auto base = livePatch();
    const auto reference = play (base, 2.0);
    for (int m = 0; m < 5; ++m)
    {
        INFO ("MOVEMENT mode " << m);
        auto p = base;
        p.motion = 0.8;
        p.effects.movementMode = static_cast<osp::MovementMode> (m);
        CHECK (difference (play (p, 2.0), reference) > 1.0e-4);
    }
    auto driven = base;
    driven.drive = 0.7;
    const auto drivenOut = play (driven, 2.0);
    CHECK (difference (drivenOut, reference) > 1.0e-3);
    for (int mode = 1; mode < 3; ++mode)
    {
        INFO ("DRIVE mode " << mode);
        auto p = driven;
        p.effects.driveMode = static_cast<osp::DriveMode> (mode);
        CHECK (difference (play (p, 2.0), drivenOut) > 1.0e-4);
    }
    auto toned = driven;
    toned.effects.driveTone = 1.0;
    CHECK (difference (play (toned, 2.0), drivenOut) > 1.0e-4);
    auto body = driven;
    body.effects.driveBody = 1.0;
    CHECK (difference (play (body, 2.0), drivenOut) > 1.0e-4);
}

TEST_CASE ("live: DRIFT wanders each voice on its own", "[live][movement]")
{
    auto p = livePatch();
    p.motion = 1.0;
    p.effects.driftSpeed = 1.0;
    p.effects.driftPitch = 1.0;
    // Two voices, one note: with DRIFT they no longer sound alike.
    const auto out = play (p, 3.0, { on (0, 60) });
    auto still = p;
    still.motion = 0.0;
    CHECK (difference (out, play (still, 3.0, { on (0, 60) })) > 1.0e-3);
}

TEST_CASE ("live: LFO 1's shapes, polarity and modes are heard", "[live][lfo]")
{
    auto base = livePatch();
    base.lfoAmount = 1.0;
    base.lfoPitch1 = base.lfoPitch2 = 0.0;
    base.lfoWidth1 = base.lfoWidth2 = 0.0;
    base.lfoCutoff = 24.0;
    base.lfo.rateHz = 2.0;
    std::vector<Stereo> shapes;
    for (int i = 0; i < osp::mod::lfoShapeCount; ++i)
    {
        auto p = base;
        p.lfo.shape = static_cast<osp::mod::LfoShape> (i);
        shapes.push_back (play (p, 1.5));
    }
    for (std::size_t a = 0; a < shapes.size(); ++a)
        for (std::size_t b = a + 1; b < shapes.size(); ++b)
        {
            INFO ("shapes " << a << " and " << b);
            CHECK (difference (shapes[a], shapes[b]) > 1.0e-4);
        }
    auto unipolar = base;
    unipolar.lfo.bipolar = false;
    CHECK (difference (play (unipolar, 1.5), shapes[0]) > 1.0e-4);

    // RETRIGGER restarts it at the first note after silence (the second phrase starts like the
    // first); FREE runs on.
    const std::vector<Event> twice { on (0, 60), off (17000, 60), on (24000, 60) };
    auto retrigger = base;
    retrigger.lfo.mode = osp::mod::LfoMode::retrigger;
    retrigger.lfo.phase = 0.25;
    CHECK (difference (play (retrigger, 1.0, twice), play (base, 1.0, twice)) > 1.0e-4);
    auto oneShot = base;
    oneShot.lfo.mode = osp::mod::LfoMode::oneShot;
    CHECK (difference (play (oneShot, 1.5), shapes[0]) > 1.0e-4);

    // POLY: each note its own LFO (RANDOM: its own values).
    auto random = base;
    random.lfo.shape = osp::mod::LfoShape::smoothRandom;
    auto poly = random;
    poly.lfo.scope = osp::mod::Scope::poly;
    CHECK (difference (play (poly, 1.5), play (random, 1.5)) > 1.0e-4);

    // CUSTOM follows its drawn curve.
    auto custom = base;
    custom.lfo.shape = osp::mod::LfoShape::custom;
    auto a = makeSynth (custom);
    const auto drawnDefault = render (*a, 1.5, { on (0, 57), on (0, 64) });
    auto b = makeSynth (custom);
    b->setLfoCurve (osp::mod::straightCurve());
    CHECK (difference (render (*b, 1.5, { on (0, 57), on (0, 64) }), drawnDefault) > 1.0e-4);
}

TEST_CASE ("live: everything at once is deterministic and independent of block sizes", "[live][determinism]")
{
    auto p = livePatch();
    p.granular = true;
    p.motion = 0.7;
    p.drive = 0.4;
    p.reimagined = 0.7;
    p.space = 0.2;
    p.echo = 0.2;
    p.analogAmount = 0.5;
    p.noiseLevel = 0.1;
    const std::vector<Event> events { on (0, 48), on (3000, 55), off (20000, 48), on (21000, 62), on (21000, 67), off (40000, 55) };
    for (int m = 0; m < osp::reimaginedModeCount; ++m)
    {
        p.reimaginedSettings.mode = osp::reimagined::modeFromIndex (m);
        for (int movement = 0; movement < 5; movement += 4)
        {
            p.effects.movementMode = static_cast<osp::MovementMode> (movement);
            INFO ("mode " << m << " movement " << movement);
            const auto a = play (p, 1.2, events, 1);
            CHECK (identical (a, play (p, 1.2, events, 7)));
            CHECK (identical (a, play (p, 1.2, events, 512)));
        }
    }
}

TEST_CASE ("live: voices taken for new notes keep their timing with grains and modes on", "[live][voices]")
{
    for (int config = 0; config < 4; ++config)
    {
        auto p = livePatch();
        p.granular = (config & 1) != 0;
        p.reimagined = (config & 2) != 0 ? 0.6 : 0.0;
        p.reimaginedSettings.mode = osp::ReimaginedMode::tapeFrame;
        p.polyphony = 2;
        std::vector<Event> events;
        for (int i = 0; i < 12; ++i)
            events.push_back (on (i * 2000, 48 + 3 * i));
        INFO ("granular " << p.granular << " tape " << p.reimagined);
        const auto a = play (p, 0.8, events, 3);
        CHECK (a.finite());
        CHECK (identical (a, play (p, 0.8, events, 256)));
    }
}
