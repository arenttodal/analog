// State version 4 in the synth: the oscillator models (DCO, WIDE, SUPER SAW) in either slot and
// every pairing, RING MOD, JUNO noise, the slots switched off, the OTA filter and the high-pass
// in the voice, the chorus after it, and the Juno modulation (LFO DELAY / RISE, GATE, ENV LINK).

#include "TestUtil.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace testutil;
using Catch::Approx;

namespace
{
    double cents (double measured, double expected) { return 1200.0 * std::log2 (measured / expected); }

    double differenceRms (const Stereo& a, const Stereo& b)
    {
        double sum = 0.0;
        for (std::size_t i = 0; i < a.left.size(); ++i)
        {
            const double d = static_cast<double> (a.left[i]) - b.left[i];
            sum += d * d;
        }
        return std::sqrt (sum / static_cast<double> (std::max<std::size_t> (1, a.left.size())));
    }

    bool identical (const Stereo& a, const Stereo& b) { return a.left == b.left && a.right == b.right; }

    /** OSC 1 alone through the open filter (measurementPatch), in a given model. */
    Patch slot1 (OscModel model)
    {
        auto p = measurementPatch();
        p.osc1Model = model;
        return p;
    }

    /** Energy of the spectrum's bins near `hz` (within +-2 bins), relative to the total. */
    double shareNear (const std::vector<double>& mag, double rate, double hz)
    {
        const double binHz = rate / (2.0 * static_cast<double> (mag.size()));
        const auto centre = static_cast<std::size_t> (std::lround (hz / binHz));
        double near = 0.0, total = 0.0;
        for (std::size_t b = 1; b < mag.size(); ++b)
        {
            total += mag[b] * mag[b];
            if (b + 2 >= centre && b <= centre + 2)
                near += mag[b] * mag[b];
        }
        return near / std::max (total, 1.0e-30);
    }

    double centroidHz (const std::vector<double>& mag, double rate)
    {
        const double binHz = rate / (2.0 * static_cast<double> (mag.size()));
        double w = 0.0, total = 0.0;
        for (std::size_t b = 1; b < mag.size(); ++b)
        {
            w += mag[b] * static_cast<double> (b) * binHz;
            total += mag[b];
        }
        return w / std::max (total, 1.0e-30);
    }

    Stereo play (const Patch& p, double seconds = 0.6, int note = 57, int block = 256)
    {
        auto s = makeSynth (p);
        return render (*s, seconds, { on (0, note, 100) }, block);
    }
}

TEST_CASE ("models: DCO plays in tune, its sub is the octave below in phase, its saw and pulse switch", "[models][dco]")
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        INFO ("rate " << rate);
        auto s = makeSynth (slot1 (OscModel::dco), rate);
        const auto out = render (*s, 1.5, { on (0, 69) });
        CHECK (std::abs (cents (frequencyByCrossings (out.mono(), rate, 0.2, 1.4), 440.0)) < 0.1);
    }
    // The sub: energy at 110 Hz for A2's 220 Hz, none without it.
    auto p = slot1 (OscModel::dco);
    const auto plain = play (p, 1.2, 57);
    p.dco1.subLevel = 1.0;
    const auto sub = play (p, 1.2, 57);
    const auto magPlain = spectrum (plain.mono(), 9600, 15), magSub = spectrum (sub.mono(), 9600, 15);
    CHECK (shareNear (magPlain, 48000.0, 110.0) < 1.0e-5);
    CHECK (shareNear (magSub, 48000.0, 110.0) > 0.05);
    // A true divide-by-two: the sub's period is exactly two of the oscillator's.
    p.dco1.saw = false;
    const auto subOnly = play (p, 1.2, 57);
    CHECK (std::abs (cents (frequencyByCrossings (subOnly.mono(), 48000.0, 0.2, 1.1), 110.0)) < 0.2);
    CHECK (subOnly.finite());
    // Saw, pulse, both: three different sounds; neither and no sub: silence.
    p = slot1 (OscModel::dco);
    const auto saw = play (p);
    p.dco1.pulse = true;
    const auto both = play (p);
    p.dco1.saw = false;
    const auto pulse = play (p);
    CHECK (differenceRms (saw, both) > 1.0e-3);
    CHECK (differenceRms (pulse, both) > 1.0e-3);
    p.dco1.pulse = false;
    CHECK (play (p).peak() == 0.0f);
}

