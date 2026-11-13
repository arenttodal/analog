// The audio thread's rules: no heap allocation while rendering (any patch change, notes,
// controllers, voice stealing included).

#include "TestUtil.h"
#include "rhythm/RhythmLoop.h"

#include "FactoryPatches.h"
#include "fx/Effects.h"
#include "fx/FxProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <memory>
#include <cstdlib>
#include <new>

namespace
{
    std::atomic<bool> counting { false };
    std::atomic<int> allocations { 0 };
}

void* operator new (std::size_t size)
{
    if (counting.load (std::memory_order_relaxed))
        allocations.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

using namespace testutil;

TEST_CASE ("realtime: rendering, events and patch changes never allocate", "[realtime]")
{
    const auto& list = factoryPatches();
    auto synthOwned = std::make_unique<Synth>();   // (on the heap: a Synth is larger than some platforms' stacks)
    Synth& synth = *synthOwned;
    synth.prepare (48000.0, 512, 4);
    synth.setPatch (list[0].patch);
    synth.reset();
    std::vector<float> left (512), right (512);
    osp::HostTiming timing;
    timing.valid = timing.playing = true;
    timing.bpm = 128.0;

    counting = true;
    for (int block = 0; block < 400; ++block)
    {
        synth.setPatch (list[static_cast<std::size_t> (block / 20) % list.size()].patch);
        timing.ppq += 512.0 / 48000.0 * 128.0 / 60.0;
        synth.setTransport (timing);
        const std::uint8_t noteOn[3] { 0x90, static_cast<std::uint8_t> (36 + block % 48), 100 };
        const std::uint8_t noteOff[3] { 0x80, static_cast<std::uint8_t> (36 + (block + 40) % 48), 0 };
        const std::uint8_t wheel[3] { 0xb0, 1, static_cast<std::uint8_t> (block % 128) };
        const std::uint8_t pedal[3] { 0xb0, 64, static_cast<std::uint8_t> (block % 50 < 25 ? 127 : 0) };
        synth.handleMidi (noteOn, 3);
        synth.render (left.data(), right.data(), 200);
        synth.handleMidi (noteOff, 3);
        synth.handleMidi (wheel, 3);
        synth.handleMidi (pedal, 3);
        synth.render (left.data(), right.data(), 312);
    }
    counting = false;
    CHECK (allocations.load() == 0);
}

TEST_CASE ("realtime: GRANULAR, the REIMAGINED modes, MOVEMENT, DRIVE and KEY DNA never allocate", "[realtime][live]")
{
    auto synthOwned = std::make_unique<Synth>();   // (on the heap: a Synth is larger than some platforms' stacks)
    Synth& synth = *synthOwned;
    synth.prepare (48000.0, 512, 4);
    auto p = factoryPatches()[0].patch;
    p.granular = true;
    p.motion = 0.6;
    p.drive = 0.5;
    p.reimagined = 0.7;
    synth.setPatch (p);
    synth.reset();
    std::vector<float> left (512), right (512);
    osp::keydna::Settings dna;
    dna.enabled = true;
    for (auto& param : dna.params)
        param.variation = 0.6;

    counting = true;
    for (int block = 0; block < 300; ++block)
    {
        p.reimaginedSettings.mode = osp::reimagined::modeFromIndex (block / 12 % osp::reimaginedModeCount);
        p.effects.movementMode = static_cast<osp::MovementMode> (block / 30 % 5);
        p.granular = block % 90 < 70;
        p.lfo.shape = static_cast<osp::mod::LfoShape> (block / 7 % osp::mod::lfoShapeCount);
        dna.amount = block % 40 < 20 ? 1.0 : 0.5;
        synth.setKeyDna (dna);
        synth.setLfoCurve (block % 2 == 0 ? osp::mod::straightCurve() : osp::mod::defaultLfoCurve());
        synth.setPatch (p);
        const std::uint8_t noteOn[3] { 0x90, static_cast<std::uint8_t> (36 + block % 48), 100 };
        const std::uint8_t noteOff[3] { 0x80, static_cast<std::uint8_t> (36 + (block + 30) % 48), 0 };
        synth.handleMidi (noteOn, 3);
        synth.render (left.data(), right.data(), 256);
        synth.handleMidi (noteOff, 3);
        synth.render (left.data(), right.data(), 256);
    }
    counting = false;
    CHECK (allocations.load() == 0);
}

TEST_CASE ("realtime: modulation (routes, a product control, controllers, reset) never allocates", "[realtime][modulation]")
{
    namespace mod = osp::mod;
    struct Tone final : ControlModulator
    {
        void moveControl (Patch& p, int, double offset) const noexcept override { p.tone = std::clamp (0.5 + offset, 0.0, 1.0); }
    } tone;
    mod::setProductDestinations ({ { "test.tone", "TONE", mod::Owner::voice, mod::Update::continuous } });
    mod::Settings settings;
    settings.routes[0] = { mod::Source::lfo2, mod::productDest (0), 0.5, true, -1 };
    settings.routes[1] = { mod::Source::env1, mod::Dest::levelA, -0.3, true, -1 };
    settings.routes[2] = { mod::Source::vel, mod::Dest::ampRelease, 0.4, true, -1 };
    settings.routes[3] = { mod::Source::cc1, mod::Dest::panA, 0.4, true, -1 };
    settings.routes[4] = { mod::Source::modWheel, mod::Dest::drive, 0.4, true, -1 };
    auto synthOwned = std::make_unique<Synth>();   // (on the heap: a Synth is larger than some platforms' stacks)
    Synth& synth = *synthOwned;
    synth.prepare (48000.0, 512, 4);
    synth.setPatch (factoryPatches()[1].patch);
    synth.setControlModulator (&tone);
    synth.reset();
    std::vector<float> left (512), right (512);
    const std::array<int, mod::ccSlots> controllers { 2, 11, 16, 17 };
    counting = true;
    synth.setModulation (settings, controllers);
    for (int block = 0; block < 40; ++block)
    {
        if (block % 5 == 0)
        {
            const std::uint8_t note[3] { 0x90, static_cast<std::uint8_t> (48 + block / 5), 100 };
            synth.handleMidi (note, 3);
            const std::uint8_t controller[3] { 0xb0, 2, static_cast<std::uint8_t> (3 * block) };
            synth.handleMidi (controller, 3);
        }
        if (block % 7 == 6)
        {
            const std::uint8_t release[3] { 0x80, static_cast<std::uint8_t> (48 + block / 7), 0 };
            synth.handleMidi (release, 3);
        }
        synth.render (left.data(), right.data(), 512);
    }
    synth.reset();
    counting = false;
    CHECK (allocations.load() == 0);
}

TEST_CASE ("realtime: the oscillator models, RING, the OTA, the high-pass and the chorus never allocate", "[realtime][models]")
{
    auto synthOwned = std::make_unique<Synth>();   // (on the heap: a Synth is larger than some platforms' stacks)
    Synth& synth = *synthOwned;
    synth.prepare (48000.0, 512, 4);
    Patch p;
    synth.setPatch (p);
    synth.reset();
    std::vector<float> left (512), right (512);
    counting = true;
    for (int block = 0; block < 240; ++block)
    {
        p.osc1Model = static_cast<OscModel> (block / 8 % 3);
        p.osc2Model = static_cast<OscModel> (block / 24 % 3);
        p.wide1.wave = static_cast<WideWave> (block / 5 % 5);
        p.wide1.spread = (block % 7) / 7.0;
        p.dco2.subLevel = 0.5;
        p.dco2.pwmSource = static_cast<PwmSource> (block % 3);
        p.dco2.pwmDepth = 0.5;
        p.ringMix = block % 40 < 20 ? 0.6 : 0.0;
        p.noiseLevel = 0.2;
        p.noiseColor = static_cast<NoiseColor> (block / 30 % 2);
        p.filterModel = static_cast<FilterModel> (block / 16 % 2);
        p.otaVoicing = static_cast<OtaVoicing> (block / 32 % 2);
        p.otaTwoPole = block % 50 < 25;
        p.highPass = block / 12 % 5 - 1;
        p.chorusMode = block / 20 % 4;
        p.chorusNoise = block % 2 == 0;
        p.lfoDelaySeconds = 0.1;
        p.lfoRiseSeconds = 0.2;
        p.vcaGate = block % 60 < 10;
        p.envLink = block % 60 > 50;
        p.sync = block % 9 < 4;
        synth.setPatch (p);
        const std::uint8_t noteOn[3] { 0x90, static_cast<std::uint8_t> (36 + block % 48), 100 };
        const std::uint8_t noteOff[3] { 0x80, static_cast<std::uint8_t> (36 + (block + 40) % 48), 0 };
        synth.handleMidi (noteOn, 3);
        synth.render (left.data(), right.data(), 256);
        synth.handleMidi (noteOff, 3);
        synth.render (left.data(), right.data(), 256);
    }
    counting = false;
    CHECK (allocations.load() == 0);
}

TEST_CASE ("realtime: ORGANIC (notes, loops, a recording replaced while it plays) never allocates", "[realtime][organic]")
{
    auto make = [] (double hz) {
        osp::AudioData audio;
        audio.sampleRate = 44100.0;
        std::vector<float> x (44100);
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = static_cast<float> (0.5 * std::sin (2.0 * 3.14159265358979 * hz * static_cast<double> (i) / 44100.0));
        audio.channels = { x };
        auto m = std::make_shared<osp::InstrumentModel>();
        m->original.source = std::make_shared<osp::PlaybackSource> (audio, 57.0, 4);
        m->original.continuation.canSustain = true;
        m->original.continuation.jumps.push_back ({ 30000.0, 8000.0, 400.0, 1.0f, 1.0f });
        m->original.continuation.backstop = 0;
        return m;
    };
    const auto a = make (220.0), b = make (330.0);
    auto synthOwned = std::make_unique<Synth>();   // (on the heap: a Synth is larger than some platforms' stacks)
    Synth& synth = *synthOwned;
    synth.prepare (48000.0, 512, 4);
    Patch p;
    synth.setPatch (p);
    synth.reset();
    std::vector<float> left (512), right (512);
    counting = true;
    for (int block = 0; block < 200; ++block)
    {
        if (block % 25 == 0)
            synth.setOrganic (block % 50 == 0 ? a.get() : b.get());
        p.organic.reverse = block % 60 > 40;
        p.organic.follow = block % 70 < 50;
        p.osc1Model = static_cast<OscModel> (block % 3);
        synth.setPatch (p);
        const std::uint8_t noteOn[3] { 0x90, static_cast<std::uint8_t> (36 + block % 48), 100 };
        const std::uint8_t noteOff[3] { 0x80, static_cast<std::uint8_t> (36 + (block + 40) % 48), 0 };
        synth.handleMidi (noteOn, 3);
        synth.render (left.data(), right.data(), 256);
        synth.handleMidi (noteOff, 3);
        synth.render (left.data(), right.data(), 256);
    }
    synth.setOrganic (nullptr);
    synth.render (left.data(), right.data(), 512);
    counting = false;
    CHECK (allocations.load() == 0);
}

TEST_CASE ("realtime: MORPH (live: IMPRINT, FUSE, WEAVE, every carrier and card C type, the position moving) never allocates", "[realtime][morph]")
{
    osp::AudioData audio;
    audio.sampleRate = 48000.0;
    std::vector<float> x (96000);
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const double t = static_cast<double> (i) / 48000.0;
        x[i] = static_cast<float> (0.3 * std::sin (2.0 * 3.14159265358979 * 220.0 * t) + 0.15 * std::sin (2.0 * 3.14159265358979 * 660.0 * t));
    }
    audio.channels = { x };
    auto model = std::make_shared<osp::InstrumentModel>();
    model->original.source = std::make_shared<osp::PlaybackSource> (audio, 57.0, 4);
    auto synthOwned = std::make_unique<Synth>();   // (on the heap: a Synth is larger than some platforms' stacks)
    Synth& synth = *synthOwned;
    synth.prepare (48000.0, 512, 4);
    Patch p;
    p.morph.enabled = true;
    synth.setPatch (p);
    synth.reset();
    synth.setOrganic (model.get());
    std::vector<float> left (512), right (512);
    counting = true;
    for (int block = 0; block < 180; ++block)
    {
        p.morph.method = static_cast<MorphMethod> (block / 20 % 3);
        p.morph.carrier = static_cast<MorphCarrier> (block / 7 % 3);
        p.morph.output = static_cast<MorphOutput> (block / 45 % 2);
        p.morph.position = (block % 30) / 29.0;
        p.morph.formants = (block % 11) - 5.0;
        p.morph.detail = (block % 13) / 12.0;
        p.hybridType = static_cast<HybridType> (block / 60 % 3);   // the recording, OSC 3, NOISE
        synth.setPatch (p);
        const std::uint8_t noteOn[3] { 0x90, static_cast<std::uint8_t> (40 + block % 36), 100 };
        const std::uint8_t noteOff[3] { 0x80, static_cast<std::uint8_t> (40 + (block + 30) % 36), 0 };
        synth.handleMidi (noteOn, 3);
        synth.render (left.data(), right.data(), 256);
        synth.handleMidi (noteOff, 3);
        synth.render (left.data(), right.data(), 256);
    }
    counting = false;
    CHECK (allocations.load() == 0);
}

