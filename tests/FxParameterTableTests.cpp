#include "fx/Effects.h"
#include "fx/FxParameterTable.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <set>

// The FX library's parameter table (dsp/fx/FxParameterTable): the one definition of every new
// effect's settings, free of any plugin framework, so that the product's host parameters and
// any other instrument that plays these processors offer the same settings.

using namespace andor::analog;

TEST_CASE ("fx parameters: the table - names, ranges and defaults of every new effect; the original four have none", "[fx][parameters]")
{
    for (int t = 0; t < fx::typeCount; ++t)
    {
        const auto type = static_cast<fx::Type> (t);
        INFO (fx::typeName (type));
        const auto& specs = fx::parameterSpecs (type);
        if (fx::isLegacy (type))
        {
            CHECK (specs.empty());   // (theirs are the instrument's own parameters)
            continue;
        }
        REQUIRE (specs.size() >= 6);
        // ON, MIX and OUTPUT come first, in that order, for every effect.
        CHECK (specs[0].name == "on");
        CHECK (specs[1].name == "mix");
        CHECK (specs[2].name == "output");
        std::set<std::string> names;
        for (const auto& s : specs)
        {
            INFO (s.name);
            CHECK (names.insert (s.name).second);
            CHECK_FALSE (s.title.empty());
            REQUIRE (s.write != nullptr);
            CHECK (s.low <= s.fallback);
            CHECK (s.fallback <= s.high);
            using Kind = fx::ParameterSpec::Kind;
            if (s.kind == Kind::choice)
            {
                REQUIRE (s.choices.size() >= 2);
                CHECK (s.low == 0.0f);
                CHECK (s.high == static_cast<float> (s.choices.size() - 1));
            }
            else if (s.kind == Kind::toggle)
            {
                CHECK (s.low == 0.0f);
                CHECK (s.high == 1.0f);
            }
            else
            {
                CHECK (s.low < s.high);
                CHECK_FALSE (s.unit.empty());
            }
            // Writing any value of the range into any slot is safe (the audio thread does it).
            fx::Settings settings;
            for (int slot = 0; slot < fx::capacity[static_cast<std::size_t> (t)]; ++slot)
                for (const float v : { s.low, s.fallback, s.high })
                    s.write (settings, slot, v);
        }
    }
    // The saved order is the table's: a few fixed points (a parameter is never moved or renamed).
    const auto& comp = fx::parameterSpecs (fx::Type::compressor);
    CHECK (comp[3].name == "mode");
    CHECK (comp[4].name == "threshold");
    CHECK (comp.back().name == "multi.match");
    CHECK (fx::parameterSpecs (fx::Type::retro).back().name == "seed");
    CHECK (fx::parameterSpecs (fx::Type::eq)[3].name == "band1.on");
}

TEST_CASE ("fx parameters: a write lands in the slot's settings, in the processors' units", "[fx][parameters]")
{
    const auto find = [] (fx::Type type, const char* name) -> const fx::ParameterSpec& {
        for (const auto& s : fx::parameterSpecs (type))
            if (s.name == name)
                return s;
        FAIL ("no parameter " << name);
        static const fx::ParameterSpec none;
        return none;
    };
    fx::Settings s;
    find (fx::Type::compressor, "threshold").write (s, 1, -33.0f);
    CHECK (s.comp[1].thresholdDb == -33.0f);
    CHECK (s.comp[0].thresholdDb != -33.0f);   // (the other slot is untouched)
    find (fx::Type::compressor, "mix").write (s, 0, 40.0f);
    CHECK (s.comp[0].mix == Catch::Approx (0.4f));   // (a percentage is 0..1 in the settings)
    find (fx::Type::compressor, "detector").write (s, 0, 1.0f);
    CHECK (s.comp[0].peak);
    find (fx::Type::phaser, "division").write (s, 0, 6.0f);
    CHECK (s.phaser[0].division == 6);
    find (fx::Type::shifter, "direction").write (s, 0, 1.0f);
    CHECK (s.shifter[0].down);
    find (fx::Type::retro, "seed").write (s, 1, 41.6f);
    CHECK (s.retro[1].seed == 42);   // (rounded to the nearest)
    find (fx::Type::eq, "band3.gain").write (s, 0, -6.0f);
    CHECK (s.eq[0].bands[2].gainDb == -6.0f);
    find (fx::Type::filter, "on").write (s, 0, 0.0f);
    CHECK_FALSE (fx::isOn (s, { fx::Type::filter, 0 }));
}