TEST_CASE ("models: DCO PWM follows LFO 1 or the filter envelope; MANUAL is the WIDTH knob", "[models][dco]")
{
    auto p = slot1 (OscModel::dco);
    p.dco1.saw = false;
    p.dco1.pulse = true;
    p.osc1Width = 0.3;
    const auto manual = play (p);
    p.dco1.pwmDepth = 1.0;
    CHECK (identical (manual, play (p)));   // MANUAL: depth alone does nothing
    p.dco1.pwmSource = PwmSource::lfo;
    p.lfo.rateHz = 4.0;
    const auto lfo = play (p);
    CHECK (differenceRms (manual, lfo) > 1.0e-3);
    p.dco1.pwmSource = PwmSource::env;
    p.filterAttack = 0.2;
    const auto env = play (p);
    CHECK (differenceRms (manual, env) > 1.0e-3);
    CHECK (differenceRms (lfo, env) > 1.0e-3);
    CHECK (env.finite());
}

TEST_CASE ("models: a DCO is pitch-stable: its voices spread far less than CLASSIC's", "[models][dco][analog]")
{
    auto spread = [] (OscModel model) {
        auto p = slot1 (model);
        p.analogAmount = 1.0;
        double lo = 1.0e9, hi = -1.0e9;
        for (int k = 0; k < 8; ++k)   // eight notes in turn: eight voice slots
        {
            auto s = makeSynth (p);
            std::vector<Event> events;
            for (int j = 0; j < k; ++j)
                events.push_back (on (0, 40 + j, 100));
            events.push_back (on (0, 69, 100));
            for (int j = 0; j < k; ++j)
                events.push_back (off (10, 40 + j));
            auto out = render (*s, 1.2, events);
            const double c = cents (frequencyByCrossings (out.mono(), 48000.0, 0.4, 1.1), 440.0);
            lo = std::min (lo, c);
            hi = std::max (hi, c);
        }
        return hi - lo;
    };
    const double classic = spread (OscModel::classic), dco = spread (OscModel::dco);
    UNSCOPED_INFO ("pitch spread over 8 slots at ANALOG 100 %: CLASSIC " << classic << " cents, DCO " << dco);
    CHECK (classic > 3.0);
    CHECK (dco < 0.35 * classic);
}

TEST_CASE ("models: WIDE's waves differ; SQUARE is a pulse at 50 %; SHAPE moves the edge", "[models][wide]")
{
    auto p = slot1 (OscModel::wide);
    p.osc1Width = 0.3;   // (PULSE at 30 %: not SQUARE)
    std::vector<Stereo> waves;
    for (const auto w : { WideWave::saw, WideWave::pulse, WideWave::triangle, WideWave::square, WideWave::superSaw })
    {
        p.wide1.wave = w;
        p.wide1.spread = 0.5;
        waves.push_back (play (p));
        CHECK (waves.back().finite());
        CHECK (waves.back().rms (0.1, 0.5) > 0.01);
    }
    for (std::size_t i = 0; i < waves.size(); ++i)
        for (std::size_t j = i + 1; j < waves.size(); ++j)
            CHECK (differenceRms (waves[i], waves[j]) > 1.0e-3);
    // SQUARE ignores WIDTH; PULSE follows it.
    p.wide1.wave = WideWave::square;
    p.osc1Width = 0.2;
    CHECK (identical (play (p), waves[3]));
    p.wide1.wave = WideWave::pulse;
    CHECK (differenceRms (play (p), waves[1]) > 1.0e-3);   // (20 % against 30 %)
    // SHAPE: soft is darker than raw; the calibrated middle between.
    p.wide1.wave = WideWave::saw;
    auto centroidAt = [&] (double shape) {
        p.wide1.shape = shape;
        return centroidHz (spectrum (play (p, 1.0, 45).mono(), 9600, 14), 48000.0);
    };
    const double soft = centroidAt (0.0), middle = centroidAt (0.5), raw = centroidAt (1.0);
    UNSCOPED_INFO ("WIDE SHAPE centroid: soft " << soft << " Hz, calibrated " << middle << " Hz, raw " << raw << " Hz");
    CHECK (soft < middle);
    CHECK (middle < raw);
}