TEST_CASE ("realtime: an imported sample source (zones, loops, swaps) never allocates", "[realtime][samplesource]")
{
    auto tone = [] (double hz, double rate) {
        osp::AudioData audio;
        audio.sampleRate = rate;
        std::vector<float> x (static_cast<std::size_t> (rate));
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = static_cast<float> (0.4 * std::sin (6.283185307 * hz * static_cast<double> (i) / rate));
        audio.channels = { x, x };
        return std::make_shared<osp::PlaybackSource> (audio, 57.0, 4);
    };
    SampleSource a, b;
    for (int i = 0; i < 6; ++i)
    {
        SampleZone z;
        z.audio = tone (110.0 * (i + 1), i % 2 == 0 ? 44100.0 : 48000.0);
        z.keyLo = 30 + 8 * i;
        z.keyHi = 50 + 8 * i;   // (overlapping)
        z.root = 40 + 8 * i;
        z.loop.on = i % 3 != 0;
        z.loop.alternating = i % 3 == 2;
        z.loop.start = 4000.0;
        z.loop.length = 20000.0;
        z.loop.crossfade = 3000.0;
        a.zones.push_back (z);
    }
    b.zones = { a.zones[1], a.zones[4] };
    auto patch = measurementPatch();
    auto synthOwned = std::make_unique<Synth>();   // (on the heap: a Synth is larger than some platforms' stacks)
    Synth& synth = *synthOwned;
    synth.prepare (48000.0, 512, 4);
    synth.setPatch (patch);
    synth.setSampleSource (&a);
    synth.reset();
    std::vector<float> left (512), right (512);
    counting = true;
    for (int block = 0; block < 300; ++block)
    {
        if (block % 50 == 25)
            synth.setSampleSource (block % 100 == 25 ? &b : &a);
        patch.organic.loop = block % 80 < 60;
        synth.setPatch (patch);
        const std::uint8_t noteOn[3] { 0x90, static_cast<std::uint8_t> (30 + block % 60), 100 };
        const std::uint8_t noteOff[3] { 0x80, static_cast<std::uint8_t> (30 + (block + 30) % 60), 0 };
        synth.handleMidi (noteOn, 3);
        synth.render (left.data(), right.data(), 256);
        synth.handleMidi (noteOff, 3);
        synth.render (left.data(), right.data(), 256);
    }
    counting = false;
    CHECK (allocations.load() == 0);
}

