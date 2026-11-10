#include "FxRouting.h"
#include "FxIdentity.h"
#include "FxPresets.h"
#include "FxRack.h"
#include "AnalogEngine.h"
#include "AnalogParameters.h"
#include "OspLookAndFeel.h"
#include "ShellEditor.h"
#include "ShellProcessor.h"
#include "fx/FxProcessor.h"

namespace andor::analog
{

namespace fonts = osp::plugin::fonts;
using fx::InstanceId;
using fx::Type;

namespace
{
    // The approved routing's palette (ANDOR_LINEAR_ROUTING_SETTINGS_MENU_APPROVAL).
    const juce::Colour ink { 0xff313532 }, soft { 0xff716d65 }, weak { 0xff9c9489 }, edge { 0xffd6cdc1 }, line { 0xffded5c9 };
    const juce::Colour wire { 0xffa3978a }, wireStrong { 0xff9fb3a8 }, sage { 0xff8caaa0 }, warm { 0xffbc7d51 }, green { 0xff658276 };

    juce::Font font (float size, fonts::Weight weight = fonts::Weight::regular, float tracking = 0.0f) { return fonts::make (size, weight, tracking); }

    // The engine mixer's cards (A, B, C: their own colours, as on the cards).
    juce::Colour mixerTileColour (int i)
    {
        const juce::Colour c[3] { juce::Colour (0xffc77f50), juce::Colour (0xff819aaf), juce::Colour (0xff879f8c) };
        return c[juce::jlimit (0, 2, i)];
    }

    AnalogEngine& engineOf (osp::plugin::InstrumentHost& host) { return dynamic_cast<AnalogEngine&> (dynamic_cast<andor::shell::ShellProcessor&> (host).productEngine()); }

    juce::String textOf (osp::plugin::InstrumentHost& host, const juce::String& id)
    {
        if (auto* p = host.apvts().getParameter (id))
            return p->getCurrentValueAsText();
        return {};
    }

    juce::String percentOf (osp::plugin::InstrumentHost& host, const juce::String& id)
    {
        return juce::String (juce::roundToInt (host.parameterValue (id))) + " %";
    }

    struct LibraryEntry
    {
        Type type;
        const char* category;
        const char* description;
    };
    const LibraryEntry libraryEntries[] {
        { Type::compressor, "DYNAMICS", "Clean, FET 76 or three-band (OTT)" },
        { Type::eq, "TONE", "Four parametric bands, high and low pass" },
        { Type::filter, "TONE", "A movable filter: LP, HP, BP, comb, formant" },
        { Type::drive, "TONE", "The instrument's saturation: tube, tape, crunch" },
        { Type::spread, "WIDTH & MOTION", "Hyper voices and dimension ensemble" },
        { Type::phaser, "WIDTH & MOTION", "Swept all-pass notches" },
        { Type::flanger, "WIDTH & MOTION", "A short swept comb" },
        { Type::movement, "WIDTH & MOTION", "The instrument's MOVEMENT lanes A and B" },
        { Type::echo, "SPACE & TIME", "The instrument's tape, BBD and ASCEND echo" },
        { Type::space, "SPACE & TIME", "The instrument's rooms, ABYSS, VINTAGE, BLOOM" },
        { Type::convolution, "SPACE & TIME", "A real space from an impulse response" },
        { Type::shifter, "SPACE & TIME", "Frequency shift and spectral blur" },
        { Type::retro, "CHARACTER", "An aging medium: tape, vinyl, 80s / 90s digital, codec" },
        { Type::utility, "UTILITIES", "Gain, pan, width, polarity, mono bass" },
    };
}

juce::Colour fxAccent (Type type)
{
    // V5: the device's enamel (docs/VISUAL_IDENTITY_INDEX.md) - one hue per device everywhere.
    return visualIdentityFor (type).enamel;
}

juce::Path fxIcon (Type type)
{
    // 24 x 24 strokes (the approved set for the original four).
    juce::Path p;
    auto svg = [&p] (const char* d) { p.addPath (juce::Drawable::parseSVGPath (d)); };
    switch (type)
    {
        case Type::drive: svg ("M12 3C8 8 16 10 11 16c5-1 8-6 6-10 6 7 2 15-5 15-7 0-11-6-7-12 0 4 4 6 7 4Z"); break;
        case Type::movement: svg ("M2 13c3-8 5-8 8 0s5 8 8 0 3-5 4-5"); break;
        case Type::echo: svg ("M7 8h10l-3-3M17 8l-3 3M17 16H7l3-3m-3 3 3 3"); break;
        case Type::space:
            p.addEllipse (4.0f, 4.0f, 16.0f, 16.0f);
            svg ("M12 2v20M2 12h20M6.4 6.4l11.2 11.2M17.6 6.4 6.4 17.6");
            break;
        case Type::spread: svg ("M4 12h16M7 8l-4 4 4 4M17 8l4 4-4 4M12 6v12"); break;
        case Type::compressor: svg ("M4 18 10 10h10M4 6h16M12 14v6"); break;
        case Type::eq: svg ("M2 15c3 0 4-8 7-8s3 10 6 10 4-6 7-6"); break;
        case Type::filter: svg ("M2 8h9c4 0 5 1 6 4s2 6 5 6"); break;
        case Type::phaser: svg ("M2 12c2-6 4-6 6 0s4 6 6 0 4-6 6 0M2 12h20"); break;
        case Type::flanger: svg ("M3 6v12M8 8v8M13 5v14M18 9v6M22 11v2"); break;
        case Type::shifter: svg ("M4 16h6l4-8h6M17 5l3 3-3 3"); break;
        case Type::convolution: svg ("M3 12h2l2-7 3 14 3-10 2 6 2-3h4"); break;
        case Type::utility: svg ("M6 4v16M12 4v16M18 4v16M4 9h4M10 14h4M16 7h4"); break;
        case Type::retro:
            // Two reels and the tape between them.
            p.addEllipse (2.5f, 7.0f, 8.0f, 8.0f);
            p.addEllipse (13.5f, 7.0f, 8.0f, 8.0f);
            svg ("M6.5 15h11M3 19h18");
            break;
        default: break;
    }
    return p;
}

//==============================================================================
/** A borderless glyph button (the gear, the collapse, + ADD EFFECT). */
class FxRoutingView::IconButton final : public juce::Button
{
public:
    enum class Kind { gear, collapse, add };
    IconButton (Kind k, const juce::String& name) : juce::Button (name), kind (k) { setTitle (name); }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat();
        if (kind == Kind::collapse)
        {
            // The racks' borderless X (the same size and stroke): the view closes, back to the cards.
            const auto c = r.getCentre();
            if (over || down)
            {
                g.setColour (osp::plugin::design::colour::text.withAlpha (down ? 0.11f : 0.07f));
                g.fillEllipse (juce::Rectangle<float> (24.0f, 24.0f).withCentre (c));
            }
            juce::Path cross;
            const float a = 4.6f;
            cross.startNewSubPath (c.x - a, c.y - a);
            cross.lineTo (c.x + a, c.y + a);
            cross.startNewSubPath (c.x + a, c.y - a);
            cross.lineTo (c.x - a, c.y + a);
            g.setColour (osp::plugin::design::colour::text.withAlpha (over || down ? 1.0f : 0.8f));   // (the ARPEGGIATOR's ink)
            g.strokePath (cross, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            return;
        }
        if (over || down || getToggleState())
        {
            g.setColour (juce::Colour (0xffe9e2d9));
            g.fillRoundedRectangle (r, 5.0f);
        }
        const auto colour = (over || getToggleState()) ? juce::Colour (0xff47675d) : juce::Colour (0xff7e7e76);
        g.setColour (colour);
        if (kind == Kind::add)
        {
            // A small plus and ADD EFFECT (12 px): found at a glance, never a toolbar.
            const auto f = font (12.0f, fonts::Weight::semibold, 0.05f);
            const float tw = juce::GlyphArrangement::getStringWidth (f, "ADD EFFECT");
            const float x0 = r.getCentreX() - 0.5f * (tw + 16.0f);
            juce::Path plus;
            plus.startNewSubPath (x0 + 4.5f, r.getCentreY() - 4.5f);
            plus.lineTo (x0 + 4.5f, r.getCentreY() + 4.5f);
            plus.startNewSubPath (x0, r.getCentreY());
            plus.lineTo (x0 + 9.0f, r.getCentreY());
            g.strokePath (plus, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setFont (f);
            g.drawText ("ADD EFFECT", juce::Rectangle<float> (x0 + 16.0f, r.getY(), tw + 4.0f, r.getHeight()), juce::Justification::centredLeft, false);
            return;
        }
        // The gear: a ring with eight teeth.
        const auto c = r.getCentre();
        juce::Path gearPath;
        for (int i = 0; i < 8; ++i)
        {
            juce::Path tooth;
            tooth.addRectangle (-1.4f, -7.5f, 2.8f, 3.0f);
            tooth.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::twoPi * static_cast<float> (i) / 8.0f));
            gearPath.addPath (tooth);
        }
        gearPath.applyTransform (juce::AffineTransform::translation (c));
        g.fillPath (gearPath);
        g.drawEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (c), 1.5f);
        g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (c));
    }

private:
    Kind kind;
};

//==============================================================================
/** V5: a tile's small real knob - the instrument's own knob, on the same parameter as the
    effect's rack (a SliderAttachment: automation, undo and every other view follow it). It
    takes its own mouse (a drag on it never drags the tile); its value shows while hovered or
    turned. */
class FxRoutingView::TileKnob final : public juce::Slider
{
public:
    TileKnob (FxRoutingView& v, osp::plugin::InstrumentHost& host, fx::InstanceId tile, juce::String parameter, juce::String caption, juce::Colour arc)
        : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox), view (v), owner (tile), parameterId (std::move (parameter)), label (std::move (caption))
    {
        getProperties().set ("mini", true);
        getProperties().set ("arc", static_cast<juce::int64> (arc.getARGB()));
        setRotaryParameters (osp::plugin::OspLookAndFeel::rotaryStart, osp::plugin::OspLookAndFeel::rotaryEnd, true);
        setTitle (label);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        if (auto* p = host.apvts().getParameter (parameterId))
        {
            attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (host.apvts(), parameterId, *this);
            setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
            setTooltip (p->getName (64));
            param = p;
        }
        onValueChange = [this] { view.repaint(); };
    }
    void mouseEnter (const juce::MouseEvent& e) override
    {
        juce::Slider::mouseEnter (e);
        view.repaint();
    }
    void mouseExit (const juce::MouseEvent& e) override
    {
        juce::Slider::mouseExit (e);
        view.repaint();
    }
    /** What the tile writes under the knob: its caption, or its value while touched. */
    juce::String shownText() const
    {
        if ((isMouseOverOrDragging() || hasKeyboardFocus (false)) && param != nullptr)
            return param->getCurrentValueAsText();
        return label;
    }

    FxRoutingView& view;
    fx::InstanceId owner;
    juce::String parameterId, label;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

namespace
{
    struct TileControl
    {
        juce::String id, label;
    };

    /** A tile's knobs (V5 §3.3): the device's most useful real parameters, never new ones -
        those its mode actually uses; a missing one is skipped, not invented. */
    std::vector<TileControl> tileControlsFor (osp::plugin::InstrumentHost& host, InstanceId id)
    {
        std::vector<TileControl> c;
        auto add = [&] (const juce::String& pid, const char* label) {
            if (host.apvts().getParameter (pid) != nullptr && c.size() < 3)
                c.push_back ({ pid, label });
        };
        switch (id.type)
        {
            case Type::movement:
            {
                add ("motion", "Amount");
                switch (juce::roundToInt (host.parameterValue ("movement.mode")))
                {
                    case 0: add ("movement.drift.speed", "Speed"); add ("movement.drift.pitch", "Pitch"); break;
                    case 1: add ("movement.tape.wow", "Wow"); add ("movement.tape.flutter", "Flutter"); break;
                    case 2: add ("movement.chorus.rate", "Rate"); add ("movement.chorus.width", "Depth"); break;
                    case 3: add ("movement.pulse.rate", "Rate"); add ("movement.pulse.shape", "Shape"); break;
                    default: add ("movement.shaper.smooth", "Smooth"); break;
                }
                return c;
            }
            case Type::echo:
                add (host.parameterValue ("echo.sync") >= 0.5f ? "echo.division" : "echo.time", "Time");
                add ("echo.feedback", "Feed");
                add ("echo", "Mix");
                return c;
            case Type::space:
                add ("space.size", "Size");
                add ("space.decay", "Decay");
                add ("space", "Mix");
                return c;
            case Type::drive:
                add ("drive", "Drive");
                add ("drive.tone", "Tone");
                add ("drive.body", "Body");
                return c;
            default: break;
        }
        // The new effects: their quick settings' first three, with short captions.
        static const std::map<juce::String, const char*> shortLabels {
            { "FEEDBACK", "Feed" }, { "THRESHOLD", "Thresh" }, { "CHARACTER", "Char" }, { "BALANCE", "Pan" }, { "LOW MID", "Lo Mid" },
            { "HIGH MID", "Hi Mid" }, { "AMOUNT", "Amount" }, { "OUTPUT", "Out" }, { "INPUT", "In" },
        };
        for (const auto& q : quickControls (host, id))
        {
            const auto it = shortLabels.find (q.caption);
            const auto label = it != shortLabels.end() ? juce::String (it->second) : q.caption.substring (0, 1) + q.caption.substring (1).toLowerCase();
            if (host.apvts().getParameter (q.id) != nullptr && c.size() < 3)
                c.push_back ({ q.id, label });
        }
        return c;
    }