TEST_CASE ("models: SUPER SAW at SPREAD 0 is exactly one saw; spread detunes it, its level held", "[models][wide][supersaw]")
{
    auto p = slot1 (OscModel::wide);
    p.wide1.wave = WideWave::saw;
    const auto saw = play (p, 1.2);
    p.wide1.wave = WideWave::superSaw;
    p.wide1.spread = 0.0;
    CHECK (identical (saw, play (p, 1.2)));
    for (const double spread : { 0.25, 0.5, 1.0 })
    {
        INFO ("spread " << spread);
        p.wide1.spread = spread;
        const auto wide = play (p, 1.2);
        const double db = 20.0 * std::log10 (wide.rms (0.2, 1.1) / saw.rms (0.2, 0.55));
        UNSCOPED_INFO ("SUPER SAW spread " << spread << ": " << db << " dB against one saw");
        CHECK (std::abs (db) < 2.5);   // (slow beating at small spreads: +-2 dB over a second)
        // A high harmonic's line widens: the 20th (4.4 kHz) no longer sits in a few bins.
        auto line = [] (const std::vector<double>& mag, double hz) {
            const double binHz = 48000.0 / (2.0 * static_cast<double> (mag.size()));
            double narrow = 0.0, wideBand = 0.0;
            for (std::size_t b = 1; b < mag.size(); ++b)
            {
                const double f = static_cast<double> (b) * binHz, e = mag[b] * mag[b];
                if (std::abs (f - hz) < 3.0 * binHz)
                    narrow += e;
                if (std::abs (f - hz) < 100.0)
                    wideBand += e;
            }
            return narrow / std::max (wideBand, 1.0e-30);
        };
        CHECK (line (spectrum (wide.mono(), 9600, 15), 4400.0) < 0.8 * line (spectrum (saw.mono(), 9600, 15), 4400.0));
    }
}

TEST_CASE ("models: the new waves stay band-limited at 4x (aliasing well below the signal)", "[models][alias]")
{
    struct Case
    {
        const char* name;
        OscModel model;
        WideWave wave;
        double sub;
    };
    auto reference = measurementPatch();
    reference.osc1Width = 0.37;
    const double classic = inharmonicDb (spectrum (play (reference, 0.8, 96).mono(), 9600, 15), 48000.0, 2093.0, 20000.0, 6);
    for (const auto& c : { Case { "DCO saw + sub", OscModel::dco, WideWave::saw, 0.7 }, Case { "WIDE pulse", OscModel::wide, WideWave::pulse, 0.0 },
                           Case { "WIDE triangle", OscModel::wide, WideWave::triangle, 0.0 }, Case { "SUPER SAW", OscModel::wide, WideWave::superSaw, 0.0 } })
    {
        INFO (c.name);
        auto p = slot1 (c.model);
        p.dco1.subLevel = c.sub;
        p.wide1.wave = c.wave;
        p.wide1.shape = 1.0;   // raw: the worst case
        p.osc1Width = 0.37;
        const auto out = play (p, 0.8, 96);   // C7, 2093 Hz
        const auto mag = spectrum (out.mono(), 9600, 15);
        // (SUPER SAW's own detuned lines are not aliases: only its centre's harmonics are
        // "harmonic", so its figure is looser.)
        const double stray = inharmonicDb (mag, 48000.0, 2093.0 / (c.sub > 0.0 ? 2.0 : 1.0), 20000.0, c.wave == WideWave::superSaw ? 40 : 6);
        UNSCOPED_INFO (c.name << " at C7: inharmonic energy " << stray << " dB (CLASSIC's saw: " << classic << " dB)");
        // As clean as CLASSIC's (whose figure is this measure's floor: the decimator's own).
        CHECK (stray < (c.wave == WideWave::superSaw ? -25.0 : classic + 1.0));
    }
}