TEST_CASE ("realtime: three engines (each card's EQ and M / S, OSC 3, NOISE, the hybrid's type switching) never allocate", "[realtime][threeengines]")
{
    auto synthOwned = std::make_unique<Synth>();   // (on the heap: a Synth is larger than some platforms' stacks)
    Synth& synth = *synthOwned;
    synth.prepare (48000.0, 512, 4);
    Patch p;
    synth.setPatch (p);
    synth.reset();
    std::vector<float> left (512), right (512);
    osp::eq::Settings bell;
    bell.enabled = true;
    bell.bands[static_cast<std::size_t> (osp::eq::Band::bell)].enabled = true;
    bell.bands[static_cast<std::size_t> (osp::eq::Band::bell)].gainDb = 9.0;
    counting = true;
    for (int block = 0; block < 240; ++block)
    {
        p.hybridType = static_cast<HybridType> (block / 10 % 3);   // SAMPLE (none), OSC 3, NOISE
        p.osc3Model = static_cast<OscModel> (block / 7 % 3);
        p.wide3.wave = static_cast<WideWave> (block / 5 % 5);
        p.dco3.pwmSource = static_cast<PwmSource> (block % 3);
        p.dco3.pwmDepth = 0.4;
        p.hybridNoiseColor = static_cast<NoiseColor> (block / 13 % 2);
        p.hybridNoiseTone = (block % 9) / 9.0;
        p.hybridNoiseMovement = 0.6;
        p.eqA = block % 30 < 15 ? bell : osp::eq::Settings();
        p.eqC = block % 20 < 10 ? bell : osp::eq::Settings();
        p.dampenC.enabled = block % 40 < 20;
        p.dampenC.amount = 0.5;
        p.osc1Heard = block % 17 != 0;
        p.osc2Heard = block % 23 != 0;
        p.osc1FineCents = (block % 11) - 5.0;
        p.ringMix = block % 40 < 20 ? 0.4 : 0.0;
        synth.setPatch (p);
        const std::uint8_t noteOn[3] { 0x90, static_cast<std::uint8_t> (36 + block % 48), 100 };
        const std::uint8_t noteOff[3] { 0x80, static_cast<std::uint8_t> (36 + (block + 40) % 48), 0 };
        synth.handleMidi (noteOn, 3);
        synth.render (left.data(), right.data(), 256);
        synth.handleMidi (noteOff, 3);
        synth.render (left.data(), right.data(), 256);
    }
    counting = false;
    CHECK (allocations.load() == 0);
}