    /** A tile's mode line: the mode it plays (or the effect's kind when it has none). */
    juce::String tileModeText (osp::plugin::InstrumentHost& host, InstanceId id, const juce::String& subtitle)
    {
        juce::String modeId = id.type == Type::movement ? juce::String ("movement.mode") : quickModeId (id);
        if (id.type == Type::filter)
            modeId = juce::String ("analog.fx.") + fx::typeKey (id.type) + "." + juce::String (id.slot + 1) + ".type";
        if (modeId.isNotEmpty())
            if (auto* p = host.apvts().getParameter (modeId))
                return p->getCurrentValueAsText().toUpperCase();
        if (id.type == Type::eq)
            return "4 BANDS";   // (no mode: what it is)
        if (id.type == Type::utility)
            return "GAIN / WIDTH";
        return subtitle.upToFirstOccurrenceOf (juce::String::fromUTF8 (" \xc2\xb7"), false, false).toUpperCase();
    }

    /** The tile's regions at a scale (V5 §3.2): the motif's zone, the text's left. */
    struct TileGeometry
    {
        juce::Rectangle<float> pattern;
        float textX = 0.0f;
    };
    TileGeometry tileGeometry (juce::Rectangle<float> r, float b, bool stacked)
    {
        TileGeometry t;
        t.pattern = r.withWidth (std::round ((stacked ? 0.22f : 0.25f) * r.getWidth()));
        t.textX = t.pattern.getRight() + 8.0f * b;
        return t;
    }
}

//==============================================================================
/** + ADD EFFECT's library: categories, a search, each effect's state (IN CHAIN, + to add,
    COMING LATER for one this build cannot play yet, MAX INSTANCES when its pool is full). */
class FxRoutingView::Library final : public juce::Component
{
public:
    explicit Library (FxRoutingView& v) : view (v)
    {
        search.setTextToShowWhenEmpty ("Search effects", weak);
        search.setFont (font (11.0f));
        search.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xfffbf8f2));
        search.setColour (juce::TextEditor::outlineColourId, edge);
        search.setColour (juce::TextEditor::focusedOutlineColourId, sage);
        search.setColour (juce::TextEditor::textColourId, ink);
        search.onTextChange = [this] {
            scroll = 0;
            repaint();
        };
        search.onEscapeKey = [this] { view.showLibrary (false); };
        addAndMakeVisible (search);
        setWantsKeyboardFocus (true);
    }

    struct Row
    {
        const LibraryEntry* entry = nullptr;
        juce::Rectangle<int> bounds, action;
        juce::String state;   // "", IN CHAIN, COMING LATER, MAX INSTANCES
        bool header = false;
        juce::String category;
    };

    std::vector<Row> rows() const
    {
        std::vector<Row> out;
        const auto g = view.engine.fxRoute();   // (MAIN + SEND: both paths hold the pool)
        const auto query = search.getText().trim().toUpperCase();
        int y = 70 - scroll;
        juce::String category;
        for (const auto& e : libraryEntries)
        {
            const juce::String name = fx::typeName (e.type);
            if (query.isNotEmpty() && ! name.contains (query) && ! juce::String (e.description).toUpperCase().contains (query))
                continue;
            if (category != e.category)
            {
                category = e.category;
                Row h;
                h.header = true;
                h.category = category;
                h.bounds = { 12, y, getWidth() - 24, 20 };
                out.push_back (h);
                y += 20;
            }
            Row r;
            r.entry = &e;
            r.bounds = { 8, y, getWidth() - 16, 38 };
            r.action = r.bounds.withTrimmedLeft (r.bounds.getWidth() - 70).reduced (6, 8);
            int used = 0;
            for (const auto& id : fx::instancesOf (g))
                used += id.type == e.type ? 1 : 0;
            if (! fx::implemented (e.type))
                r.state = "COMING LATER";
            else if (fx::isLegacy (e.type) && used > 0)
                r.state = "IN CHAIN";
            else if (used >= fx::capacity[static_cast<std::size_t> (e.type)])
                r.state = "MAX INSTANCES";
            out.push_back (r);
            y += 38;
        }
        return out;
    }

    void resized() override { search.setBounds (12, 36, getWidth() - 24, 24); }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0x38291d23));
        g.fillRoundedRectangle (r.translated (0.0f, 3.0f), 8.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffbf8f2), 0.0f, 0.0f, juce::Colour (0xfff1eae0), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r.reduced (0.5f), 8.0f);
        g.setColour (juce::Colour (0xffd2c7b9));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (ink);
        g.setFont (font (11.0f, fonts::Weight::semibold, 0.075f));
        g.drawText ("ADD EFFECT", juce::Rectangle<int> (12, 8, 200, 20), juce::Justification::centredLeft, false);
        g.setColour (soft);
        g.drawText (juce::String::fromUTF8 ("\xc3\x97"), closeArea(), juce::Justification::centred, false);
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (getLocalBounds().withTrimmedTop (66).reduced (1, 4));
        for (const auto& row : rows())
        {
            if (row.header)
            {
                g.setColour (weak);
                g.setFont (font (8.5f, fonts::Weight::semibold, 0.1f));
                g.drawText (row.category, row.bounds, juce::Justification::bottomLeft, false);
                continue;
            }
            const auto type = row.entry->type;
            const bool enabled = row.state.isEmpty();
            if (row.bounds.contains (hover) && enabled)
            {
                g.setColour (juce::Colour (0xffe3ebe3));
                g.fillRoundedRectangle (row.bounds.toFloat(), 5.0f);
            }
            {
                // V5: the device's own enamel and motif (its tile and rack in miniature).
                const auto& identity = visualIdentityFor (type);
                const auto swatch = juce::Rectangle<float> (static_cast<float> (row.bounds.getX() + 5), static_cast<float> (row.bounds.getY() + 6), 26.0f, 26.0f);
                juce::Graphics::ScopedSaveState thumb (g);
                if (! enabled)
                    g.beginTransparencyLayer (0.5f);
                g.setColour (identity.enamel);
                g.fillRoundedRectangle (swatch, 5.0f);
                {
                    juce::Graphics::ScopedSaveState clip (g);
                    juce::Path rounded;
                    rounded.addRoundedRectangle (swatch, 5.0f);
                    g.reduceClipRegion (rounded);
                    paintFxMotif (g, identity.motif, swatch.reduced (3.0f), identity, MotifScale::thumb);
                }
                g.setColour (identity.edge);
                g.drawRoundedRectangle (swatch.reduced (0.5f), 5.0f, 1.0f);
                if (! enabled)
                    g.endTransparencyLayer();
            }
            g.setColour (ink.withAlpha (enabled ? 1.0f : 0.5f));
            g.setFont (font (10.5f, fonts::Weight::semibold, 0.04f));
            g.drawText (fx::typeName (type), row.bounds.withTrimmedLeft (36).withHeight (20).translated (0, 3), juce::Justification::centredLeft, false);
            g.setColour (weak.withAlpha (enabled ? 1.0f : 0.6f));
            g.setFont (font (9.0f));
            g.drawText (row.entry->description, row.bounds.withTrimmedLeft (36).withTrimmedRight (72).withTrimmedTop (19).withHeight (14), juce::Justification::centredLeft, true);
            g.setFont (font (8.5f, fonts::Weight::semibold, 0.06f));
            if (enabled)
            {
                g.setColour (green);
                g.drawRoundedRectangle (row.action.toFloat().reduced (0.5f), 4.0f, 1.0f);
                g.drawText ("+ ADD", row.action, juce::Justification::centred, false);
            }
            else
            {
                g.setColour (weak);
                g.drawText (row.state, row.bounds.withTrimmedRight (10).withTrimmedTop (2), juce::Justification::centredRight, false);
            }
        }
    }

    juce::Rectangle<int> closeArea() const { return { getWidth() - 30, 6, 24, 24 }; }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        // The list scrolls under the search field (it never grows the popover past its frame).
        const auto all = rows();
        const int bottom = all.empty() ? 0 : all.back().bounds.getBottom() + scroll;
        const int most = std::max (0, bottom - getHeight() + 8);
        scroll = juce::jlimit (0, most, scroll - juce::roundToInt (60.0f * wheel.deltaY));
        repaint();
    }
    int scroll = 0;

    void mouseMove (const juce::MouseEvent& e) override
    {
        hover = e.getPosition();
        repaint();
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        hover = { -1, -1 };
        repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (closeArea().contains (e.getPosition()))
        {
            view.showLibrary (false);
            return;
        }
        for (const auto& row : rows())
            if (! row.header && row.bounds.contains (e.getPosition()) && row.state.isEmpty())
            {
                view.insertNew (row.entry->type);
                return;
            }
    }

    juce::TextEditor search;

private:
    FxRoutingView& view;
    juce::Point<int> hover { -1, -1 };
};

//==============================================================================
/** One route edit for the host's undo (the route before and after). */
class FxRoutingView::RouteEdit final : public juce::UndoableAction
{
public:
    RouteEdit (AnalogEngine& e, fx::Graph a, fx::Graph b) : engine (e), before (std::move (a)), after (std::move (b)) {}
    bool perform() override { return engine.setFxRoute (after); }
    bool undo() override { return engine.setFxRoute (before); }

private:
    AnalogEngine& engine;
    fx::Graph before, after;
};

//==============================================================================
FxRoutingView::FxRoutingView (osp::plugin::InstrumentHost& h) : host (h), engine (engineOf (h))
{
    setTitle ("FX Routing");
    setWantsKeyboardFocus (true);
    gear = std::make_unique<IconButton> (IconButton::Kind::gear, "Routing settings");
    gear->setTooltip ("Routing settings");
    gear->onClick = [this] {
        gear->setToggleState (true, juce::dontSendNotification);
        settingsMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (gear.get()), [safe = juce::Component::SafePointer<FxRoutingView> (this)] (int) {
            if (safe != nullptr)
                safe->gear->setToggleState (false, juce::dontSendNotification);
        });
    };
    close = std::make_unique<IconButton> (IconButton::Kind::collapse, "Close routing");
    close->setTooltip ("Back to the sound sources");
    close->onClick = [this] {
        if (auto* editor = findParentComponentOfClass<andor::shell::ShellEditor>())
            editor->setSourceViewShown (false);
    };
    add = std::make_unique<IconButton> (IconButton::Kind::add, "Add effect");
    add->setTooltip ("The effects library: add an effect to the chain");
    add->onClick = [this] { showLibrary (library == nullptr || ! library->isVisible()); };
    for (auto* b : { gear.get(), close.get(), add.get() })
        addAndMakeVisible (*b);

    // The engine mixer: each card's own level (the same parameters as the cards' LEVEL).
    const juce::String levelIds[3] { params::osc1Level, params::osc2Level, "layerB.level" };
    for (int i = 0; i < 3; ++i)
    {
        auto& s = levels[static_cast<std::size_t> (i)];
        // (V4: a small ceramic level knob, the instrument's own knob, on the same parameter as
        // the card's LEVEL: dragging it is the card's level, nothing else.)
        s = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
        s->getProperties().set ("mini", true);
        s->getProperties().set ("arc", static_cast<juce::int64> (mixerTileColour (i).getARGB()));
        s->setRotaryParameters (osp::plugin::OspLookAndFeel::rotaryStart, osp::plugin::OspLookAndFeel::rotaryEnd, true);
        s->onValueChange = [this] { repaint (mixerArea); };
        addAndMakeVisible (*s);
    }
    juce::ignoreUnused (levelIds);
    bindMixer();

    engine.onFxRouteChanged = [safe = juce::Component::SafePointer<FxRoutingView> (this)] {
        if (safe != nullptr)
        {
            safe->layoutChain();
            safe->repaint();
        }
    };
    startTimerHz (8);
}

FxRoutingView::~FxRoutingView()
{
    engine.onFxRouteChanged = nullptr;
}

fx::Graph FxRoutingView::route() const
{
    // MAIN + SEND: the board is the tab's chain, as a route of its own (OVERVIEW: none).
    auto g = engine.fxRoute();
    if (! g.mainSend)
        return g;
    fx::Graph view;
    view.custom = true;
    if (currentTab == 0)
        view.chain = g.chain;
    else if (currentTab == 1)
        view.chain = g.send;
    return view;
}