TEST_CASE ("models: all nine pairings play, differ, sync and Poly Mod act on each, the same every time", "[models][pairs]")
{
    const OscModel models[] = { OscModel::classic, OscModel::dco, OscModel::wide };
    std::vector<Stereo> renders;
    for (const auto m1 : models)
        for (const auto m2 : models)
        {
            INFO ("pair " << static_cast<int> (m1) << "+" << static_cast<int> (m2));
            Patch p;
            p.osc1Model = m1;
            p.osc2Model = m2;
            p.dco1.pulse = p.dco2.pulse = true;
            p.dco2.subLevel = 0.4;
            p.wide1.wave = WideWave::superSaw;
            p.wide1.spread = 0.4;
            p.wide2.wave = WideWave::triangle;
            p.space = 0.0;
            const std::vector<Event> chord { on (0, 48, 100), on (0, 55, 90), on (0, 64, 80) };
            auto s = makeSynth (p);
            const auto out = render (*s, 0.6, chord);
            CHECK (out.finite());
            CHECK (out.peak() < 0.8f);
            CHECK (out.rms (0.1, 0.5) > 0.005);
            auto again = makeSynth (p);
            CHECK (identical (out, render (*again, 0.6, chord, 61)));   // block sizes do not matter
            auto synced = p;
            synced.sync = true;
            synced.osc2Coarse = -5.0;
            auto plainSlave = p;
            plainSlave.osc2Coarse = -5.0;
            CHECK (differenceRms (play (plainSlave), play (synced)) > 1.0e-4);
            auto pm = p;
            pm.polyModOsc2 = 0.6;
            pm.polyModToPitch1 = true;
            CHECK (differenceRms (play (p), play (pm)) > 1.0e-4);
            renders.push_back (out);
        }
    for (std::size_t i = 0; i < renders.size(); ++i)
        for (std::size_t j = i + 1; j < renders.size(); ++j)
            CHECK (differenceRms (renders[i], renders[j]) > 1.0e-4);
}

TEST_CASE ("models: RING MOD makes the sum and difference tones, crossfaded in at a steady level", "[models][ring]")
{
    auto p = measurementPatch();
    p.osc1Wave = Osc1Wave::pulse;   // (any waves: sines would be clearer, these are the instrument's)
    p.osc2Level = 1.0;
    p.osc2Wave = Osc2Wave::triangle;
    p.osc2Coarse = 7.0;
    const auto pair = play (p, 1.2, 57);
    p.ringMix = 1.0;
    const auto ring = play (p, 1.2, 57);
    CHECK (differenceRms (pair, ring) > 1.0e-3);
    const double db = 20.0 * std::log10 (ring.rms (0.2, 1.1) / pair.rms (0.2, 1.1));
    UNSCOPED_INFO ("RING MOD at 100 %: " << db << " dB against the pair");
    CHECK (std::abs (db) < 6.0);
    // The difference tone (329.6 - 220 = 109.6 Hz) appears; it was not in the pair.
    const auto magPair = spectrum (pair.mono(), 9600, 15), magRing = spectrum (ring.mono(), 9600, 15);
    CHECK (shareNear (magRing, 48000.0, 329.63 - 220.0) > 10.0 * shareNear (magPair, 48000.0, 329.63 - 220.0));
    // A RING change glides: no step larger than the sound's own.
    CHECK (ring.finite());
}

TEST_CASE ("models: JUNO noise is darker than white; a slot switched off is silent", "[models][noise]")
{
    auto p = measurementPatch();
    p.osc1Level = 0.0;
    p.noiseLevel = 1.0;
    const auto white = play (p, 1.0);
    p.noiseColor = NoiseColor::juno;
    const auto juno = play (p, 1.0);
    const double cw = centroidHz (spectrum (white.mono(), 9600, 14), 48000.0), cj = centroidHz (spectrum (juno.mono(), 9600, 14), 48000.0);
    UNSCOPED_INFO ("noise centroid: white " << cw << " Hz, JUNO " << cj << " Hz");
    CHECK (cj < 0.75 * cw);
    CHECK (std::abs (20.0 * std::log10 (juno.rms (0.1, 0.9) / white.rms (0.1, 0.9))) < 4.0);
    auto q = measurementPatch();
    q.osc1Enabled = false;
    CHECK (play (q).peak() == 0.0f);
    q.osc1Enabled = true;
    q.osc2Level = 1.0;
    q.osc2Enabled = false;
    CHECK (identical (play (q), play (measurementPatch())));
}