TEST_CASE ("realtime: every new effect - RETRO in each personality, switched mid-stream - never allocates", "[realtime][fx][retro]")
{
    namespace fx = andor::analog::fx;
    fx::Settings settings;
    std::vector<std::unique_ptr<fx::Processor>> processors;
    for (int t = 4; t < fx::typeCount; ++t)
        if (auto proc = fx::makeProcessor (static_cast<fx::Type> (t)))
        {
            proc->prepare (48000.0, 512);
            proc->set (settings, 0);
            proc->reset();
            processors.push_back (std::move (proc));
        }
    REQUIRE (processors.size() >= 10);
    std::vector<float> l (512), r (512);
    auto& retro = settings.retro[0];
    retro.noiseDb = -50.0f;
    retro.crackle = retro.pops = 0.6f;
    retro.dropoutRate = 6.0f;
    retro.dropoutDepth = 0.8f;
    retro.compAmount = 0.5f;
    retro.codecAmbience = 0.5f;
    retro.digitalMotion = 1.0f;
    counting = true;
    for (int block = 0; block < 400; ++block)
    {
        // The personality every 40 blocks (TAPE, VINYL, 80s, 90s, CODEC and its packet modes).
        const int which = (block / 40) % 8;
        retro.mode = which < 2 ? which : fx::RetroSettings::digital;
        retro.era = which < 2 ? fx::RetroSettings::nineties : std::min (2, which - 2);
        retro.codecMode = std::max (0, which - 4);
        retro.aliasHard = (block / 20) % 2 == 1;
        const int n = 1 + (block * 37) % 512;
        for (auto& proc : processors)
        {
            for (int i = 0; i < n; ++i)
            {
                l[static_cast<std::size_t> (i)] = 0.3f * std::sin (0.031f * static_cast<float> (block * 512 + i));
                r[static_cast<std::size_t> (i)] = l[static_cast<std::size_t> (i)];
            }
            proc->set (settings, 0);
            proc->process (l.data(), r.data(), n);
        }
    }
    counting = false;
    CHECK (allocations.load() == 0);
}

