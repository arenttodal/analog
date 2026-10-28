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