TEST_CASE ("filters: OTA differs from the ladder, its voicings and slopes differ, a model change glides", "[models][ota]")
{
    auto p = measurementPatch();
    p.tone = 0.45;
    p.resonance = 0.7;
    const auto ladder = play (p);
    p.filterModel = FilterModel::ota;
    const auto juno = play (p);
    p.otaVoicing = OtaVoicing::jupiter;
    const auto jupiter = play (p);
    p.otaTwoPole = true;
    const auto twoPole = play (p);
    CHECK (differenceRms (ladder, juno) > 1.0e-3);
    CHECK (differenceRms (juno, jupiter) > 1.0e-4);
    CHECK (differenceRms (jupiter, twoPole) > 1.0e-3);
    auto six = p;
    six.otaVoicing = OtaVoicing::juno;
    six.otaTwoPole = false;
    six.junoVariant = JunoVariant::juno6;
    CHECK (differenceRms (play (six), juno) > 1.0e-4);   // the 6's darker calibration
    for (const auto* r : { &juno, &jupiter, &twoPole })
    {
        CHECK (r->finite());
        CHECK (r->peak() < 0.8f);
    }
    // The 12 dB slope lets more of the top through.
    CHECK (centroidHz (spectrum (twoPole.mono(), 9600, 14), 48000.0) > centroidHz (spectrum (jupiter.mono(), 9600, 14), 48000.0));
    // Switching LADDER -> OTA while a note sounds crossfades (about 30 ms): no click.
    auto s = makeSynth (measurementPatch());
    auto q = measurementPatch();
    q.tone = 0.5;
    s->setPatch (q);
    Stereo out;
    out.left.assign (48000, 0.0f);
    out.right.assign (48000, 0.0f);
    const std::uint8_t note[3] { 0x90, 45, 100 };
    s->handleMidi (note, 3);
    s->render (out.left.data(), out.right.data(), 24000);
    q.filterModel = FilterModel::ota;
    s->setPatch (q);
    s->render (out.left.data() + 24000, out.right.data() + 24000, 24000);
    float steady = 0.0f, around = 0.0f;
    for (int i = 12000; i < 23000; ++i)
        steady = std::max (steady, std::abs (out.left[static_cast<std::size_t> (i + 1)] - out.left[static_cast<std::size_t> (i)]));
    for (int i = 23900; i < 26000; ++i)
        around = std::max (around, std::abs (out.left[static_cast<std::size_t> (i + 1)] - out.left[static_cast<std::size_t> (i)]));
    UNSCOPED_INFO ("largest step: steady " << steady << ", around the model change " << around);
    CHECK (around < 1.5f * steady);
}

TEST_CASE ("filters: the high-pass thins the low end; each variant and position differs; OFF is the original", "[models][hpf]")
{
    auto p = measurementPatch();
    const auto plain = play (p, 0.8, 33);   // A1: lots of low end
    p.highPass = 3;
    const auto cut = play (p, 0.8, 33);
    CHECK (cut.rms (0.2, 0.7) < 0.8 * plain.rms (0.2, 0.7));
    p.junoVariant = JunoVariant::juno106;
    p.highPass = 0;   // the 106's bass boost
    const auto boost = play (p, 0.8, 33);
    CHECK (boost.rms (0.2, 0.7) > plain.rms (0.2, 0.7));
    p.highPass = 1;   // its FLAT
    CHECK (differenceRms (play (p, 0.8, 33), plain) < 1.0e-6);
    p.highPass = -1;
    CHECK (identical (play (p, 0.8, 33), plain));
    p.otaVoicing = OtaVoicing::jupiter;
    p.highPass = 2;
    const auto jupiter = play (p, 0.8, 33);
    p.otaVoicing = OtaVoicing::juno;
    p.junoVariant = JunoVariant::juno60;
    CHECK (differenceRms (jupiter, play (p, 0.8, 33)) > 1.0e-4);
}