TEST_CASE ("realtime: SUB's keys, range, settings and controllers never allocate (RT-01)", "[realtime][sub][v6]")
{
    auto synthOwned = std::make_unique<Synth>();   // (on the heap: a Synth is larger than some platforms' stacks)
    Synth& synth = *synthOwned;
    synth.prepare (48000.0, 512, 4);
    auto p = factoryPatches()[0].patch;
    p.sub.enabled = true;
    synth.setPatch (p);
    synth.reset();
    std::vector<float> left (512), right (512);
    counting = true;
    for (int block = 0; block < 300; ++block)
    {
        p.sub.wave = static_cast<SubSettings::Wave> (block / 7 % 4);
        p.sub.grit = (block % 11) / 10.0;
        p.sub.lowKey = 36 + block % 13;
        p.sub.highKey = 70 + block % 17;
        p.sub.taper = block % 3 != 0;
        p.sub.enabled = block % 50 < 45;
        synth.setPatch (p);
        const std::uint8_t keyOn[3] { 0x90, static_cast<std::uint8_t> (36 + block % 48), 100 };
        const std::uint8_t keyOff[3] { 0x80, static_cast<std::uint8_t> (36 + (block + 30) % 48), 0 };
        const std::uint8_t pedal[3] { 0xb0, 64, static_cast<std::uint8_t> (block % 40 < 20 ? 127 : 0) };
        const std::uint8_t bend[3] { 0xe0, 0, static_cast<std::uint8_t> (block % 128) };
        const std::uint8_t allOff[3] { 0xb0, static_cast<std::uint8_t> (block % 2 == 0 ? 123 : 120), 0 };
        synth.handleSubMidi (keyOn, 3);
        synth.handleMidi (keyOn, 3);
        synth.render (left.data(), right.data(), 256);
        synth.handleSubMidi (keyOff, 3);
        synth.handleSubMidi (pedal, 3);
        synth.handleSubMidi (bend, 3);
        if (block % 37 == 0)
            synth.handleSubMidi (allOff, 3);
        synth.render (left.data(), right.data(), 256);
    }
    counting = false;
    CHECK (allocations.load() == 0);
    for (std::size_t i = 0; i < left.size(); ++i)
        CHECK ((std::isfinite (left[i]) && (left[i] == 0.0f || std::abs (left[i]) > 1.0e-30f)));
}

