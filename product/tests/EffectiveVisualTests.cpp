// Universal modulation-aware visual feedback (ANDOR Analog): a picture draws what the DSP plays.
// MOVEMENT SHAPER first (both lanes), then the product's own pictures. A control moved by
// modulation draws as the same control set by hand to that value; the knob keeps its stored
// value; nothing is written back.

#include "AnalogEngine.h"
#include "AnalogParameters.h"
#include "MacroPopups.h"
#include "ShellProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <vector>

namespace
{
    using andor::shell::ShellProcessor;
    using Catch::Approx;
    namespace mod = osp::mod;

    constexpr double rate = 48000.0;
    constexpr int block = 256;

    void set (ShellProcessor& p, const juce::String& id, float value)
    {
        auto* param = p.parameters.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    float stored (ShellProcessor& p, const juce::String& id)
    {
        auto* param = p.parameters.getParameter (id);
        REQUIRE (param != nullptr);
        return param->convertFrom0to1 (param->getValue());
    }

    void run (ShellProcessor& p, int blocks, bool noteAtStart = true)
    {
        juce::AudioBuffer<float> buffer (2, block);
        for (int i = 0; i < blocks; ++i)
        {
            juce::MidiBuffer midi;
            if (i == 0 && noteAtStart)
                midi.addEvent (juce::MidiMessage::noteOn (1, 57, static_cast<juce::uint8> (100)), 0);
            buffer.clear();
            p.processBlock (buffer, midi);
        }
    }

    /** The pictures in `root` (component ID "picture"), side by side - the knobs left out. */
    juce::Image picturesOf (juce::Component& root)
    {
        std::vector<juce::Component*> found;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
            for (auto* child : c.getChildren())
            {
                if (child->getComponentID() == "picture" && child->isVisible())
                    found.push_back (child);
                walk (*child);
            }
        };
        walk (root);
        int width = 0, height = 0;
        for (auto* c : found)
        {
            width += c->getWidth();
            height = std::max (height, c->getHeight());
        }
        juce::Image out (juce::Image::ARGB, std::max (1, width), std::max (1, height), true);
        juce::Graphics g (out);
        int x = 0;
        for (auto* c : found)
        {
            g.drawImageAt (c->createComponentSnapshot (c->getLocalBounds(), true, 1.0f), x, 0);
            x += c->getWidth();
        }
        return out;
    }

    int pixelsDiffering (const juce::Image& a, const juce::Image& b, int tolerance = 2)
    {
        if (a.getBounds() != b.getBounds())
            return a.getWidth() * a.getHeight() + 1;
        int n = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
            {
                const auto p = a.getPixelAt (x, y), q = b.getPixelAt (x, y);
                if (std::abs (p.getRed() - q.getRed()) > tolerance || std::abs (p.getGreen() - q.getGreen()) > tolerance
                    || std::abs (p.getBlue() - q.getBlue()) > tolerance || std::abs (p.getAlpha() - q.getAlpha()) > tolerance)
                    ++n;
            }
        return n;
    }

    /** The MOVEMENT rack's pictures with lane `lane` shown. */
    juce::Image movementRackPicture (ShellProcessor& p, int lane)
    {
        auto module = osp::plugin::createRackModule (osp::plugin::rack::Kind::movement, p);
        REQUIRE (module != nullptr);
        module->setSize (1380, 300);
        osp::plugin::showRackMovementLane (*module, lane);
        module->setSize (1380, 300);
        return picturesOf (*module);
    }

