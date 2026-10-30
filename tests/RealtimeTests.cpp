// The audio thread's rules: no heap allocation while rendering (any patch change, notes,
// controllers, voice stealing included).

#include "TestUtil.h"

#include "FactoryPatches.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
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
    Synth synth;
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
    Synth synth;
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
    Synth synth;
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
    Synth synth;
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