TEST_CASE ("realtime: card C's second layer (its types, its source swapped mid-note) never allocates", "[realtime][hybrid2][v6]")
{
    auto makeSource = [] (double hz) {
        osp::AudioData audio;
        audio.sampleRate = 44100.0;
        std::vector<float> x (44100);
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = static_cast<float> (0.4 * std::sin (6.283185307179586 * hz * static_cast<double> (i) / 44100.0));
        audio.channels.assign (1, x);
        SampleSource s;
        SampleZone z;
        z.audio = std::make_shared<osp::PlaybackSource> (audio, 69.0, 4);
        z.root = 69.0;
        s.zones.push_back (z);
        return s;
    };
    const auto a = makeSource (440.0), b = makeSource (330.0);
    auto synthOwned = std::make_unique<Synth>();   // (on the heap: a Synth is larger than some platforms' stacks)
    Synth& synth = *synthOwned;
    synth.prepare (48000.0, 512, 4);
    auto p = factoryPatches()[0].patch;
    p.hybrid2.enabled = true;
    synth.setPatch (p);
    synth.reset();
    std::vector<float> left (512), right (512);
    counting = true;
    for (int block = 0; block < 300; ++block)
    {
        p.hybrid2.type = static_cast<HybridType> (block / 9 % 4);
        p.hybrid2.model = static_cast<OscModel> (block / 5 % 3);
        p.hybrid2.enabled = block % 40 < 35;
        p.morph.enabled = block % 60 < 30;
        synth.setPatch (p);
        if (block % 13 == 0)
            synth.setSampleSource2 (block % 26 == 0 ? &a : (block % 39 == 0 ? nullptr : &b));
        const std::uint8_t keyOn[3] { 0x90, static_cast<std::uint8_t> (36 + block % 48), 100 };
        const std::uint8_t keyOff[3] { 0x80, static_cast<std::uint8_t> (36 + (block + 30) % 48), 0 };
        synth.handleMidi (keyOn, 3);
        synth.render (left.data(), right.data(), 256);
        synth.handleMidi (keyOff, 3);
        synth.render (left.data(), right.data(), 256);
    }
    counting = false;
    synth.setSampleSource2 (nullptr);
    CHECK (allocations.load() == 0);
}

TEST_CASE ("realtime: MAIN + SEND (the cards' sends, SEND's wet chain, SUB on MAIN) never allocates", "[realtime][mainsend][v6]")
{
    namespace fx = andor::analog::fx;
    fx::Graph g;
    g.custom = true;
    g.mainSend = true;
    g.chain = { fx::Node::effect ({ fx::Type::drive, 0 }), fx::Node::effect ({ fx::Type::eq, 0 }) };
    g.send = { fx::Node::effect ({ fx::Type::eq, 1 }), fx::Node::parallel ({ fx::Node::effect ({ fx::Type::echo, 0 }) }, { fx::Node::effect ({ fx::Type::space, 0 }) }, fx::Law::send),
               fx::Node::effect ({ fx::Type::convolution, 0 }) };
    auto synthOwned = std::make_unique<Synth>();   // (on the heap: a Synth is larger than some platforms' stacks)
    Synth& synth = *synthOwned;
    synth.prepare (48000.0, 512, 4);
    auto p = factoryPatches()[0].patch;
    p.sub.enabled = true;
    p.hybrid2.enabled = true;
    synth.setPatch (p);
    synth.setFxGraph (fx::compile (g, 1));
    synth.reset();
    std::vector<float> left (512), right (512);
    synth.render (left.data(), right.data(), 512);   // (the route taken)
    counting = true;
    for (int block = 0; block < 200; ++block)
    {
        p.sendA = (block % 5) / 4.0;
        p.sendB = (block % 3) / 2.0;
        p.sendC = block % 7 == 0 ? 0.0 : 0.8;
        p.space = (block % 11) / 10.0;
        synth.setPatch (p);
        const std::uint8_t keyOn[3] { 0x90, static_cast<std::uint8_t> (36 + block % 48), 100 };
        const std::uint8_t keyOff[3] { 0x80, static_cast<std::uint8_t> (36 + (block + 30) % 48), 0 };
        synth.handleMidi (keyOn, 3);
        synth.handleSubMidi (keyOn, 3);
        synth.render (left.data(), right.data(), 256);
        synth.handleMidi (keyOff, 3);
        synth.handleSubMidi (keyOff, 3);
        synth.render (left.data(), right.data(), 256);
    }
    counting = false;
    CHECK (allocations.load() == 0);
}

TEST_CASE ("RT-01: SHAPER DNA (16 voices, three profiles, FILTER / BOTH, hand-overs, profile changes) never allocates", "[realtime][shaperdna][v8]")
{
    auto synthOwned = std::make_unique<Synth>();
    Synth& synth = *synthOwned;
    synth.prepare (48000.0, 512, 4);
    auto p = factoryPatches()[0].patch;
    p.motion = 1.0;
    p.effects.movementMode = osp::MovementMode::shaper;
    p.effects.shaper.target = osp::ShaperTarget::both;
    p.shaperDna.enabled = true;
    p.shaperDna.rate = { -1, static_cast<int> (osp::ShaperRate::eighthTriplet), static_cast<int> (osp::ShaperRate::thirtySecond) };
    p.sub.enabled = true;
    synth.setPatch (p);
    synth.reset();
    std::vector<float> left (512), right (512);
    synth.render (left.data(), right.data(), 512);
    counting = true;
    for (int block = 0; block < 200; ++block)
    {
        p.shaperDna.enabled = block % 37 < 30;   // hand-overs both ways
        p.shaperDna.pattern[1] = block % 12;      // profile changes (crossfades)
        p.shaperDna.assignment = block % 50 < 25 ? andor::analog::ShaperDnaSettings::Assignment::cycle : andor::analog::ShaperDnaSettings::Assignment::keyZones;
        p.effects.shaper.target = block % 3 == 0 ? osp::ShaperTarget::filter : osp::ShaperTarget::both;
        synth.setPatch (p);
        osp::HostTiming t;
        t.valid = true;
        t.playing = block % 40 < 20;
        t.ppq = block * 0.1;
        synth.setTransport (t);
        const std::uint8_t keyOn[3] { 0x90, static_cast<std::uint8_t> (36 + block % 48), 100 };
        const std::uint8_t keyOff[3] { 0x80, static_cast<std::uint8_t> (36 + (block + 40) % 48), 0 };
        synth.handleMidi (keyOn, 3);
        synth.handleSubMidi (keyOn, 3);
        synth.render (left.data(), right.data(), 256);
        synth.handleMidi (keyOff, 3);
        synth.handleSubMidi (keyOff, 3);
        synth.render (left.data(), right.data(), 256);
    }
    counting = false;
    CHECK (allocations.load() == 0);
}