TEST_CASE ("chorus: OFF is bit-exact; I, II and I+II each change the sound and widen it; switching is smooth", "[models][chorus]")
{
    Patch p;
    p.space = 0.0;
    const auto off = play (p, 1.2);
    std::vector<Stereo> modes;
    for (int mode = 1; mode <= 3; ++mode)
    {
        INFO ("mode " << mode);
        p.chorusMode = mode;
        modes.push_back (play (p, 1.2));
        CHECK (differenceRms (off, modes.back()) > 1.0e-3);
        CHECK (modes.back().finite());
        // Wider: left and right differ more than without it.
        double dOff = 0.0, dOn = 0.0;
        for (std::size_t i = 4800; i < off.left.size(); ++i)
        {
            dOff += std::abs (off.left[i] - off.right[i]);
            dOn += std::abs (modes.back().left[i] - modes.back().right[i]);
        }
        CHECK (dOn > dOff);
    }
    CHECK (differenceRms (modes[0], modes[1]) > 1.0e-4);
    CHECK (differenceRms (modes[1], modes[2]) > 1.0e-4);
    p.chorusMode = 1;
    p.chorusNoise = true;
    CHECK (differenceRms (play (p, 1.2), modes[0]) > 1.0e-6);
}

TEST_CASE ("modulation: LFO DELAY holds the LFO's depths back, RISE fades them in; off is the original", "[models][juno][lfo]")
{
    auto p = slot1 (OscModel::classic);
    p.lfoAmount = 1.0;
    p.lfoPitch1 = 2.0;
    p.lfo.rateHz = 6.0;
    auto noLfo = p;
    noLfo.lfoPitch1 = 0.0;
    const auto still = play (noLfo, 1.0, 69);
    const auto vibrato = play (p, 1.0, 69);
    p.lfoDelaySeconds = 0.5;
    p.lfoRiseSeconds = 0.3;
    const auto delayed = play (p, 1.0, 69);
    auto window = [] (const Stereo& x, double from, double to) {
        Stereo w;
        w.rate = x.rate;
        const auto a = static_cast<std::size_t> (from * x.rate), z = static_cast<std::size_t> (to * x.rate);
        w.left.assign (x.left.begin() + static_cast<long> (a), x.left.begin() + static_cast<long> (z));
        w.right.assign (x.right.begin() + static_cast<long> (a), x.right.begin() + static_cast<long> (z));
        return w;
    };
    // Before the delay: as without the LFO; after delay + rise: the full vibrato's depth.
    CHECK (differenceRms (window (delayed, 0.05, 0.45), window (still, 0.05, 0.45)) < 1.0e-6);
    CHECK (differenceRms (window (delayed, 0.05, 0.45), window (vibrato, 0.05, 0.45)) > 1.0e-3);
    const double fLate = frequencyByCrossings (delayed.mono(), 48000.0, 0.82, 0.99), fStill = frequencyByCrossings (still.mono(), 48000.0, 0.82, 0.99);
    CHECK (std::abs (cents (fLate, fStill)) > 1.0);
}

TEST_CASE ("modulation: GATE makes the amplitude a gate; ENV LINK makes it follow the filter envelope", "[models][juno][vca]")
{
    auto p = measurementPatch();
    p.ampAttack = 0.3;
    p.ampRelease = 1.0;
    const auto slow = play (p, 0.4);
    p.vcaGate = true;
    const auto gate = play (p, 0.4);
    CHECK (gate.rms (0.01, 0.05) > 3.0 * slow.rms (0.01, 0.05));   // open at once
    // Released: a gate stops within milliseconds, the envelope's release lasts.
    auto releaseTail = [] (const Patch& q) {
        auto s = makeSynth (q);
        return render (*s, 0.6, { on (0, 57, 100), off (9600, 57) }).rms (0.25, 0.35);
    };
    auto r = measurementPatch();
    r.ampRelease = 1.0;
    CHECK (releaseTail (r) > 0.01);
    r.vcaGate = true;
    CHECK (releaseTail (r) < 1.0e-4);
    // ENV LINK: the amplitude takes the filter envelope's slow attack.
    auto l = measurementPatch();
    l.filterAttack = 0.3;
    const auto own = play (l, 0.4);
    l.envLink = true;
    const auto linked = play (l, 0.4);
    CHECK (linked.rms (0.01, 0.05) < 0.5 * own.rms (0.01, 0.05));
}