    /** Both lanes on, in SHAPER (B at its own INTENSITY), MOVEMENT stored at `motion`, the mod
        wheel (held at full) on MOVEMENT at `wheelDepth` percent. */
    std::unique_ptr<ShellProcessor> shaperRig (float motion, float wheelDepth)
    {
        auto p = std::make_unique<ShellProcessor>();
        p->prepareToPlay (rate, block);
        const auto a = p->movementLane (0), b = p->movementLane (1);
        set (*p, a.prefix + "mode", 4.0f);
        set (*p, b.prefix + "mode", 4.0f);
        set (*p, b.prefix + "shaper.pattern", 3.0f);
        if (b.onId.isNotEmpty())
            set (*p, b.onId, 1.0f);
        if (b.levelId.isNotEmpty())
            set (*p, b.levelId, 70.0f);
        set (*p, "motion", motion);
        if (wheelDepth != 0.0f)
        {
            p->assignModulation (mod::Source::modWheel, mod::Dest::movement, wheelDepth);
            p->setScreenModWheel (1.0f);
        }
        run (*p, 40);
        return p;
    }
}

TEST_CASE ("effective values: MOVEMENT SHAPER, lanes A and B - modulated to 80 % each draws the hand-set 80 %", "[analog][effective][shaper]")
{
    const auto byHand80 = shaperRig (80.0f, 0.0f);
    const auto byHand20 = shaperRig (20.0f, 0.0f);
    const auto modulated = shaperRig (20.0f, 60.0f);

    osp::plugin::InstrumentHost::EffectiveValue v;
    REQUIRE (modulated->effectiveValue ("motion", v));
    CHECK (v.value == Approx (80.0f).margin (0.01));
    CHECK_FALSE (byHand80->effectiveValue ("motion", v));
    CHECK (stored (*modulated, "motion") == Approx (20.0f));   // (the knob: stored, never written back)

    for (const int lane : { 0, 1 })
    {
        DYNAMIC_SECTION ("lane " << (lane == 0 ? "A" : "B"))
        {
            const auto hand80 = movementRackPicture (*byHand80, lane), hand20 = movementRackPicture (*byHand20, lane),
                       mod80 = movementRackPicture (*modulated, lane);
            REQUIRE (hand80.getWidth() > 10);
            INFO ("pixels differing: hand 80 vs hand 20 " << pixelsDiffering (hand80, hand20) << ", hand 80 vs modulated 80 " << pixelsDiffering (hand80, mod80));
            CHECK (pixelsDiffering (hand80, hand20) > 200);
            // (A handful of edge pixels may differ: the modulated 80 is 80 within 0.01, and on MSVC
            // that moved the anti-aliasing of 3 pixels in a picture of about 400,000; GCC and Clang 0.)
            CHECK (pixelsDiffering (hand80, mod80) <= 8);
        }
    }
}

TEST_CASE ("effective values: the knob's value is not written back, presets keep it; an LFO's sweep is reported as its span", "[analog][effective]")
{
    auto p = shaperRig (50.0f, 0.0f);
    p->assignModulation (mod::Source::lfo1, mod::Dest::movement, 30.0f);
    float low = 1000.0f, high = -1000.0f;
    osp::plugin::InstrumentHost::EffectiveValue v;
    for (int i = 0; i < 16; ++i)
    {
        run (*p, 12, false);
        REQUIRE (p->effectiveValue ("motion", v));
        low = std::min (low, v.value);
        high = std::max (high, v.value);
        CHECK (v.low <= v.value);
        CHECK (v.high >= v.value);
        CHECK (stored (*p, "motion") == Approx (50.0f));
    }
    INFO ("swept " << low << " .. " << high);
    CHECK (high - low > 20.0f);
    // The saved state holds the knob's 50 %, whatever the LFO played.
    juce::MemoryBlock state;
    p->getStateInformation (state);
    ShellProcessor q;
    q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (stored (q, "motion") == Approx (50.0f));
}

namespace
{
    /** The product destination for `parameterId` (its routing slot). */
    mod::Dest productDestOf (const juce::String& parameterId)
    {
        const auto& all = andor::analog::params::modulationDestinations();
        for (std::size_t k = 0; k < all.size(); ++k)
            if (juce::String (all[k].id) == parameterId)
                return mod::productDest (static_cast<int> (k));
        FAIL ("no destination " << parameterId);
        return mod::Dest::none;
    }

    std::vector<float> cardCycle (ShellProcessor& p, int layer)
    {
        osp::plugin::InstrumentHost::GeneratedSource g;
        REQUIRE (p.generatedSource (layer, g));
        return g.cycle;
    }