bool FxRoutingView::apply (const fx::Graph& next, const juce::String& what)
{
    const auto now = engine.fxRoute();
    // An edit of a MAIN + SEND tab is that chain's (Restore original: the whole route).
    fx::Graph target = next;
    if (now.mainSend && next.custom && ! next.mainSend)
    {
        if (currentTab == 2)
            return false;   // (OVERVIEW edits nothing)
        target = now;
        (currentTab == 1 ? target.send : target.chain) = next.chain;
    }
    if (target == now || ! fx::validate (target).empty())
        return false;
    auto& undo = host.undo();
    undo.beginNewTransaction (what);
    const bool done = undo.perform (new RouteEdit (engine, now, target), what);
    if (! engine.fxRoute().mainSend && currentTab != 0)
        setTab (0);
    layoutChain();
    repaint();
    return done;
}

void FxRoutingView::bindMixer()
{
    // MAIN (and one path): the cards' levels; SEND: their sends.
    const bool sends = currentTab == 1 && engine.fxRoute().mainSend;
    const juce::String ids[3] { sends ? params::sendA : params::osc1Level, sends ? params::sendB : params::osc2Level, sends ? params::sendC : juce::String ("layerB.level") };
    for (int i = 0; i < 3; ++i)
    {
        auto& s = levels[static_cast<std::size_t> (i)];
        levelAttachments[static_cast<std::size_t> (i)].reset();
        s->setTitle (juce::String ("Card ") + juce::String::charToString (static_cast<juce::juce_wchar> ('A' + i)) + (sends ? " send" : " level"));
        if (auto* p = host.apvts().getParameter (ids[i]))
        {
            levelAttachments[static_cast<std::size_t> (i)] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (host.apvts(), ids[i], *s);
            s->setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
            s->setTooltip (sends ? juce::String ("How much of card ") + juce::String::charToString (static_cast<juce::juce_wchar> ('A' + i))
                                       + " reaches SEND (taken before the filter; card C: both its layers)"
                                 : p->getName (64));
        }
    }
    repaint (mixerArea);
}

void FxRoutingView::setTab (int index)
{
    index = std::clamp (index, 0, 2);
    if (! engine.fxRoute().mainSend)
        index = 0;
    if (index == currentTab)
        return;
    currentTab = index;
    showQuick (std::nullopt);
    showLibrary (false);
    select (std::nullopt);
    bindMixer();
    layoutChain();
    repaint();
}

bool FxRoutingView::useMainSend()
{
    const auto now = engine.fxRoute();
    if (now.mainSend)
        return false;
    const auto next = fx::mainSendGraph (now);
    if (! next.mainSend || ! apply (next, "Use MAIN + SEND"))
        return false;
    // Conservative sends: card C at 25 % when nothing is sent yet (the dry is untouched).
    if (host.parameterValue (params::sendA) <= 0.0f && host.parameterValue (params::sendB) <= 0.0f && host.parameterValue (params::sendC) <= 0.0f)
        if (auto* p = host.apvts().getParameter (params::sendC))
            p->setValueNotifyingHost (p->convertTo0to1 (25.0f));
    setTab (0);
    return true;
}

bool FxRoutingView::useSinglePath()
{
    const auto now = engine.fxRoute();
    if (! now.mainSend)
        return false;
    const auto next = fx::singlePathGraph (now);
    if (! fx::validate (next).empty())
        return false;
    auto& undo = host.undo();
    undo.beginNewTransaction ("Back to one path");
    const bool done = undo.perform (new RouteEdit (engine, now, next), "Back to one path");
    setTab (0);
    layoutChain();
    repaint();
    return done;
}

std::vector<InstanceId> FxRoutingView::order() const
{
    // The top chain's effects left to right (a group: its first lane's first effect stands for it).
    std::vector<InstanceId> out;
    for (const auto& n : route().chain)
    {
        if (n.kind == fx::Node::Kind::effect)
            out.push_back (n.fx);
        else
            for (const auto& lane : n.lanes)
                for (const auto& m : lane)
                    if (m.kind == fx::Node::Kind::effect)
                        out.push_back (m.fx);
    }
    return out;
}

bool FxRoutingView::moveTo (InstanceId id, const fx::ChainPath& path, int index)
{
    auto g = route();
    return fx::moveEffect (g, id, path, index) && apply (g, juce::String ("Move ") + fx::typeName (id.type));
}

bool FxRoutingView::stackOn (InstanceId id, InstanceId target)
{
    auto g = route();
    return fx::stackEffect (g, id, target) && apply (g, juce::String ("Stack ") + fx::typeName (id.type));
}

bool FxRoutingView::toggleBypass (InstanceId id)
{
    if (! fx::isLegacy (id.type))
    {
        // A new effect's bypass is its ON parameter (automatable).
        const auto param = juce::String ("analog.fx.") + fx::typeKey (id.type) + "." + juce::String (id.slot + 1) + ".on";
        if (auto* p = host.apvts().getParameter (param))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getValue() >= 0.5f ? 0.0f : 1.0f);
            p->endChangeGesture();
            repaint();
            return true;
        }
        return false;
    }
    auto g = route();
    fx::ChainPath path;
    int at = 0;
    if (! fx::locate (g, id, path, at))
        return false;
    const bool now = (*fx::chainAt (g, path))[static_cast<std::size_t> (at)].bypassed;
    return fx::setBypassed (g, id, ! now) && apply (g, juce::String (now ? "Enable " : "Bypass ") + fx::typeName (id.type));
}

bool FxRoutingView::restoreOriginal()
{
    // Topology only: every effect keeps its settings.
    return apply (fx::classicGraph(), "Restore original routing");
}

bool FxRoutingView::remove (InstanceId id)
{
    auto g = route();
    if (! fx::removeEffect (g, id) || ! apply (g, juce::String ("Remove ") + fx::typeName (id.type)))
        return false;
    if (selected == id)
        selected.reset();
    return true;
}

bool FxRoutingView::insertNew (Type type)
{
    // MAIN + SEND: a spatial effect goes on SEND (MAIN holds inserts only).
    if (engine.fxRoute().mainSend && (fx::isSpatial (type) ? currentTab != 1 : currentTab == 2))
        setTab (fx::isSpatial (type) ? 1 : 0);   // (OVERVIEW: an insert goes on MAIN)
    auto g = route();
    // A free slot of its pool (in the whole route: MAIN's and SEND's).
    const auto whole = engine.fxRoute();
    std::optional<InstanceId> id;
    for (int s = 0; s < fx::capacity[static_cast<std::size_t> (type)] && ! id; ++s)
        if (! fx::contains (whole, { type, s }))
            id = InstanceId { type, s };
    if (! id || ! fx::implemented (type))
        return false;
    // After the selected effect (in its chain), or at the end, before OUTPUT.
    fx::ChainPath path;
    int index = static_cast<int> (g.chain.size());
    if (selected && fx::locate (g, *selected, path, index))
        ++index;
    else
        path.clear(), index = static_cast<int> (g.chain.size());
    if (! fx::insertEffect (g, path, index, fx::Node::effect (*id)) || ! apply (g, juce::String ("Add ") + fx::typeName (type)))
        return false;
    selected = id;
    showLibrary (false);
    return true;
}

juce::PopupMenu FxRoutingView::settingsMenu()
{
    juce::PopupMenu m;
    const bool custom = route().custom;
    m.addItem ("Restore original", custom, false, [this] { restoreOriginal(); });
    // Cinematic V6 gate 4: MAIN + SEND, only by this explicit choice (never on load).
    if (engine.fxRoute().mainSend)
        m.addItem ("Back to one path", [this] { useSinglePath(); });
    else
        m.addItem (juce::String::fromUTF8 ("Use MAIN + SEND\xe2\x80\xa6"), [this] { useMainSend(); });
    m.addSeparator();
    // FX CHAIN presets: the route and its new effects' settings (never the instrument's).
    m.addItem ("Save FX chain...", [safe = juce::Component::SafePointer<FxRoutingView> (this)] {
        if (safe == nullptr)
            return;
        auto* w = new juce::AlertWindow ("Save FX chain", "A name for this chain:", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", "My chain");
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result) {
            if (result == 1 && safe != nullptr)
                if (const auto name = juce::File::createLegalFileName (w->getTextEditorContents ("name").trim()); name.isNotEmpty())
                    safe->saveChain (name);
        }), true);
    });
    juce::PopupMenu load;
    for (const auto& file : fxpresets::chainFolder().findChildFiles (juce::File::findFiles, false, "*.xml"))
        load.addItem (file.getFileNameWithoutExtension(), [this, file] { loadChain (file); });
    m.addSubMenu ("Load FX chain", load, load.getNumItems() > 0);
    m.addItem ("Show chains folder", [] {
        auto folder = fxpresets::chainFolder();
        folder.createDirectory();
        folder.revealToUser();
    });
    return m;
}

bool FxRoutingView::saveChain (const juce::String& name)
{
    auto folder = fxpresets::chainFolder();
    folder.createDirectory();
    const auto xml = fxpresets::chainToXml (host, route(), name);
    return xml != nullptr && xml->writeTo (folder.getChildFile (name + ".xml"));
}

bool FxRoutingView::loadChain (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);
    fx::Graph next;
    if (xml == nullptr || ! fx::fromText (xml->getStringAttribute ("graph").toStdString(), next) || ! fx::validate (next).empty())
        return false;   // (an unreadable chain changes nothing: checked before any setting moves)
    if (! fxpresets::chainFromXml (host, *xml, next))
        return false;
    next.custom = true;
    return apply (next, "Load FX chain " + file.getFileNameWithoutExtension());
}

juce::Component& FxRoutingView::settingsButton() noexcept { return *gear; }
juce::Component& FxRoutingView::addEffectButton() noexcept { return *add; }
juce::Component* FxRoutingView::libraryPopover() const noexcept { return library.get(); }

void FxRoutingView::showLibrary (bool shown)
{
    if (shown)
        showQuick (std::nullopt);   // (one popover at a time)
    if (shown && library == nullptr)
    {
        library = std::make_unique<Library> (*this);
        addChildComponent (*library);
    }
    if (library == nullptr)
        return;
    if (shown)
    {
        // Anchored under + ADD EFFECT, over the chain (no layout shift).
        const int w = 330, h = std::min (360, getHeight() - add->getBottom() - 10);
        library->setBounds (juce::Rectangle<int> (w, h).withPosition (std::min (add->getX(), getWidth() - w - 12), add->getBottom() + 6));
        library->toFront (true);
        library->search.grabKeyboardFocus();
    }
    library->setVisible (shown);
    add->setToggleState (shown, juce::dontSendNotification);
}

void FxRoutingView::openEditor (InstanceId id)
{
    if (auto* editor = findParentComponentOfClass<andor::shell::ShellEditor>())
        openFxRack (*editor, id);
}

void FxRoutingView::showQuick (std::optional<InstanceId> id)
{
    if (quick != nullptr && (! id || id != quickId))
    {
        removeChildComponent (quick.get());
        quick.reset();
        quickId.reset();
    }
    if (! id || quick != nullptr)
        return;
    const bool onBoard = std::any_of (placed.begin(), placed.end(), [&id] (const Tile& t) { return t.id == *id; })
                         || std::any_of (summaryList.begin(), summaryList.end(), [&id] (const Summary& sum) {
                                return std::find (sum.members.begin(), sum.members.end(), *id) != sum.members.end();
                            });
    if (! onBoard)
        return;
    juce::Rectangle<int> size;
    const auto target = *id;
    quick = createFxQuickPopover (
        host, target,
        [safe = juce::Component::SafePointer<FxRoutingView> (this), target] {
            if (safe != nullptr)
            {
                safe->showQuick (std::nullopt);
                safe->openEditor (target);
            }
        },
        [safe = juce::Component::SafePointer<FxRoutingView> (this)] {
            if (safe != nullptr)
            {
                safe->showQuick (std::nullopt);
                safe->grabKeyboardFocus();
            }
        },
        size);
    quickId = target;
    quick->setSize (size.getWidth(), size.getHeight());
    placeQuick();
    addAndMakeVisible (*quick);
    quick->toFront (false);
}

void FxRoutingView::placeQuick()
{
    if (quick == nullptr || ! quickId)
        return;
    // Its tile - or, for an effect shown inside a group summary, that summary.
    juce::Rectangle<int> tile;
    if (const auto it = std::find_if (placed.begin(), placed.end(), [this] (const Tile& t) { return t.id == *quickId; }); it != placed.end())
        tile = it->bounds;
    else
        for (const auto& sum : summaryList)
            if (std::find (sum.members.begin(), sum.members.end(), *quickId) != sum.members.end())
                tile = sum.bounds;
    if (tile.isEmpty())
    {
        showQuick (std::nullopt);   // (its effect left the route)
        return;
    }
    // Anchored under the tile; above it when there is no room; beside it when neither fits
    // (never over the header's controls). Clamped to the view's edges.
    const auto room = getLocalBounds().withTrimmedTop (headerArea.getBottom() + 2).reduced (4);
    const int w = quick->getWidth(), h = quick->getHeight();
    auto at = juce::Rectangle<int> (tile.getCentreX() - w / 2, tile.getBottom() + 6, w, h);
    if (at.getBottom() > room.getBottom())
        at.setY (tile.getY() - 6 - h);
    if (at.getY() < room.getY())
    {
        const bool right = tile.getRight() + 8 + w <= room.getRight();
        at.setPosition (right ? tile.getRight() + 8 : tile.getX() - 8 - w, tile.getCentreY() - h / 2);
    }
    quick->setBounds (at.constrainedWithin (room));
}