TEST_CASE ("realtime: SATELLITE (three cards moving, twins starting and stopping, modes switching) never allocates", "[realtime][satellite]")
{
    auto synthOwned = std::make_unique<Synth>();
    Synth& synth = *synthOwned;
    synth.prepare (48000.0, 512, 4);
    Patch p = measurementPatch();
    p.blend = 0.5;
    p.hybridType = HybridType::oscillator;
    p.resonance = 0.5;
    synth.setPatch (p);
    synth.reset();
    synth.setCardRouting (true);
    std::vector<float> left (512), right (512);
    osp::HostTiming timing;
    timing.valid = timing.playing = true;
    timing.bpm = 140.0;
    counting = true;
    for (int block = 0; block < 300; ++block)
    {
        // Every few blocks: a card on or off, a new mode, FREE or SYNC; notes on single cards
        // (whole voices) and on all three (twins).
        for (std::size_t k = 0; k < 3; ++k)
        {
            auto& s = p.satellite[k];
            s.enabled = ((block / (7 + 3 * static_cast<int> (k))) % 2) == 0;
            s.mode = static_cast<osp::satellite::Mode> ((block / 11 + static_cast<int> (k)) % 3);
            s.timing = (block / 13) % 2 == 0 ? osp::satellite::Timing::sync : osp::satellite::Timing::free;
            s.retrigger = (block / 17) % 2 == 0 ? osp::satellite::Retrigger::cycle : osp::satellite::Retrigger::note;
            s.depth = 0.9;
            s.width = 0.3;
        }
        p.morph.enabled = (block / 40) % 2 == 1;
        synth.setPatch (p);
        timing.ppq += 512.0 / 48000.0 * 140.0 / 60.0;
        synth.setTransport (timing);
        const int channel = 1 + (block % 8);   // (1: every card; 2..8: card masks)
        const std::uint8_t noteOn[3] { static_cast<std::uint8_t> (0x90 | (channel - 1)), static_cast<std::uint8_t> (40 + block % 40), 100 };
        const std::uint8_t noteOff[3] { static_cast<std::uint8_t> (0x80 | (channel - 1)), static_cast<std::uint8_t> (40 + (block + 30) % 40), 0 };
        synth.handleMidi (noteOn, 3);
        synth.render (left.data(), right.data(), 256);
        synth.handleMidi (noteOff, 3);
        synth.render (left.data(), right.data(), 256);
    }
    counting = false;
    CHECK (allocations.load() == 0);
}

TEST_CASE ("realtime: the rhythm loop player (sync, free, swaps, seeks) never allocates", "[realtime][rhythm]")
{
    // Two loops prepared beforehand (a loader thread's work), then only playing.
    std::vector<float> a (96000, 0.0f), b (72000, 0.0f);
    for (std::size_t k = 0; k < a.size(); k += 24000)
        for (std::size_t i = 0; i < 300; ++i)
            a[k + i] = 0.5f * std::sin (0.26f * static_cast<float> (i));
    for (std::size_t i = 0; i < b.size(); ++i)
        b[i] = 0.1f * std::sin (0.01f * static_cast<float> (i));
    const auto loopA = andor::analog::rhythm::prepareLoop (a.data(), a.data(), a.size(), 48000.0);
    const auto loopB = andor::analog::rhythm::prepareLoop (b.data(), b.data(), b.size(), 48000.0);
    andor::analog::rhythm::LoopPlayer player;
    player.prepare (48000.0);
    std::vector<float> l (512), r (512), lev (512), tr (512);
    counting = true;
    double q = 0.0;
    for (int block = 0; block < 400; ++block)
    {
        if (block % 50 == 0)
            player.setLoop (block % 100 == 0 ? loopA.get() : loopB.get());
        if (block == 200)
            q = 3.5;   // (a seek)
        andor::analog::rhythm::LoopPlayer::Settings s;
        s.sync = (block / 30) % 2 == 0;
        s.tempoFactor = block % 3 == 0 ? 2.0 : 1.0;
        player.render (l.data(), r.data(), 0.8f, lev.data(), tr.data(), 512, q, (100.0 + block * 0.1) / 60.0 / 48000.0, block % 70 != 0, s);
        q += 512 * (100.0 + block * 0.1) / 60.0 / 48000.0;
    }
    counting = false;
    CHECK (allocations.load() == 0);
}