    std::unique_ptr<ShellProcessor> widthRig (float width, float wheelDepth, bool hold = true)
    {
        auto p = std::make_unique<ShellProcessor>();
        p->prepareToPlay (rate, block);
        set (*p, andor::analog::params::osc1Wave, 1.0f);   // PULSE: its width is drawn
        set (*p, andor::analog::params::osc1Width, width);
        if (wheelDepth != 0.0f)
        {
            p->assignModulation (mod::Source::modWheel, productDestOf (andor::analog::params::osc1Width), wheelDepth);
            p->setScreenModWheel (1.0f);
        }
        run (*p, 20, hold);
        return p;
    }
}

TEST_CASE ("effective values: a per-voice control (OSC 1 WIDTH) - the sounding voice's width draws the card's waveform as the hand-set width; silent, the stored one", "[analog][effective][voice]")
{
    const auto byHand = widthRig (80.0f, 0.0f);
    const auto stored20 = widthRig (20.0f, 0.0f);
    // The wheel's 60 % of the knob's travel (2 .. 98 %) on a 20 % base: 20 + 0.6 x 96 = 77.6 %.
    const auto modulated = widthRig (20.0f, 60.0f);
    osp::plugin::InstrumentHost::EffectiveValue v;
    REQUIRE (modulated->effectiveValue (andor::analog::params::osc1Width, v));
    CHECK (v.value == Approx (77.6f).margin (0.05));
    CHECK (stored (*modulated, andor::analog::params::osc1Width) == Approx (20.0f));
    // The same picture as the knob set by hand there (the knob snaps to 0.1 %, the moved value
    // need not: at most the one sample on each edge may fall on the other side).
    const auto handSet = widthRig (77.6f, 0.0f);
    const auto a = cardCycle (*modulated, 0), b = cardCycle (*handSet, 0);
    REQUIRE (a.size() == b.size());
    int differing = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        differing += std::abs (a[i] - b[i]) > 1.0e-3f ? 1 : 0;
    CHECK (differing <= 2);
    CHECK (cardCycle (*modulated, 0) != cardCycle (*stored20, 0));    // (not the stored one)
    juce::ignoreUnused (byHand);

    // No voice sounds: no per-voice value of its own - the stored width is drawn.
    const auto silent = widthRig (20.0f, 60.0f, false);
    CHECK_FALSE (silent->effectiveValue (andor::analog::params::osc1Width, v));
    CHECK (cardCycle (*silent, 0) == cardCycle (*stored20, 0));
}

TEST_CASE ("effective values: two routes on one control are summed, and stop where the DSP clamps", "[analog][effective]")
{
    auto p = shaperRig (70.0f, 0.0f);
    p->assignModulation (mod::Source::modWheel, mod::Dest::movement, 25.0f);
    p->setScreenModWheel (1.0f);
    run (*p, 40, false);   // (the screen's wheel glides to where it is put)
    osp::plugin::InstrumentHost::EffectiveValue v;
    REQUIRE (p->effectiveValue ("motion", v));
    CHECK (v.value == Approx (95.0f).margin (0.01));   // 70 + 25
    // A second route (LFO 2, 40 %): at its peaks the sum passes 100 %; MOVEMENT stops there.
    p->assignModulation (mod::Source::lfo2, mod::Dest::movement, 40.0f);
    float highest = 0.0f;
    for (int i = 0; i < 40; ++i)
    {
        run (*p, 8, false);
        REQUIRE (p->effectiveValue ("motion", v));
        CHECK (v.high <= 100.0f);
        highest = std::max (highest, v.high);
    }
    CHECK (highest == Approx (100.0f));
    // Opposite polarity: wheel down 50 % from 70 % plays 20 %.
    auto q = shaperRig (70.0f, -50.0f);
    REQUIRE (q->effectiveValue ("motion", v));
    CHECK (v.value == Approx (20.0f).margin (0.01));
}