void FxRoutingView::select (std::optional<InstanceId> id)
{
    selected = id;
    repaint();
}

juce::String FxRoutingView::statusText() const
{
    return route().custom ? juce::String::fromUTF8 ("CUSTOM \xc2\xb7 Your signal path") : juce::String::fromUTF8 ("CLASSIC \xc2\xb7 Original signal path");
}

juce::String FxRoutingView::summaryText() const
{
    std::function<juce::String (const std::vector<fx::Node>&)> chainText = [&chainText] (const std::vector<fx::Node>& chain) {
        juce::StringArray parts;
        for (const auto& n : chain)
        {
            if (n.kind == fx::Node::Kind::effect)
                parts.add (juce::String (fx::typeName (n.fx.type)) + (n.bypassed ? " (off)" : ""));
            else
            {
                juce::StringArray lanes;
                for (const auto& lane : n.lanes)
                    lanes.add (lane.empty() ? juce::String ("dry") : chainText (lane));
                parts.add ("(" + lanes.joinIntoString (juce::String::fromUTF8 (" \xe2\x88\xa5 ")) + ")");
            }
        }
        return parts.joinIntoString (juce::String::fromUTF8 (" \xe2\x86\x92 "));
    };
    const auto g = route();
    return g.chain.empty() ? juce::String ("No effects: the voices straight out") : chainText (g.chain);
}

juce::String FxRoutingView::subtitleOf (InstanceId id) const
{
    auto& h = host;
    switch (id.type)
    {
        case Type::drive: return textOf (h, "drive.mode") + juce::String::fromUTF8 (" \xc2\xb7 ") + percentOf (h, "drive");
        case Type::movement:
        {
            auto s = textOf (h, "movement.mode");
            if (h.parameterValue (params::movementLaneId (1, "on")) >= 0.5f)
                s << juce::String::fromUTF8 (" \xc2\xb7 ") << textOf (h, params::movementBPrefix + "mode");
            return s + juce::String::fromUTF8 (" \xc2\xb7 ") + percentOf (h, "motion");
        }
        case Type::echo: return textOf (h, "echo.type") + juce::String::fromUTF8 (" \xc2\xb7 ") + percentOf (h, "echo");
        case Type::space: return textOf (h, "space.engine") + juce::String::fromUTF8 (" \xc2\xb7 ") + percentOf (h, "space");
        default: return {};
    }
}

juce::String FxRoutingView::signature() const
{
    juce::String s = fx::toText (route());
    for (const auto& t : placed)
        s << "|" << subtitleOf (t.id);
    for (const char* id : { "drive", "motion", "echo", "space" })
        s << "|" << host.parameterValue (id);
    return s;
}

void FxRoutingView::timerCallback()
{
    // (A session or preset left MAIN + SEND: back to its one path.)
    if (currentTab != 0 && ! engine.fxRoute().mainSend)
        setTab (0);
    // (The window's scale changes the board's tile size: a pure re-projection.)
    if (std::abs (juce::Component::getApproximateScaleFactorForComponent (this) - layoutScale) > 0.01f)
    {
        layoutChain();
        placeQuick();
        repaint();
    }
    if (const auto now = signature(); now != shown)
    {
        shown = now;
        placeQuick();   // (follows its tile; closes when the effect is removed)
        repaint();
    }
}

//==============================================================================
void FxRoutingView::resized()
{
    auto inner = getLocalBounds().reduced (18, 13);
    headerArea = inner.removeFromTop (34);
    auto header = headerArea;
    // The X as the racks' and the ARPEGGIATOR's: a 30 px target centred 25 px from the right edge.
    close->setBounds (juce::Rectangle<int> (30, 30).withCentre ({ getWidth() - 25, headerArea.getCentreY() }));
    header.setRight (close->getX() - 6);
    gear->setBounds (header.removeFromRight (26).withSizeKeepingCentre (26, 26));
    header.removeFromRight (10);
    const int statusWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (11.5f, fonts::Weight::semibold), statusText())) + 14;
    header.removeFromRight (statusWidth);
    header.removeFromRight (16);
    add->setBounds (header.removeFromRight (116).withSizeKeepingCentre (116, 28));
    {
        // MAIN | SEND | OVERVIEW: a compact segmented key after ROUTING (MAIN + SEND only).
        const int x0 = headerArea.getX() + 120, w = 74, h = 24;
        for (int i = 0; i < 3; ++i)
            tabAreas[static_cast<std::size_t> (i)] = juce::Rectangle<int> (x0 + i * w, headerArea.getCentreY() - h / 2, w, h);
    }
    inner.removeFromTop (14);
    // The mixer: about a fifth (V4: 21 %, knobs in place of the sliders), never so narrow that
    // a card's name and its knob collide.
    mixerArea = inner.removeFromLeft (std::max (170, juce::roundToInt (0.21f * static_cast<float> (inner.getWidth()))));
    zoneArea = inner.withTrimmedLeft (30);
    zoneArea.removeFromBottom (12);   // (no footer: the tiles' menus and keys move and stack)
    if (library != nullptr && library->isVisible())
        showLibrary (true);
    layoutChain();
}

juce::Rectangle<int> FxRoutingView::chainArea() const
{
    return zoneArea.withTrimmedTop (14).withTrimmedBottom (6);
}

namespace
{
    // Tile and wire metrics at a board scale (1 at the canonical width): tiles 150 x 72, 66 in
    // a parallel lane; 18 px wire gaps; two rows 26 px apart.
    void scaleMetrics (int& tileW, int& tileH, int& stackedH, int& gapW, int& stackGap, int& branch, int& rowGap, float b)
    {
        // V5: the miniature hardware tiles 192 x 84 (a parallel lane's 60: name and mode only).
        tileW = juce::roundToInt (192.0f * b);
        tileH = juce::roundToInt (84.0f * b);
        stackedH = juce::roundToInt (60.0f * b);
        gapW = juce::roundToInt (22.0f * b);
        stackGap = juce::roundToInt (8.0f * b);
        branch = juce::roundToInt (18.0f * b);
        rowGap = juce::roundToInt (28.0f * b);
    }
    constexpr int outWBase = 78, outHBase = 28, contWBase = 66, contHBase = 22, lead = 26;
}

void FxRoutingView::layoutChain()
{
    projectRoute();
    placeTileKnobs();
}

void FxRoutingView::projectRoute()
{
    // The board is a pure projection of the route (fx::Graph): it never edits it. Readability
    // first: the largest tiles (at a small window, larger in design units) that fit in one row,
    // else in two rows (the serial chain wrapped left to right), at the same scale; only when
    // nothing fits at the canonical size are parallel stages, then the chain's tail, shown as
    // labelled group summaries (never hidden, never reordered).
    const auto g = route();
    const int n = static_cast<int> (g.chain.size());
    const float k = juce::Component::getApproximateScaleFactorForComponent (this);
    layoutScale = k;
    const float bMax = juce::jlimit (1.0f, 1.3f, 0.78f / std::max (0.1f, k));

    // The mixer at the same legibility: its rows, names and knobs scale with the board.
    mixerScale = bMax;
    {
        const int knob = juce::roundToInt ((mixerArea.getWidth() >= 210 ? 42.0f : 38.0f) * mixerScale);
        for (int i = 0; i < static_cast<int> (levels.size()); ++i)
        {
            const auto row = mixerRow (i);
            // The knob at the row's right, its value under it (painted).
            levels[static_cast<std::size_t> (i)]->setBounds (juce::Rectangle<int> (knob, knob).withCentre ({ row.getRight() - juce::roundToInt (26.0f * mixerScale), row.getCentreY() - juce::roundToInt (7.0f * mixerScale) }));
        }
    }

    std::vector<int> groupOrder;   // (the biggest stages give way first)
    for (int i = 0; i < n; ++i)
        if (g.chain[static_cast<std::size_t> (i)].kind != fx::Node::Kind::effect)
            groupOrder.push_back (i);
    std::stable_sort (groupOrder.begin(), groupOrder.end(), [&g] (int a, int b) {
        auto count = [] (const fx::Node& node) {
            std::function<int (const fx::Node&)> c = [&c] (const fx::Node& x) -> int {
                if (x.kind == fx::Node::Kind::effect)
                    return 1;
                int t = 0;
                for (const auto& lane : x.lanes)
                    for (const auto& y : lane)
                        t += c (y);
                return t;
            };
            return c (node);
        };
        return count (g.chain[static_cast<std::size_t> (a)]) > count (g.chain[static_cast<std::size_t> (b)]);
    });

    // (Never below the legible minimum for this window: 1 at the canonical width and above;
    // larger in design units at a small window - at 869, four tiles a row at most.)
    const float bMin = juce::jlimit (1.0f, 1.3f, 0.7f / std::max (0.1f, k));
    std::vector<bool> collapsed (static_cast<std::size_t> (n), false);
    for (float b = bMax; b >= bMin - 0.001f; b -= 0.05f)
        if (layoutAttempt (g, std::max (b, bMin), collapsed, n))
            return;
    for (const int gi : groupOrder)
    {
        collapsed[static_cast<std::size_t> (gi)] = true;
        if (layoutAttempt (g, bMin, collapsed, n))
            return;
    }
    for (int tail = n - 2; tail >= 0; --tail)
        if (layoutAttempt (g, bMin, collapsed, tail))
            return;
    layoutAttempt (g, bMin, collapsed, 0);   // (one summary for the whole chain: it always fits)
}