TEST_CASE ("realtime: card C's external bus and GLOBAL MORPH (on / off, MORPH switched, the position moving, a latency) never allocate", "[realtime][external]")
{
    // EXPERIMENT (docs/experiments/KONTAKT_EXTERNAL_MORPH.md): the bus is the host's block (here
    // a stand-in signal); the synth mixes or morphs it with A + B.
    Patch p = measurementPatch();
    p.blend = 0.5;
    p.hybridType = HybridType::external;
    auto s = makeSynth (p, 48000.0, 4, 512);
    std::vector<float> l (512), r (512), cl (512), cr (512);
    for (std::size_t i = 0; i < cl.size(); ++i)
    {
        cl[i] = 0.2f * std::sin (0.031f * static_cast<float> (i));
        cr[i] = 0.2f * std::sin (0.029f * static_cast<float> (i));
    }
    counting = true;
    for (int block = 0; block < 400; ++block)
    {
        if (block % 40 == 0)
        {
            const std::uint8_t on[3] { 0x90, static_cast<std::uint8_t> (48 + block / 40), 100 };
            s->handleMidi (on, 3);
        }
        const bool active = (block / 100) % 2 == 0 || block > 300;
        const bool morph = (block / 25) % 2 == 1;
        s->setExternal (active, cl.data(), cr.data(), 512, 0.9f, morph, 0.5 + 0.5 * std::sin (block * 0.05), block > 200 ? 256 : 0);
        s->render (l.data(), r.data(), 512);
    }
    counting = false;
    CHECK (allocations.load() == 0);
}

TEST_CASE ("realtime: D / AUX's bus allocates nothing - its four modes (LIVE INPUT), switches, MUTE, POWER, its filter, latency and MORPH TO D", "[realtime][aux]")
{
    Patch p = measurementPatch();
    p.aux.enabled = true;
    p.aux.mode = Patch::Aux::Mode::noise;
    p.aux.modAmount = 0.5;
    std::vector<float> ticks (static_cast<std::size_t> (2 * 48000), 0.0f);
    for (std::size_t k = 0; k < 4; ++k)
        for (std::size_t i = 0; i < 1000; ++i)
            ticks[k * 24000 + i] = 0.5f * std::sin (0.26f * static_cast<float> (i)) * std::exp (-static_cast<float> (i) / 300.0f);
    const auto loop = andor::analog::rhythm::prepareLoop (ticks.data(), ticks.data(), ticks.size(), 48000.0);
    auto s = makeSynth (p, 48000.0, 4, 512);
    s->setAuxLoop (loop.get());
    std::vector<float> l (512), r (512), cl (512), cr (512);
    for (std::size_t i = 0; i < cl.size(); ++i)
        cl[i] = cr[i] = 0.2f * std::sin (0.031f * static_cast<float> (i));
    counting = true;
    for (int block = 0; block < 400; ++block)
    {
        if (block % 40 == 0)
        {
            const std::uint8_t on[3] { 0x90, static_cast<std::uint8_t> (48 + block / 40), 100 };
            s->handleMidi (on, 3);
            s->auxKeyEvent (on, 3);
        }
        if (block % 50 == 0)
        {
            p.aux.mode = static_cast<Patch::Aux::Mode> ((block / 50) % 4);   // (LIVE INPUT included)
            p.aux.morphExclusive = (block / 50) % 3 != 0;                      // (MORPH TO D: D only through the stage)
            p.aux.heard = (block / 50) % 4 != 3;
            p.aux.filterFollow = (block / 100) % 2 == 0;
            p.aux.enabled = block < 350;
            p.aux.morph = (block / 50) % 3 == 1 ? 0.0 : 0.3 * static_cast<double> ((block / 50) % 4);   // (D MORPH on, off, back)
            p.aux.pitchSemis = (block / 50) % 2 == 0 ? 7.0 : -12.0;
            s->setPatch (p);
        }
        s->setAuxExternal (cl.data(), cr.data(), 512, block > 200 ? 256 : 0);
        s->setAuxLive (block % 7 == 0 ? nullptr : cl.data(), cr.data(), 512);   // (the sidechain, now and then off)
        s->render (l.data(), r.data(), 512);
    }
    counting = false;
    CHECK (allocations.load() == 0);
}
