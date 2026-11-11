// USER MACROS V1.1, gate 1: the pure core - anchored zero-jump maths, sanitising, the
// bounded table and its lock-free hand-over (docs/USER_MACROS_V1_1.md).

#include "UserMacroCore.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <thread>

using namespace andor::analog::usermacro;
using Catch::Approx;

namespace
{
    Mapping made (float macroNow, float targetNow)
    {
        Mapping m;
        m.anchorMacro = macroNow;
        m.anchorTarget = targetNow;
        m.low = m.high = targetNow;   // as made: no influence
        return m;
    }
}

TEST_CASE ("M06: a new assignment never jumps - neutral at 0 %, 50 %, 73 %, 100 %, and at every position", "[usermacro][unit]")
{
    for (const float a : { 0.0f, 0.5f, 0.73f, 1.0f })
    {
        INFO ("made at " << a);
        const auto m = made (a, 0.41f);
        for (double x = 0.0; x <= 1.0; x += 0.01)
            CHECK (offsetOf (m, x) == 0.0);   // L = H = b0: nothing anywhere
        // Shaped afterwards: still exactly nothing at the anchor (editing LOW / HIGH never jumps).
        auto shaped = m;
        shaped.low = 0.1f;
        shaped.high = 0.8f;
        CHECK (offsetOf (shaped, a) == 0.0);
    }
}

TEST_CASE ("M07: the range - LOW at 0, HIGH at 1, linear between; inverted when LOW is above HIGH", "[usermacro][unit]")
{
    auto m = made (0.0f, 0.41f);   // made at 0: only the HIGH side
    m.high = 0.80f;
    CHECK (offsetOf (m, 0.0) == Approx (0.0));
    CHECK (offsetOf (m, 0.5) == Approx (0.195));
    CHECK (offsetOf (m, 1.0) == Approx (0.39));
    // Made at 73 %: both sides from there.
    auto mid = made (0.73f, 0.5f);
    mid.low = 0.2f;
    mid.high = 0.9f;
    CHECK (offsetOf (mid, 0.0) == Approx (-0.3));
    CHECK (offsetOf (mid, 1.0) == Approx (0.4));
    CHECK (offsetOf (mid, 0.365) == Approx (-0.15));
    // Inverted (SWAP): the same code.
    auto inv = made (0.0f, 0.6f);
    inv.high = 0.1f;
    CHECK (offsetOf (inv, 1.0) == Approx (-0.5));
    // EASE shapes the distance, the ends are the same.
    auto eased = m;
    eased.curve = Curve::ease;
    CHECK (offsetOf (eased, 1.0) == Approx (0.39));
    CHECK (offsetOf (eased, 0.25) < offsetOf (m, 0.25));
    // Bypassed: nothing.
    m.enabled = false;
    CHECK (offsetOf (m, 1.0) == 0.0);
    // Out-of-range macro values are clamped.
    m.enabled = true;
    CHECK (offsetOf (m, 7.0) == Approx (0.39));
    CHECK (offsetOf (m, -3.0) == 0.0);
}

TEST_CASE ("M08: two macros on one target add, then clamp once (the caller's job, shown here)", "[usermacro][unit]")
{
    auto a = made (0.0f, 0.5f);
    a.high = 0.9f;
    auto b = made (0.0f, 0.5f);
    b.high = 1.0f;
    const double base = 0.5;
    const double total = std::clamp (base + offsetOf (a, 1.0) + offsetOf (b, 1.0), 0.0, 1.0);
    CHECK (total == Approx (1.0));
    CHECK (offsetOf (a, 1.0) + offsetOf (b, 1.0) == Approx (0.9));
}

TEST_CASE ("M12 (core): bad numbers are made safe - NaN, infinities, out of range", "[usermacro][unit]")
{
    Mapping m;
    m.anchorMacro = std::numeric_limits<float>::quiet_NaN();
    m.anchorTarget = 3.0f;
    m.low = -std::numeric_limits<float>::infinity();
    m.high = 0.7f;
    const auto s = sanitised (m);
    CHECK (s.anchorMacro == 0.0f);
    CHECK (s.anchorTarget == 1.0f);
    CHECK (s.low == 1.0f);   // (a non-finite end: neutral)
    CHECK (s.high == 0.7f);
    for (double x = 0.0; x <= 1.0; x += 0.1)
        CHECK (std::isfinite (offsetOf (s, x)));
}

TEST_CASE ("USER MACROS: the triple buffer hands the latest table over, never torn, without waiting", "[usermacro][unit]")
{
    TripleBuffer<Snapshot> buffer;
    CHECK (buffer.read().count == 0);
    Snapshot s;
    s.count = 1;
    s.maps[0].index = 7;
    buffer.publish (s);
    CHECK (buffer.read().count == 1);
    CHECK (buffer.read().maps[0].index == 7);   // (read again: the same)
    // A writer far ahead of the reader: the reader sees the newest complete one.
    for (int i = 2; i <= 10; ++i)
    {
        s.count = i;
        s.maps[0].index = static_cast<std::int16_t> (i * 3);
        buffer.publish (s);
    }
    CHECK (buffer.read().count == 10);
    CHECK (buffer.read().maps[0].index == 30);
    // Two threads: every table the reader sees is one the writer wrote whole.
    std::atomic<bool> done { false };
    bool consistent = true;
    std::thread reader ([&] {
        while (! done.load())
        {
            const auto& t = buffer.read();
            consistent = consistent && t.maps[0].index == static_cast<std::int16_t> (t.count * 3) && t.maps[1].index == static_cast<std::int16_t> (t.count);
        }
    });
    for (int i = 11; i < 20000; ++i)
    {
        s.count = i;
        s.maps[0].index = static_cast<std::int16_t> (i * 3);
        s.maps[1].index = static_cast<std::int16_t> (i);
        buffer.publish (s);
    }
    done = true;
    reader.join();
    CHECK (consistent);
}