bool FxRoutingView::layoutAttempt (const fx::Graph& g, float scale, const std::vector<bool>& collapsed, int tailFrom)
{
    placed.clear();
    gapList.clear();
    groups.clear();
    summaryList.clear();
    rows.clear();
    contOut = contIn = {};
    Metrics m;
    m.b = scale;
    scaleMetrics (m.tileW, m.tileH, m.stackedH, m.gapW, m.stackGap, m.branch, m.rowGap, scale);
    metrics = m;

    const auto area = chainArea();
    const int n = static_cast<int> (g.chain.size());
    tailFrom = juce::jlimit (0, n, tailFrom);
    const int outW = juce::roundToInt (outWBase * scale), outH = juce::roundToInt (outHBase * scale);
    const int contW = juce::roundToInt (contWBase * scale), contH = juce::roundToInt (contHBase * scale);

    // Sizes: a tile; a stage: its lanes stacked (each at least a lane tile high), a branch each side.
    std::function<juce::Point<int> (const std::vector<fx::Node>&, bool)> chainSize;
    std::function<juce::Point<int> (const fx::Node&, bool)> nodeSize = [&] (const fx::Node& node, bool stacked) -> juce::Point<int> {
        if (node.kind == fx::Node::Kind::effect)
            return { m.tileW, stacked ? m.stackedH : m.tileH };
        int w = 0, h = 0;
        for (const auto& lane : node.lanes)
        {
            const auto sz = chainSize (lane, true);
            w = std::max (w, sz.x);
            h += std::max (sz.y, m.stackedH) + m.stackGap;
        }
        return { std::max (w, m.tileW) + 2 * m.branch, h - m.stackGap };
    };
    chainSize = [&] (const std::vector<fx::Node>& chain, bool stacked) -> juce::Point<int> {
        int w = 0, h = 0;
        for (std::size_t i = 0; i < chain.size(); ++i)
        {
            const auto sz = nodeSize (chain[i], stacked);
            w += sz.x + (i > 0 ? m.gapW : 0);
            h = std::max (h, sz.y);
        }
        return { w, h };
    };

    // The top chain's items: its nodes, a summary for a collapsed stage, one for the tail.
    struct Item
    {
        int first = 0, last = 0;   ///< the top chain's [first, last) it stands for
        bool summary = false;
        juce::Point<int> size;
    };
    std::vector<Item> items;
    for (int i = 0; i < tailFrom; ++i)
    {
        const bool sum = collapsed[static_cast<std::size_t> (i)];
        items.push_back ({ i, i + 1, sum, sum ? juce::Point<int> (m.tileW, m.tileH) : nodeSize (g.chain[static_cast<std::size_t> (i)], false) });
    }
    if (tailFrom < n)
        items.push_back ({ tailFrom, n, true, { m.tileW, m.tileH } });

    auto widthOf = [&items, &m] (int from, int to) {
        int w = 0;
        for (int i = from; i < to; ++i)
            w += items[static_cast<std::size_t> (i)].size.x + (i > from ? m.gapW : 0);
        return w;
    };
    auto heightOf = [&items, &m] (int from, int to) {
        int h = m.tileH;
        for (int i = from; i < to; ++i)
            h = std::max (h, items[static_cast<std::size_t> (i)].size.y);
        return h;
    };

    const int count = static_cast<int> (items.size());
    const int left = area.getX() + lead, outLeft = area.getRight() - outW;
    int split = count;   // (items from here on go to row 2)
    if (widthOf (0, count) > outLeft - 22 - left || heightOf (0, count) > area.getHeight())
    {
        // Two rows: the most even split that fits.
        split = -1;
        int best = std::numeric_limits<int>::max();
        // (both rows on one column grid: from the later of their starts)
        const int base = std::max (left, area.getX() + contW + 14);
        const int row1Avail = area.getRight() - contW - 14 - base, row2Avail = outLeft - 22 - base;
        for (int s = 1; s < count; ++s)
        {
            const int w1 = widthOf (0, s), w2 = widthOf (s, count);
            if (w1 > row1Avail || w2 > row2Avail || heightOf (0, s) + m.rowGap + heightOf (s, count) > area.getHeight())
                continue;
            if (const int worst = std::max (w1, w2); worst < best)
            {
                best = worst;
                split = s;
            }
        }
        if (split < 0)
            return false;
    }

    // Rows: centred in the zone (one row also centred across it).
    const bool two = split < count;
    const int h1 = heightOf (0, split), h2 = two ? heightOf (split, count) : 0;
    const int total = h1 + (two ? m.rowGap + h2 : 0);
    const int top = area.getY() + (area.getHeight() - total) / 2;
    Row r1 { top + h1 / 2, mixerArea.getRight() + 1, 0 };
    Row r2 { top + h1 + m.rowGap + h2 / 2, 0, 0 };
    outputBox = juce::Rectangle<int> (outW, outH).withPosition (outLeft, (two ? r2.centreY : r1.centreY) - outH / 2);
    if (two)
    {
        contOut = juce::Rectangle<int> (contW, contH).withPosition (area.getRight() - contW, r1.centreY - contH / 2);
        contIn = juce::Rectangle<int> (contW, contH).withPosition (area.getX(), r2.centreY - contH / 2);
        r1.right = contOut.getX();
        r2.left = contIn.getRight();
        r2.right = outputBox.getX();
        rows = { r1, r2 };
    }
    else
    {
        r1.right = outputBox.getX();
        rows = { r1 };
    }
    wireFrom = { mixerArea.getRight() + 4, r1.centreY - 1, 2, 2 };

    // Placing: tiles, stages (lanes recursively, with their own gaps), summaries; the gaps
    // (drop targets) where the wire shows them.
    auto tileFor = [this] (fx::InstanceId id, bool bypassedLegacy, juce::Rectangle<int> bounds, bool stacked) {
        Tile t;
        t.id = id;
        t.bounds = bounds;
        t.light = juce::Rectangle<int> (24, 24).withCentre ({ bounds.getRight() - 14, bounds.getY() + 14 });
        t.bypassed = fx::isLegacy (id.type) ? bypassedLegacy : ! fx::isOn (fx::Settings {}, id);
        if (! fx::isLegacy (id.type))
        {
            const auto param = juce::String ("analog.fx.") + fx::typeKey (id.type) + "." + juce::String (id.slot + 1) + ".on";
            t.bypassed = host.apvts().getParameter (param) != nullptr && host.parameterValue (param) < 0.5f;
        }
        t.stacked = stacked;
        placed.push_back (t);
    };
    std::function<void (const std::vector<fx::Node>&, const fx::ChainPath&, int, int)> placeLane;
    auto placeNode = [&] (const fx::Node& node, const fx::ChainPath& path, int index, int x, int cy, bool stacked) {
        const auto sz = nodeSize (node, stacked);
        if (node.kind == fx::Node::Kind::effect)
        {
            tileFor (node.fx, node.bypassed, { x, cy - sz.y / 2, sz.x, sz.y }, stacked);
            return;
        }
        Group group;
        group.bounds = { x, cy - sz.y / 2, sz.x, sz.y };
        group.split = node.kind == fx::Node::Kind::split;
        int y = group.bounds.getY();
        for (int lane = 0; lane < static_cast<int> (node.lanes.size()); ++lane)
        {
            const auto ls = chainSize (node.lanes[static_cast<std::size_t> (lane)], true);
            const int h = std::max (ls.y, m.stackedH);
            const int laneCy = y + h / 2;
            group.laneCentres.push_back (laneCy);
            auto lanePath = path;
            lanePath.push_back ({ index, lane });
            placeLane (node.lanes[static_cast<std::size_t> (lane)], lanePath, x + m.branch + (sz.x - 2 * m.branch - ls.x) / 2, laneCy);
            y += h + m.stackGap;
        }
        groups.push_back (group);
    };
    placeLane = [&] (const std::vector<fx::Node>& chain, const fx::ChainPath& path, int x, int cy) {
        const int gapH = m.stackedH;
        gapList.push_back ({ path, 0, juce::Rectangle<int> (x - m.gapW + 2, cy - gapH / 2, m.gapW - 4, gapH) });
        for (int i = 0; i < static_cast<int> (chain.size()); ++i)
        {
            const auto sz = nodeSize (chain[static_cast<std::size_t> (i)], true);
            placeNode (chain[static_cast<std::size_t> (i)], path, i, x, cy, true);
            x += sz.x;
            gapList.push_back ({ path, i + 1, juce::Rectangle<int> (x + 2, cy - gapH / 2, m.gapW - 4, gapH) });
            x += m.gapW;
        }
    };

    auto collectIds = [] (const fx::Node& node, std::vector<fx::InstanceId>& out) {
        std::function<void (const fx::Node&)> walk = [&] (const fx::Node& x) {
            if (x.kind == fx::Node::Kind::effect)
                out.push_back (x.fx);
            else
                for (const auto& lane : x.lanes)
                    for (const auto& y : lane)
                        walk (y);
        };
        walk (node);
    };

    // The spread (extra space in each top-level gap) and, for two rows, the block's left.
    int spread = 0, blockLeft = left;
    {
        const int base = std::max (left, contIn.getRight() + 14);
        const int availRow = two ? std::min (area.getRight() - contW - 14, outLeft - 22) - base : outLeft - 22 - left;
        const int widest = two ? std::max (widthOf (0, split), widthOf (split, count)) : widthOf (0, count);
        const int most = two ? std::max (split, count - split) : count;
        if (most > 1)
            spread = juce::jlimit (0, juce::roundToInt (26.0f * scale), (availRow - widest) / (most - 1));
        if (two)
        {
            const int blockW = widest + spread * (most - 1);
            blockLeft = base + std::max (0, (availRow - blockW) / 2);
        }
    }
    for (int row = 0; row < (two ? 2 : 1); ++row)
    {
        const int from = row == 0 ? 0 : split, to = row == 0 ? split : count;
        const auto& rr = rows[static_cast<std::size_t> (row)];
        const int rowH = row == 0 ? h1 : h2;
        const int rowLeft = row == 0 ? left : contIn.getRight() + 14;
        const int rowRight = (two && row == 0) ? contOut.getX() - 14 : outLeft - 22;
        // The rows share one column grid, centred, the top-level gaps opened a little where
        // there is room (V5: a balanced board, not tiles stranded at one end).
        int x = two ? blockLeft : rowLeft + (rowRight - rowLeft - (widthOf (from, to) + spread * std::max (0, to - from - 1))) / 2;
        const int gapTop = rr.centreY - rowH / 2 - 4, gapH = rowH + 8;
        int prevRight = row == 0 ? area.getX() : contIn.getRight() + 2;
        for (int i = from; i < to; ++i)
        {
            const auto& it = items[static_cast<std::size_t> (i)];
            gapList.push_back ({ {}, it.first, juce::Rectangle<int> (prevRight + 2, gapTop, std::max (6, x - prevRight - 4), gapH) });
            if (it.summary)
            {
                Summary sum;
                sum.bounds = juce::Rectangle<int> (it.size.x, it.size.y).withPosition (x, rr.centreY - it.size.y / 2);
                for (int j = it.first; j < it.last; ++j)
                    collectIds (g.chain[static_cast<std::size_t> (j)], sum.members);
                sum.group = it.last - it.first == 1 && g.chain[static_cast<std::size_t> (it.first)].kind != fx::Node::Kind::effect;
                sum.lanes = sum.group ? static_cast<int> (g.chain[static_cast<std::size_t> (it.first)].lanes.size()) : 0;
                summaryList.push_back (sum);
            }
            else
                placeNode (g.chain[static_cast<std::size_t> (it.first)], {}, it.first, x, rr.centreY, false);
            x += it.size.x;
            prevRight = x;
            x += m.gapW + spread;
        }
        // The row's last gap: to CONT. 2 (row 1 of two) or to OUTPUT.
        const int end = (two && row == 0) ? contOut.getX() - 2 : outputBox.getX() - 2;
        const int index = to > from ? items[static_cast<std::size_t> (to - 1)].last : (row == 0 ? 0 : n);
        if (to > from || row == 0)
            gapList.push_back ({ {}, index, juce::Rectangle<int> (prevRight + 2, gapTop, std::max (6, end - prevRight - 4), gapH) });
    }
    return true;
}

void FxRoutingView::placeTileKnobs()
{
    // Each full-size tile's knobs (a parallel lane's compact tile has none: its click opens the
    // quick settings). Kept across layouts when the tile and parameter are the same, so a
    // knob is never rebuilt under the hand.
    std::vector<std::unique_ptr<TileKnob>> next;
    const float b = metrics.b;
    for (const auto& t : placed)
    {
        if (t.stacked)
            continue;
        const auto controls = tileControlsFor (host, t.id);
        if (controls.empty())
            continue;
        const auto r = t.bounds.toFloat();
        const auto geo = tileGeometry (r, b, false);
        const float right = r.getRight() - 6.0f * b;
        const float cw = (right - geo.textX + 6.0f * b) / static_cast<float> (controls.size());
        const int side = juce::roundToInt (30.0f * b);
        for (std::size_t i = 0; i < controls.size(); ++i)
        {
            const auto& c = controls[i];
            std::unique_ptr<TileKnob> k;
            for (auto& old : tileKnobs)
                if (old != nullptr && old->owner == t.id && old->parameterId == c.id)
                {
                    k = std::move (old);
                    break;
                }
            if (k == nullptr)
            {
                k = std::make_unique<TileKnob> (*this, host, t.id, c.id, c.label, visualIdentityFor (t.id.type).motifInk);
                addAndMakeVisible (*k);
            }
            k->label = c.label;
            const float cx = geo.textX - 6.0f * b + cw * (static_cast<float> (i) + 0.5f);
            k->setBounds (juce::Rectangle<int> (side, side).withCentre ({ juce::roundToInt (cx), juce::roundToInt (r.getY() + 55.0f * b) }));
            k->setAlpha (t.bypassed ? 0.55f : 1.0f);
            next.push_back (std::move (k));
        }
    }
    for (auto& old : tileKnobs)
        if (old != nullptr)
            removeChildComponent (old.get());
    tileKnobs = std::move (next);
    // (the popovers stay over the board)
    if (library != nullptr && library->isVisible())
        library->toFront (false);
    if (quick != nullptr)
        quick->toFront (false);
}

juce::StringArray FxRoutingView::tileKnobParameters (InstanceId id) const
{
    juce::StringArray ids;
    for (const auto& k : tileKnobs)
        if (k->owner == id)
            ids.add (k->parameterId);
    return ids;
}

juce::Slider* FxRoutingView::tileKnob (InstanceId id, const juce::String& parameterId) const
{
    for (const auto& k : tileKnobs)
        if (k->owner == id && k->parameterId == parameterId)
            return k.get();
    return nullptr;
}

int FxRoutingView::tileAt (juce::Point<int> p) const
{
    if (p.x <= mixerArea.getRight())
        return -1;
    for (int i = 0; i < static_cast<int> (placed.size()); ++i)
        if (placed[static_cast<std::size_t> (i)].bounds.contains (p))
            return i;
    return -1;
}

int FxRoutingView::summaryAt (juce::Point<int> p) const
{
    for (int i = 0; i < static_cast<int> (summaryList.size()); ++i)
        if (summaryList[static_cast<std::size_t> (i)].bounds.contains (p))
            return i;
    return -1;
}

int FxRoutingView::gapAt (juce::Point<int> p) const
{
    for (int i = 0; i < static_cast<int> (gapList.size()); ++i)
        if (gapList[static_cast<std::size_t> (i)].bounds.contains (p))
            return i;
    return -1;
}

//==============================================================================
namespace
{
    // A small arrowhead on the wire, pointing along the signal (left to right).
    void arrowAt (juce::Graphics& g, float x, float y, float size)
    {
        juce::Path p;
        p.startNewSubPath (x - size, y - 0.75f * size);
        p.lineTo (x, y);
        p.lineTo (x - size, y + 0.75f * size);
        g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void FxRoutingView::paint (juce::Graphics& g)
{
    // (The window's scale sets the board's tile size: if it changed since the last layout - a
    // host resize moves no bounds here - re-project before drawing, so what is drawn is what
    // the mouse hits.)
    if (std::abs (juce::Component::getApproximateScaleFactorForComponent (this) - layoutScale) > 0.01f)
        layoutChain();
    const auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0x1d3c3225));
    g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 13.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff9f5ee), r.getX(), r.getY(), juce::Colour (0xfff2ece3), r.getRight(), r.getBottom(), false));
    g.fillRoundedRectangle (r.reduced (0.5f), 13.0f);
    g.setColour (juce::Colour (0xffd3cabe));
    g.drawRoundedRectangle (r.reduced (0.5f), 13.0f, 1.0f);

    // Header: the dot and ROUTING; + ADD EFFECT; CLASSIC / CUSTOM by the gear; the rule under it.
    g.setColour (sage);
    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ static_cast<float> (headerArea.getX()) + 4.0f, static_cast<float> (headerArea.getCentreY()) }));
    g.setColour (ink);
    g.setFont (font (15.5f * mixerScale, fonts::Weight::semibold, 0.07f));
    g.drawText ("ROUTING", headerArea.withTrimmedLeft (17), juce::Justification::centredLeft, false);
    const bool custom = route().custom;
    {
        const auto statusArea = juce::Rectangle<int> (add->getRight() + 8, headerArea.getY(), gear->getX() - 10 - add->getRight() - 8, headerArea.getHeight());
        g.setColour (custom ? warm.darker (0.12f) : green);
        g.setFont (font (11.5f * std::min (mixerScale, 1.15f), fonts::Weight::semibold, 0.05f));
        const bool mainSend = engine.fxRoute().mainSend;
        g.drawText (mainSend ? "MAIN + SEND" : (custom ? "CUSTOM" : "CLASSIC"), statusArea, juce::Justification::centredRight, false);
    }
    paintTabs (g);
    g.setColour (juce::Colour (0xffe5dbcf));
    g.fillRect (juce::Rectangle<int> (headerArea.getX(), headerArea.getBottom() + 4, headerArea.getWidth(), 1));

    paintMixer (g);

    // The wires: one line per row, from the mixer (row 1) to OUTPUT (the last row); between
    // two rows, CONT. 2 and FROM 1 and the quiet return between them.
    const float wireWidth = 1.6f;
    g.setColour (wire);
    for (const auto& row : rows)
        g.fillRect (juce::Rectangle<float> (static_cast<float> (row.left), static_cast<float> (row.centreY) - 0.5f * wireWidth, static_cast<float> (row.right - row.left), wireWidth));
    if (rows.size() > 1)
    {
        const float midY = 0.5f * static_cast<float> (rows[0].centreY + rows[1].centreY);
        juce::Path ret;
        ret.startNewSubPath (static_cast<float> (contOut.getCentreX()), static_cast<float> (contOut.getBottom()));
        ret.lineTo (static_cast<float> (contOut.getCentreX()), midY);
        ret.lineTo (static_cast<float> (contIn.getCentreX()), midY);
        ret.lineTo (static_cast<float> (contIn.getCentreX()), static_cast<float> (contIn.getY()));
        juce::Path dashed;
        const float dashes[] { 3.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, ret, dashes, 2);
        g.setColour (wire.withAlpha (0.55f));
        g.fillPath (dashed);
        // (no words: the wire runs on to the turn, a small node, down and back, on into row 2)
        g.setColour (wire);
        g.fillRect (juce::Rectangle<float> (static_cast<float> (contOut.getX()), static_cast<float> (rows[0].centreY) - 0.5f * wireWidth,
                                            static_cast<float> (contOut.getWidth() / 2), wireWidth));
        g.fillRect (juce::Rectangle<float> (static_cast<float> (contIn.getCentreX()), static_cast<float> (rows[1].centreY) - 0.5f * wireWidth,
                                            static_cast<float> (contIn.getRight() - contIn.getCentreX()), wireWidth));
        g.setColour (wire.withAlpha (0.55f));
        g.drawLine (static_cast<float> (contOut.getCentreX()), static_cast<float> (rows[0].centreY), static_cast<float> (contOut.getCentreX()),
                    static_cast<float> (contOut.getBottom()), 1.0f);
        g.drawLine (static_cast<float> (contIn.getCentreX()), static_cast<float> (contIn.getY()), static_cast<float> (contIn.getCentreX()),
                    static_cast<float> (rows[1].centreY), 1.0f);
        g.setColour (wire);
        for (const auto& box : { contOut, contIn })
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (box.getCentre().toFloat()));
    }
    // The stages' splits and joins: down / up to each lane, across, back to the line.
    for (const auto& grp : groups)
    {
        const float x0 = static_cast<float> (grp.bounds.getX()), x1 = static_cast<float> (grp.bounds.getRight());
        const float gy = static_cast<float> (grp.bounds.getCentreY());
        const float top = static_cast<float> (grp.laneCentres.front()), bottom = static_cast<float> (grp.laneCentres.back());
        const float inset = 0.45f * static_cast<float> (metrics.branch);
        juce::Path p;
        p.startNewSubPath (x0, gy);
        p.lineTo (x0 + inset, gy);
        p.startNewSubPath (x0 + inset, top);
        p.lineTo (x0 + inset, bottom);
        for (const int lc : grp.laneCentres)
        {
            p.startNewSubPath (x0 + inset, static_cast<float> (lc));
            p.lineTo (x1 - inset, static_cast<float> (lc));
        }
        p.startNewSubPath (x1 - inset, top);
        p.lineTo (x1 - inset, bottom);
        p.startNewSubPath (x1 - inset, gy);
        p.lineTo (x1, gy);
        // (the middle of the line under the stage is not signal: cleared)
        g.setColour (juce::Colour (0xfff5f0e8));
        g.fillRect (juce::Rectangle<float> (x0 + inset + 0.5f, gy - 1.2f, x1 - x0 - 2.0f * inset - 1.0f, 2.4f));
        g.setColour (grp.split ? juce::Colour (0xff8f9a9c) : juce::Colour (0xff91aa9e));
        g.strokePath (p, juce::PathStrokeType (wireWidth));
        // SPLIT / JOIN where the lanes leave and meet the line.
        g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ x0 + inset, gy }));
        g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ x1 - inset, gy }));
        // (a splitter's name above the stage, clear of the lanes; a parallel stage reads as one)
        if (grp.split)
        {
            const auto f = font (9.5f * metrics.b, fonts::Weight::semibold, 0.08f);
            const juce::String word ("SPLIT");
            const float tw = juce::GlyphArrangement::getStringWidth (f, word) + 12.0f;
            const auto pill = juce::Rectangle<float> (tw, 14.0f).withCentre ({ 0.5f * (x0 + x1), static_cast<float> (grp.bounds.getY()) - 9.0f });
            g.setColour (juce::Colour (0xfff5f0e8));
            g.fillRoundedRectangle (pill, 7.0f);
            g.setFont (f);
            g.setColour (juce::Colour (0xff6f8075));
            g.drawText (word, pill, juce::Justification::centred, false);
        }
    }
    // The gaps: an arrowhead on the wire (the drop target, while dragging, a +).
    for (int i = 0; i < static_cast<int> (gapList.size()); ++i)
    {
        const auto& gap = gapList[static_cast<std::size_t> (i)];
        const bool target = dragging && i == dropGap;
        const float cy = static_cast<float> (gap.bounds.getCentreY());
        if (target)
        {
            const auto c = juce::Point<float> (static_cast<float> (gap.bounds.getCentreX()), cy);
            g.setColour (juce::Colour (0x88d9e7df));
            g.fillRoundedRectangle (gap.bounds.toFloat(), 6.0f);
            g.setColour (juce::Colour (0xffc9dfd0));
            g.fillEllipse (juce::Rectangle<float> (25.0f, 25.0f).withCentre (c));
            g.setColour (juce::Colour (0xfff5f0e8));
            g.fillEllipse (juce::Rectangle<float> (17.0f, 17.0f).withCentre (c));
            g.setColour (juce::Colour (0xff426f5a));
            g.setFont (font (13.0f, fonts::Weight::bold));
            g.drawText ("+", juce::Rectangle<float> (17.0f, 17.0f).withCentre (c), juce::Justification::centred, false);
            continue;
        }
        // (into the next block: its arrow right before it)
        const float x = gap.bounds.getWidth() > 30 ? static_cast<float> (gap.bounds.getRight()) - 3.0f : static_cast<float> (gap.bounds.getCentreX()) + 3.0f;
        g.setColour (juce::Colour (0xffa59a8e));
        arrowAt (g, x, cy, 4.0f);
    }
    for (const auto& t : placed)
        paintTile (g, t);
    for (const auto& sum : summaryList)
        paintSummary (g, sum);

    // OUTPUT: the small terminal at the wire's end.
    g.setColour (juce::Colour (0xfffaf6ee));
    g.fillRoundedRectangle (outputBox.toFloat(), 6.0f);
    g.setColour (juce::Colour (0xffbfb4a6));
    g.drawRoundedRectangle (outputBox.toFloat().reduced (0.5f), 6.0f, 1.0f);
    g.setColour (juce::Colour (0xff5d5750));
    g.setFont (font (10.5f * metrics.b, fonts::Weight::semibold, 0.08f));
    g.drawText ("OUTPUT", outputBox, juce::Justification::centred, false);
}

juce::Rectangle<int> FxRoutingView::mixerRow (int index) const
{
    // The cards' rows share the column's whole height (no title, no empty foot): one row per
    // card, however many the instrument has.
    const auto area = mixerArea.withTrimmedRight (16);
    const int count = static_cast<int> (levels.size());
    const int y0 = area.getY() + area.getHeight() * index / count, y1 = area.getY() + area.getHeight() * (index + 1) / count;
    return { area.getX(), y0, area.getWidth(), y1 - y0 };
}

void FxRoutingView::paintMixer (juce::Graphics& g) const
{
    g.setColour (juce::Colour (0xffd9d0c5));
    g.fillRect (juce::Rectangle<int> (mixerArea.getRight(), mixerArea.getY(), 1, mixerArea.getHeight()));
    const float k = mixerScale;
    const char* names[3] { "OSC 1", "OSC 2", "HYBRID" };
    const juce::String kinds[3] { textOf (host, params::osc1Model), textOf (host, params::osc2Model), textOf (host, params::hybridType) };
    const bool sends = currentTab == 1 && engine.fxRoute().mainSend;
    const juce::String levelIds[3] { sends ? params::sendA : params::osc1Level, sends ? params::sendB : params::osc2Level,
                                     sends ? params::sendC : juce::String ("layerB.level") };
    for (int i = 0; i < 3; ++i)
    {
        const auto row = mixerRow (i);
        if (i < 2)
        {
            g.setColour (juce::Colour (0xffe7dfd5));
            g.fillRect (row.withTop (row.getBottom() - 1));
        }
        const auto letter = juce::Rectangle<float> (28.0f * k, 28.0f * k).withCentre ({ static_cast<float> (row.getX()) + 14.0f * k, static_cast<float> (row.getCentreY()) });
        g.setColour (mixerTileColour (i));
        g.fillRoundedRectangle (letter, 5.0f);
        g.setColour (juce::Colours::white);
        g.setFont (font (15.0f * k, fonts::Weight::medium));
        g.drawText (juce::String::charToString (static_cast<juce::juce_wchar> ('A' + i)), letter, juce::Justification::centred, false);
        const auto& knob = levels[static_cast<std::size_t> (i)]->getBounds();
        const int textX = row.getX() + juce::roundToInt (38.0f * k), textW = knob.getX() - 6 - textX;
        g.setColour (juce::Colour (0xff3a3e3a));
        g.setFont (font (13.5f * k, fonts::Weight::semibold, 0.03f));
        g.drawFittedText (names[i], juce::Rectangle<int> (textX, row.getCentreY() - juce::roundToInt (17.0f * k), textW, juce::roundToInt (17.0f * k)), juce::Justification::centredLeft, 1, 0.9f);
        g.setColour (juce::Colour (0xff958b80));
        g.setFont (font (10.5f * k, fonts::Weight::regular, 0.02f));
        g.drawText (kinds[i].toUpperCase(), juce::Rectangle<int> (textX, row.getCentreY() + 1, textW, juce::roundToInt (14.0f * k)), juce::Justification::centredLeft, true);
        // The level under its knob (the parameter's own text: dB / % as on the card).
        g.setColour (juce::Colour (0xff5f7369));
        g.setFont (font (10.5f * k, fonts::Weight::medium));
        g.drawText (textOf (host, levelIds[i]), juce::Rectangle<int> (knob.getCentreX() - 40, knob.getBottom() + 1, 80, juce::roundToInt (13.0f * k)), juce::Justification::centred, false);
    }
}

void FxRoutingView::paintTile (juce::Graphics& g, const Tile& t, bool ghost) const
{
    // V5 PATTERNED HARDWARE: the device in miniature - its enamel, its motif in the left
    // quarter, the name bold and clear, its mode, its real knobs, its status light (the
    // effect's real ON), the same identity as its rack.
    const auto& id = visualIdentityFor (t.id.type);
    const auto r = t.bounds.toFloat();
    const float b = metrics.b, radius = 8.0f * b;
    const bool isSelected = selected && *selected == t.id;
    const bool beingDragged = ! ghost && dragging && pressed && *pressed == t.id;
    const bool hover = ! ghost && hovered && *hovered == t.id;
    const auto geo = tileGeometry (r, b, t.stacked);
    juce::Graphics::ScopedSaveState state (g);
    if (beingDragged)
        g.beginTransparencyLayer (0.36f);
    g.setColour (juce::Colours::black.withAlpha (hover ? 0.20f : 0.15f));
    g.fillRoundedRectangle (r.translated (0.0f, (hover ? 2.5f : 2.0f) * b), radius);
    juce::Path face;
    face.addRoundedRectangle (r, radius);
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (face);
        g.setGradientFill (juce::ColourGradient (id.enamel.brighter (hover ? 0.10f : 0.06f), 0.0f, r.getY(), id.enamel.darker (0.07f), 0.0f, r.getBottom(), false));
        g.fillRect (r);
        // The motif: the left quarter, fading into the enamel toward the name.
        for (int i = -1; i < 6; ++i)
        {
            const float solid = 0.6f * geo.pattern.getWidth(), stepW = (geo.pattern.getWidth() - solid + 10.0f * b) / 6.0f;
            const auto strip = i < 0 ? geo.pattern.withWidth (solid) : juce::Rectangle<float> (geo.pattern.getX() + solid + stepW * static_cast<float> (i), r.getY(), stepW, r.getHeight());
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (strip.getSmallestIntegerContainer());
            g.beginTransparencyLayer (i < 0 ? 1.0f : 1.0f - (static_cast<float> (i) + 0.5f) / 6.0f);
            paintFxMotif (g, id.motif, geo.pattern.withTrimmedTop (3.0f * b).withTrimmedBottom (3.0f * b).withWidth (geo.pattern.getWidth() + 10.0f * b), id, MotifScale::mini);
            g.endTransparencyLayer();
        }
        if (t.bypassed)
        {
            // Bypassed: the face greyed under a cream veil, still in its place.
            g.setColour (juce::Colour (0xfff2ece3).withAlpha (0.42f));
            g.fillRect (r);
        }
    }
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.drawRoundedRectangle (r.reduced (1.0f).withTrimmedBottom (1.0f), radius - 1.0f, 0.8f);
    g.setColour (id.edge.withAlpha (0.85f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);

    // The name, the mode.
    juce::String name = id.tileName;
    if (! fx::isLegacy (t.id.type) && t.id.slot > 0)
        name << " " << (t.id.slot + 1);
    const float textRight = r.getRight() - 30.0f * b;
    const float nameY = t.stacked ? r.getCentreY() - 16.0f * b : r.getY() + 9.0f * b;
    g.setColour (id.text.withMultipliedAlpha (t.bypassed ? 0.7f : 1.0f));
    g.setFont (font ((t.stacked ? 13.0f : 13.5f) * b, fonts::Weight::bold, 0.06f));
    g.drawFittedText (name, juce::Rectangle<float> (geo.textX, nameY, textRight - geo.textX, 17.0f * b).getSmallestIntegerContainer(), juce::Justification::centredLeft, 1, 0.88f);
    g.setColour (id.mutedText.withMultipliedAlpha (t.bypassed ? 0.8f : 1.0f));
    g.setFont (font (10.5f * b, fonts::Weight::semibold, 0.08f));
    g.drawText (t.bypassed ? juce::String ("BYPASSED") : tileModeText (host, t.id, subtitleOf (t.id)),
                juce::Rectangle<float> (geo.textX, nameY + 17.0f * b, r.getRight() - 8.0f * b - geo.textX, 13.0f * b), juce::Justification::centredLeft, true);
    // Its knobs' captions (their values while touched).
    if (! ghost)
        for (const auto& k : tileKnobs)
            if (k->owner == t.id)
            {
                const auto kb = k->getBounds().toFloat();
                const bool live = k->isMouseOverOrDragging() || k->hasKeyboardFocus (false);
                g.setColour (live ? id.text : id.mutedText);
                g.setFont (font (9.5f * b, live ? fonts::Weight::semibold : fonts::Weight::medium, 0.02f));
                g.drawText (k->shownText(), juce::Rectangle<float> (kb.getCentreX() - 30.0f * b, kb.getBottom() - 2.0f * b, 60.0f * b, 12.0f * b), juce::Justification::centred, false);
            }

    // The status light: the effect's real ON (its own hit target: clicking the tile never flips it).
    const auto light = t.light.toFloat().withSizeKeepingCentre (8.5f * b, 8.5f * b);
    g.setColour (juce::Colours::black.withAlpha (0.30f));
    g.fillEllipse (light.expanded (2.0f * b));
    if (t.bypassed)
    {
        g.setColour (id.enamel.darker (0.45f));
        g.fillEllipse (light);
        g.setColour (id.mutedText.withAlpha (0.5f));
        g.drawEllipse (light.reduced (0.5f), 1.0f);
    }
    else
    {
        g.setColour (id.statusActive.withAlpha (0.30f));
        g.fillEllipse (light.expanded (2.5f * b));
        g.setGradientFill (juce::ColourGradient (juce::Colours::white, light.getX(), light.getY(), id.statusActive.darker (0.08f), light.getRight(), light.getBottom(), false));
        g.fillEllipse (light);
    }
    if (beingDragged)
        g.endTransparencyLayer();
    if (isSelected)
    {
        g.setColour (sage);
        g.drawRoundedRectangle (r.expanded (3.0f), radius + 3.0f, 2.0f);
    }
    // While dragging over it: STACK (a drop here makes the two parallel).
    const bool target = ! ghost && dragging && dropTile >= 0 && placed[static_cast<std::size_t> (dropTile)].id == t.id;
    if (target)
    {
        g.setColour (juce::Colour (0xe8f1ece2));
        g.fillRoundedRectangle (r.reduced (1.0f), radius - 1.0f);
        juce::Path dash;
        const float dashes[] { 4.0f, 3.0f };
        juce::Path outline;
        outline.addRoundedRectangle (r.reduced (1.0f), radius - 1.0f);
        juce::PathStrokeType (1.5f).createDashedStroke (dash, outline, dashes, 2);
        g.setColour (juce::Colour (0xff739589));
        g.fillPath (dash);
        g.setColour (juce::Colour (0xff416a55));
        g.setFont (font (10.5f * b, fonts::Weight::semibold, 0.045f));
        g.drawText ("STACK IN PARALLEL", r, juce::Justification::centred, false);
    }
}

void FxRoutingView::paintSummary (juce::Graphics& g, const Summary& sum) const
{
    // A group summary (V5): a neutral graphite enamel card on a fanned stack, its members'
    // colours in bands at its left, the count, what it is, how many are on.
    const auto r = sum.bounds.toFloat();
    const float b = metrics.b, radius = 8.0f * b;
    const juce::Colour face (0xff6d6962), bone (0xfff5eddf);
    for (int layer = 2; layer >= 1; --layer)
    {
        const auto back = r.translated (3.0f * static_cast<float> (layer), -3.0f * static_cast<float> (layer));
        g.setColour (face.brighter (0.25f * static_cast<float> (layer)));
        g.fillRoundedRectangle (back, radius);
        g.setColour (face.darker (0.3f));
        g.drawRoundedRectangle (back.reduced (0.5f), radius, 1.0f);
    }
    const bool hover = hovered && std::find (sum.members.begin(), sum.members.end(), *hovered) != sum.members.end();
    g.setColour (juce::Colours::black.withAlpha (0.15f));
    g.fillRoundedRectangle (r.translated (0.0f, 2.0f), radius);
    g.setGradientFill (juce::ColourGradient (face.brighter (hover ? 0.12f : 0.06f), 0.0f, r.getY(), face.darker (0.08f), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setColour (face.darker (0.45f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    {
        juce::Graphics::ScopedSaveState clip (g);
        juce::Path rounded;
        rounded.addRoundedRectangle (r, radius);
        g.reduceClipRegion (rounded);
        const float band = r.getHeight() / static_cast<float> (std::max<std::size_t> (1, sum.members.size()));
        for (std::size_t i = 0; i < sum.members.size(); ++i)
        {
            g.setColour (fxAccent (sum.members[i].type));
            g.fillRect (juce::Rectangle<float> (r.getX(), r.getY() + band * static_cast<float> (i), 10.0f * b, band));
        }
    }
    int on = 0;
    for (const auto& id : sum.members)
    {
        bool bypassed = false;
        if (fx::isLegacy (id.type))
        {
            std::function<void (const std::vector<fx::Node>&)> find = [&] (const std::vector<fx::Node>& chain) {
                for (const auto& node : chain)
                {
                    if (node.kind == fx::Node::Kind::effect && node.fx == id)
                        bypassed = node.bypassed;
                    for (const auto& lane : node.lanes)
                        find (lane);
                }
            };
            find (route().chain);
        }
        else
        {
            const auto param = juce::String ("analog.fx.") + fx::typeKey (id.type) + "." + juce::String (id.slot + 1) + ".on";
            bypassed = host.apvts().getParameter (param) != nullptr && host.parameterValue (param) < 0.5f;
        }
        on += bypassed ? 0 : 1;
    }
    const float padX = 20.0f * b;
    g.setColour (bone);
    g.setFont (font (13.5f * b, fonts::Weight::semibold, 0.035f));
    const juce::String title = juce::String (static_cast<int> (sum.members.size())) + (sum.members.size() == 1 ? " EFFECT" : " EFFECTS");
    g.drawText (title, juce::Rectangle<float> (r.getX() + padX, r.getY() + 10.0f * b, r.getWidth() - padX - 30.0f * b, 18.0f * b), juce::Justification::centredLeft, false);
    g.setColour (bone.withAlpha (0.72f));
    g.setFont (font (10.5f * b));
    const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
    juce::StringArray names;
    for (const auto& id : sum.members)
        names.add (juce::String (fx::typeName (id.type)).toLowerCase().replaceSection (0, 1, juce::String (fx::typeName (id.type)).substring (0, 1)));
    const juce::String what = sum.group ? juce::String ("Parallel") + dot + juce::String (sum.lanes) + " lanes" : names.joinIntoString (dot);
    g.drawText (what, juce::Rectangle<float> (r.getX() + padX, r.getY() + 29.0f * b, r.getWidth() - padX - 8.0f, 14.0f * b), juce::Justification::centredLeft, true);
    g.setColour (on == static_cast<int> (sum.members.size()) ? juce::Colour (0xffcfe2d4) : juce::Colour (0xfff0c99a));
    g.drawText (juce::String (on) + " of " + juce::String (static_cast<int> (sum.members.size())) + " on" + dot + juce::String::fromUTF8 ("open \xe2\x96\xbe"),
                juce::Rectangle<float> (r.getX() + padX, r.getY() + 44.0f * b, r.getWidth() - padX - 8.0f, 14.0f * b), juce::Justification::centredLeft, true);
    if (selected && std::find (sum.members.begin(), sum.members.end(), *selected) != sum.members.end())
    {
        g.setColour (sage);
        g.drawRoundedRectangle (r.expanded (2.5f), 9.0f, 1.5f);
    }
}

void FxRoutingView::showSummaryMenu (int index)
{
    if (index < 0 || index >= static_cast<int> (summaryList.size()))
        return;
    const auto sum = summaryList[static_cast<std::size_t> (index)];
    juce::PopupMenu m;
    m.addSectionHeader (sum.group ? "Parallel stage (shown as a group)" : "The chain's end (shown as a group)");
    for (const auto& id : sum.members)
    {
        juce::String name = fx::typeName (id.type);
        if (! fx::isLegacy (id.type) && fx::capacity[static_cast<std::size_t> (id.type)] > 1)
            name << " " << (id.slot + 1);
        juce::PopupMenu one;
        one.addItem ("Quick settings", [this, id] { showQuick (id); });
        one.addItem ("Open rack", [this, id] { openEditor (id); });
        one.addItem ("Bypass / enable", [this, id] { toggleBypass (id); });
        one.addItem ("Remove", [this, id] { remove (id); });
        m.addSubMenu (name, one);
    }
    m.addSeparator();
    m.addItem ("Too long to show at a readable size: remove or merge effects to see every tile", false, false, [] {});
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (sum.bounds)));
}

void FxRoutingView::paintTabs (juce::Graphics& g) const
{
    if (! engine.fxRoute().mainSend)
        return;
    const char* names[3] { "MAIN", "SEND", "OVERVIEW" };
    const auto all = tabAreas[0].getUnion (tabAreas[2]).toFloat();
    g.setColour (juce::Colour (0xffece5da));
    g.fillRoundedRectangle (all, 6.0f);
    g.setColour (edge);
    g.drawRoundedRectangle (all.reduced (0.5f), 6.0f, 1.0f);
    for (int i = 0; i < 3; ++i)
    {
        const auto r = tabAreas[static_cast<std::size_t> (i)].toFloat();
        const bool on = i == currentTab;
        if (on)
        {
            g.setColour (juce::Colour (0xfffbf8f2));
            g.fillRoundedRectangle (r.reduced (2.0f), 5.0f);
            g.setColour (edge.darker (0.05f));
            g.drawRoundedRectangle (r.reduced (2.5f), 5.0f, 1.0f);
        }
        else if (i > 0 && i - 1 != currentTab)
        {
            g.setColour (edge);
            g.fillRect (juce::Rectangle<float> (r.getX(), r.getY() + 6.0f, 1.0f, r.getHeight() - 12.0f));
        }
        g.setColour (on ? ink : soft);
        g.setFont (font (10.5f, fonts::Weight::semibold, 0.08f));
        g.drawText (names[i], r, juce::Justification::centred, false);
    }
}

void FxRoutingView::paintOverview (juce::Graphics& g) const
{
    // Both paths in miniature, read left to right: MAIN (the cards' levels into its inserts,
    // OUTPUT) and SEND (the cards' sends into its wet chain, back to OUTPUT).
    const auto full = engine.fxRoute();
    auto area = zoneArea.reduced (0, 8);
    g.setColour (juce::Colour (0xfff7f2ea));
    g.fillRect (zoneArea.withTrimmedLeft (-28));
    const int laneH = std::min (96, area.getHeight() / 2 - 10);
    auto lane = [&] (juce::Rectangle<int> r, const char* title, const std::vector<fx::Node>& chain, const juce::String& feed) {
        g.setColour (juce::Colour (0xfffbf8f2));
        g.fillRoundedRectangle (r.toFloat(), 10.0f);
        g.setColour (edge);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.0f, 1.0f);
        auto inner = r.reduced (16, 0);
        g.setColour (ink);
        g.setFont (font (13.0f, fonts::Weight::semibold, 0.08f));
        g.drawText (title, inner.removeFromLeft (64), juce::Justification::centredLeft, false);
        g.setColour (soft);
        g.setFont (font (11.0f, fonts::Weight::medium, 0.04f));
        const int feedW = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (11.0f, fonts::Weight::medium, 0.04f), feed)) + 12;
        g.drawText (feed, inner.removeFromLeft (feedW), juce::Justification::centredLeft, false);
        const float cy = static_cast<float> (r.getCentreY());
        g.setColour (wire);
        g.drawLine (static_cast<float> (inner.getX()), cy, static_cast<float> (inner.getRight() - 64), cy, 1.4f);
        auto chip = [&] (const juce::String& text, juce::Colour accent) {
            const int w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (11.0f, fonts::Weight::semibold, 0.05f), text)) + 30;
            auto c = inner.removeFromLeft (w).withSizeKeepingCentre (w, 30).toFloat();
            inner.removeFromLeft (12);
            g.setColour (juce::Colour (0xfffdfbf7));
            g.fillRoundedRectangle (c, 6.0f);
            g.setColour (edge.darker (0.05f));
            g.drawRoundedRectangle (c.reduced (0.5f), 6.0f, 1.0f);
            g.setColour (accent);
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ c.getX() + 12.0f, c.getCentreY() }));
            g.setColour (ink);
            g.setFont (font (11.0f, fonts::Weight::semibold, 0.05f));
            g.drawText (text, c.withTrimmedLeft (20.0f), juce::Justification::centredLeft, false);
        };
        std::function<void (const std::vector<fx::Node>&)> walk = [&] (const std::vector<fx::Node>& c) {
            for (const auto& n : c)
            {
                if (n.kind == fx::Node::Kind::effect)
                    chip (fx::typeName (n.fx.type), fxAccent (n.fx.type));
                else
                {
                    juce::String names;
                    std::vector<fx::InstanceId> ids;
                    for (const auto& l : n.lanes)
                        for (const auto& id : fx::instancesOf (fx::Graph { true, l, false, {} }))
                            ids.push_back (id);
                    for (const auto& id : ids)
                        names << (names.isEmpty() ? "" : juce::String::fromUTF8 (" \xe2\x80\x96 ")) << fx::typeName (id.type);
                    chip (names, ids.empty() ? sage : fxAccent (ids.front().type));
                }
            }
        };
        if (chain.empty())
            chip ("NOTHING", weak);
        walk (chain);
        g.setColour (soft);
        g.setFont (font (11.0f, fonts::Weight::semibold, 0.08f));
        g.drawText ("OUTPUT", r.withLeft (r.getRight() - 76), juce::Justification::centred, false);
    };
    const auto percent = [this] (const juce::String& id) { return juce::String (juce::roundToInt (host.parameterValue (id))) + "%"; };
    const juce::String feed = "A " + percent (params::sendA) + juce::String::fromUTF8 (" \xc2\xb7 B ") + percent (params::sendB)
                              + juce::String::fromUTF8 (" \xc2\xb7 C ") + percent (params::sendC);
    area = area.withSizeKeepingCentre (area.getWidth(), 2 * laneH + 16);
    lane (area.removeFromTop (laneH), "MAIN", full.chain, "A B C");
    area.removeFromTop (16);
    lane (area.removeFromTop (laneH), "SEND", full.send, feed);
}

void FxRoutingView::paintOverChildren (juce::Graphics& g)
{
    if (currentTab == 2 && engine.fxRoute().mainSend)
        paintOverview (g);
    // The dragged tile follows the pointer (a light copy).
    if (dragging && pressed)
    {
        Tile ghost;
        ghost.id = *pressed;
        ghost.bounds = juce::Rectangle<int> (metrics.tileW, metrics.tileH).withCentre (dragAt);
        ghost.light = juce::Rectangle<int> (24, 24).withCentre ({ ghost.bounds.getRight() - 14, ghost.bounds.getY() + 14 });
        juce::Graphics::ScopedSaveState s (g);
        g.beginTransparencyLayer (0.9f);
        paintTile (g, ghost, true);
        g.endTransparencyLayer();
    }
}

//==============================================================================
void FxRoutingView::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (engine.fxRoute().mainSend)
        for (int t = 0; t < 3; ++t)
            if (tabAreas[static_cast<std::size_t> (t)].contains (e.getPosition()))
            {
                setTab (t);
                return;
            }
    if (library != nullptr && library->isVisible() && ! library->getBounds().contains (e.getPosition()))
        showLibrary (false);
    if (quick != nullptr && tileAt (e.getPosition()) < 0)
        showQuick (std::nullopt);   // (a click elsewhere closes the quick settings)
    const int i = tileAt (e.getPosition());
    pressed.reset();
    if (i < 0)
    {
        if (const int si = summaryAt (e.getPosition()); si >= 0)
        {
            showSummaryMenu (si);   // (a group summary: its effects, each reachable)
            return;
        }
        select (std::nullopt);
        return;
    }
    const auto& t = placed[static_cast<std::size_t> (i)];
    if (e.mods.isPopupMenu())
    {
        select (t.id);
        showTileMenu (t.id);
        return;
    }
    if (t.light.contains (e.getPosition()))
    {
        toggleBypass (t.id);
        return;
    }
    pressed = t.id;
    select (t.id);
}

void FxRoutingView::mouseDrag (const juce::MouseEvent& e)
{
    if (! pressed || e.getDistanceFromDragStart() < 5)
        return;
    dragging = true;
    dragAt = e.getPosition();
    dropTile = tileAt (dragAt);
    if (dropTile >= 0 && placed[static_cast<std::size_t> (dropTile)].id == *pressed)
        dropTile = -1;
    dropGap = dropTile >= 0 ? -1 : gapAt (dragAt);
    repaint();
}

void FxRoutingView::mouseUp (const juce::MouseEvent& e)
{
    if (dragging && pressed)
    {
        // The drop commits (the only moment the sound changes).
        const auto id = *pressed;
        if (dropTile >= 0)
            stackOn (id, placed[static_cast<std::size_t> (dropTile)].id);
        else if (dropGap >= 0)
        {
            const auto gap = gapList[static_cast<std::size_t> (dropGap)];
            moveTo (id, gap.path, gap.index);
        }
    }
    else if (pressed && ! e.mods.isPopupMenu())
        showQuick (*pressed);   // (a click: the tile's quick settings; a drag never opens them)
    dragging = false;
    dropTile = dropGap = -1;
    pressed.reset();
    repaint();
}

void FxRoutingView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const int i = tileAt (e.getPosition()); i >= 0 && ! placed[static_cast<std::size_t> (i)].light.contains (e.getPosition()))
    {
        showQuick (std::nullopt);   // (the full rack instead)
        openEditor (placed[static_cast<std::size_t> (i)].id);
    }
}

void FxRoutingView::mouseMove (const juce::MouseEvent& e)
{
    const int i = tileAt (e.getPosition());
    std::optional<InstanceId> now;
    if (i >= 0)
        now = placed[static_cast<std::size_t> (i)].id;
    setMouseCursor (i >= 0 ? juce::MouseCursor::DraggingHandCursor
                           : (summaryAt (e.getPosition()) >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor));
    if (now != hovered)
    {
        hovered = now;
        repaint();
    }
}

void FxRoutingView::mouseExit (const juce::MouseEvent&)
{
    hovered.reset();
    repaint();
}

void FxRoutingView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // (V4: the board never scrolls; the wheel belongs to the page.)
    juce::Component::mouseWheelMove (e, wheel);
}

bool FxRoutingView::keyPressed (const juce::KeyPress& key)
{
    const auto o = order();
    if (key == juce::KeyPress::escapeKey)
    {
        if (library != nullptr && library->isVisible())
            showLibrary (false);
        else if (quick != nullptr)
            showQuick (std::nullopt);
        else if (auto* editor = findParentComponentOfClass<andor::shell::ShellEditor>())
            editor->setSourceViewShown (false);
        return true;
    }
    if (o.empty())
        return false;
    const auto it = selected ? std::find (o.begin(), o.end(), *selected) : o.end();
    if (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey)
    {
        const int delta = key.getKeyCode() == juce::KeyPress::leftKey ? -1 : 1;
        if (key.getModifiers().isAltDown() && selected)
        {
            if (auto g = route(); fx::nudgeEffect (g, *selected, delta))
                apply (g, "Move effect");
            return true;
        }
        int index = it == o.end() ? (delta > 0 ? 0 : static_cast<int> (o.size()) - 1) : static_cast<int> (it - o.begin()) + delta;
        select (o[static_cast<std::size_t> (juce::jlimit (0, static_cast<int> (o.size()) - 1, index))]);
        return true;
    }
    if (! selected)
        return false;
    if (key == juce::KeyPress::spaceKey)
        return toggleBypass (*selected);
    if (key == juce::KeyPress::returnKey)
    {
        openEditor (*selected);
        return true;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        return remove (*selected);
    return false;
}

void FxRoutingView::showTileMenu (InstanceId id)
{
    juce::PopupMenu m;
    m.addItem ("Quick settings", [this, id] { showQuick (id); });
    m.addItem ("Expand", [this, id] { openEditor (id); });
    bool bypassed = false;
    for (const auto& t : placed)
        if (t.id == id)
            bypassed = t.bypassed;
    m.addItem (bypassed ? "Enable" : "Bypass", [this, id] { toggleBypass (id); });
    m.addSeparator();
    m.addItem ("Move left", [this, id] {
        if (auto g = route(); fx::nudgeEffect (g, id, -1))
            apply (g, "Move effect left");
    });
    m.addItem ("Move right", [this, id] {
        if (auto g = route(); fx::nudgeEffect (g, id, 1))
            apply (g, "Move effect right");
    });
    juce::PopupMenu stack;
    for (const auto& other : fx::instancesOf (route()))
        if (other != id)
            stack.addItem (fx::typeName (other.type), [this, id, other] { stackOn (id, other); });
    m.addSubMenu ("Place parallel with", stack);
    fx::ChainPath path;
    int at = 0;
    const bool inGroup = fx::locate (route(), id, path, at) && ! path.empty();
    m.addItem ("Out of parallel", inGroup, false, [this, id] {
        if (auto g = route(); fx::unstackEffect (g, id))
            apply (g, "Effect to series");
    });
    m.addSeparator();
    m.addItem ("Remove", [this, id] { remove (id); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (std::find_if (placed.begin(), placed.end(), [id] (const Tile& t) { return t.id == id; })->bounds)));
}

std::unique_ptr<juce::Component> createFxRoutingView (osp::plugin::InstrumentHost& host)
{
    return std::make_unique<FxRoutingView> (host);
}

} // namespace andor::analog
