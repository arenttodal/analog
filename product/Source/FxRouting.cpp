#include "FxRouting.h"
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
    const juce::Colour wire { 0xffb9ada0 }, wireStrong { 0xff9fb3a8 }, sage { 0xff8caaa0 }, warm { 0xffbc7d51 }, green { 0xff658276 };

    juce::Font font (float size, fonts::Weight weight = fonts::Weight::regular, float tracking = 0.0f) { return fonts::make (size, weight, tracking); }

    constexpr int tileW = 129, tileH = 74, stackedH = 68, gapW = 31, stackGap = 9, branch = 16;

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
        { Type::utility, "UTILITIES", "Gain, pan, width, polarity, mono bass" },
    };
}

juce::Colour fxAccent (Type type)
{
    switch (type)
    {
        case Type::drive: return juce::Colour (0xffc77f50);
        case Type::movement: return juce::Colour (0xff819aaf);
        case Type::echo: return juce::Colour (0xff7baba7);
        case Type::space: return juce::Colour (0xffab95b2);
        case Type::spread: return juce::Colour (0xff879f8c);
        case Type::compressor: return juce::Colour (0xffb0905f);
        case Type::eq: return juce::Colour (0xff8f9f7c);
        case Type::filter: return juce::Colour (0xffa68a6c);
        case Type::phaser: return juce::Colour (0xff8d98b8);
        case Type::flanger: return juce::Colour (0xff7d9fb0);
        case Type::shifter: return juce::Colour (0xffa08cb0);
        case Type::convolution: return juce::Colour (0xff9a9488);
        case Type::utility: return juce::Colour (0xff8e9a96);
        default: return juce::Colour (0xff9a9488);
    }
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
            g.setColour (juce::Colour (0xfffaf6ee));
            g.fillEllipse (r.reduced (1.0f));
            g.setColour (over ? juce::Colour (0xffb9ab9b) : juce::Colour (0xffd7cbbe));
            g.drawEllipse (r.reduced (1.0f), 1.0f);
            juce::Path chevron;
            const auto c = r.getCentre();
            chevron.startNewSubPath (c.x - 5.0f, c.y + 2.5f);
            chevron.lineTo (c.x, c.y - 2.5f);
            chevron.lineTo (c.x + 5.0f, c.y + 2.5f);
            g.setColour (juce::Colour (0xff6f6a62));
            g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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
            g.setFont (font (10.0f, fonts::Weight::semibold, 0.06f));
            g.drawText ("+ ADD EFFECT", r, juce::Justification::centred, false);
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
        const auto g = view.route();
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
            auto icon = fxIcon (type);
            icon.applyTransform (icon.getTransformToScaleToFit (juce::Rectangle<float> (static_cast<float> (row.bounds.getX() + 8), static_cast<float> (row.bounds.getY() + 10), 18.0f, 18.0f), true));
            g.setColour (fxAccent (type).withAlpha (enabled ? 1.0f : 0.5f));
            g.strokePath (icon, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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
        s = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
        s->setColour (juce::Slider::trackColourId, juce::Colour (0xff809d93));
        s->setColour (juce::Slider::backgroundColourId, juce::Colour (0xffd9d1c5));
        s->setColour (juce::Slider::thumbColourId, juce::Colour (0xff6f8b81));
        s->setTitle (juce::String ("Card ") + juce::String::charToString (static_cast<juce::juce_wchar> ('A' + i)) + " level");
        if (host.apvts().getParameter (levelIds[i]) != nullptr)
            levelAttachments[static_cast<std::size_t> (i)] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (host.apvts(), levelIds[i], *s);
        s->onValueChange = [this] { repaint (mixerArea); };
        addAndMakeVisible (*s);
    }

    // The footer's keyboard-free actions.
    const char* labels[4] { "\xe2\x86\x90", "\xe2\x86\x92", "Parallel", "Series" };
    const char* tips[4] { "Move the selected effect left", "Move the selected effect right", "Stack the selected effect in parallel with the next one",
                          "Take the selected effect out of its parallel pair" };
    for (int i = 0; i < 4; ++i)
    {
        auto& b = footerButtons[static_cast<std::size_t> (i)];
        b = std::make_unique<juce::TextButton> (juce::String::fromUTF8 (labels[i]));
        b->setTooltip (tips[i]);
        b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xfffaf5ed));
        b->setColour (juce::TextButton::textColourOffId, juce::Colour (0xff665e54));
        b->setColour (juce::ComboBox::outlineColourId, juce::Colour (0xffd9cfc2));
        addAndMakeVisible (*b);
    }
    footerButtons[0]->onClick = [this] {
        if (selected)
            if (auto g = route(); fx::nudgeEffect (g, *selected, -1))
                apply (g, "Move effect left");
    };
    footerButtons[1]->onClick = [this] {
        if (selected)
            if (auto g = route(); fx::nudgeEffect (g, *selected, 1))
                apply (g, "Move effect right");
    };
    footerButtons[2]->onClick = [this] {
        if (! selected)
            return;
        const auto o = order();
        const auto it = std::find (o.begin(), o.end(), *selected);
        if (it != o.end() && it + 1 != o.end())
            stackOn (*selected, *(it + 1));
    };
    footerButtons[3]->onClick = [this] {
        if (selected)
            if (auto g = route(); fx::unstackEffect (g, *selected))
                apply (g, "Effect to series");
    };

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

fx::Graph FxRoutingView::route() const { return engine.fxRoute(); }

bool FxRoutingView::apply (const fx::Graph& next, const juce::String& what)
{
    auto now = route();
    if (next == now)
        return false;
    auto& undo = host.undo();
    undo.beginNewTransaction (what);
    const bool done = undo.perform (new RouteEdit (engine, now, next), what);
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
    auto g = route();
    // A free slot of its pool.
    std::optional<InstanceId> id;
    for (int s = 0; s < fx::capacity[static_cast<std::size_t> (type)] && ! id; ++s)
        if (! fx::contains (g, { type, s }))
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
    const auto it = std::find_if (placed.begin(), placed.end(), [&id] (const Tile& t) { return t.id == *id; });
    if (it == placed.end())
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
    revealTile (target);
    quick->setSize (size.getWidth(), size.getHeight());
    placeQuick();
    addAndMakeVisible (*quick);
    quick->toFront (false);
}

void FxRoutingView::revealTile (InstanceId id)
{
    // A tile scrolled out of sight (a long chain) pans into view (interface only).
    const auto it = std::find_if (placed.begin(), placed.end(), [&id] (const Tile& t) { return t.id == id; });
    if (it == placed.end())
        return;
    const int left = chainArea().getX() + 12, right = chainClipRight() - 12;
    int delta = 0;
    if (it->bounds.getRight() > right)
        delta = it->bounds.getRight() - right;
    else if (it->bounds.getX() < left)
        delta = it->bounds.getX() - left;
    if (delta == 0)
        return;
    scrollX += delta;
    layoutChain();
    repaint();
}

void FxRoutingView::placeQuick()
{
    if (quick == nullptr || ! quickId)
        return;
    const auto it = std::find_if (placed.begin(), placed.end(), [this] (const Tile& t) { return t.id == *quickId; });
    if (it == placed.end())
    {
        showQuick (std::nullopt);   // (its effect left the route)
        return;
    }
    // Anchored under the tile; above it when there is no room; beside it when neither fits
    // (never over the header's controls). Clamped to the view's edges.
    const auto tile = it->bounds;
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
    if (id)
        revealTile (*id);
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
    headerArea = inner.removeFromTop (31);
    auto header = headerArea;
    close->setBounds (header.removeFromRight (23).withSizeKeepingCentre (23, 23));
    header.removeFromRight (12);
    gear->setBounds (header.removeFromRight (24).withSizeKeepingCentre (24, 24));
    header.removeFromRight (12);
    const int statusWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (10.0f), statusText())) + 30;
    header.removeFromRight (statusWidth);
    add->setBounds (header.removeFromRight (96).withSizeKeepingCentre (96, 24));
    inner.removeFromTop (16);
    // The mixer: at most a third (the approved 26.8 %).
    mixerArea = inner.removeFromLeft (juce::roundToInt (0.268f * static_cast<float> (inner.getWidth())));
    zoneArea = inner.withTrimmedLeft (39);
    footerArea = zoneArea.removeFromBottom (38);
    auto rows = mixerArea.withTrimmedRight (18).withTrimmedTop (22);
    for (auto& s : levels)
    {
        auto row = rows.removeFromTop (70);
        s->setBounds (row.withTrimmedLeft (124).withTrimmedRight (40).withSizeKeepingCentre (row.getWidth() - 164, 18));
    }
    auto footer = footerArea.withTrimmedTop (9);
    for (int i = 3; i >= 0; --i)
    {
        const int w = i >= 2 ? 64 : 30;
        footerButtons[static_cast<std::size_t> (i)]->setBounds (footer.removeFromRight (w).withSizeKeepingCentre (w, 25));
        footer.removeFromRight (6);
    }
    if (library != nullptr && library->isVisible())
        showLibrary (true);
    layoutChain();
}

int FxRoutingView::chainClipRight() const noexcept
{
    return outputBox.getX() - 10;
}

juce::Rectangle<int> FxRoutingView::chainArea() const
{
    return zoneArea.withTrimmedTop (24).withTrimmedBottom (8);
}

void FxRoutingView::layoutChain()
{
    placed.clear();
    gapList.clear();
    groups.clear();
    const auto g = route();
    const auto area = chainArea();
    const int centreY = area.getCentreY();

    // Sizes: a tile 129 x 74 (68 stacked); a group: its lanes stacked, 16 px of branch each side.
    std::function<juce::Point<int> (const std::vector<fx::Node>&, bool)> chainSize;
    std::function<juce::Point<int> (const fx::Node&, bool)> nodeSize = [&] (const fx::Node& n, bool stacked) -> juce::Point<int> {
        if (n.kind == fx::Node::Kind::effect)
            return { tileW, stacked ? stackedH : tileH };
        int w = 0, h = 0;
        for (const auto& lane : n.lanes)
        {
            const auto s = chainSize (lane, true);
            w = std::max (w, s.x);
            h += std::max (s.y, stackedH) + stackGap;
        }
        return { std::max (w, tileW) + 2 * branch, h - stackGap };
    };
    chainSize = [&] (const std::vector<fx::Node>& chain, bool stacked) -> juce::Point<int> {
        int w = 0, h = 0;
        for (std::size_t i = 0; i < chain.size(); ++i)
        {
            const auto s = nodeSize (chain[i], stacked);
            w += s.x + (i > 0 ? gapW : 0);
            h = std::max (h, s.y);
        }
        return { w, h };
    };

    const auto total = chainSize (g.chain, false);
    outputBox = juce::Rectangle<int> (62, 26).withCentre ({ area.getRight() - 31, centreY });
    const int lead = 28, available = outputBox.getX() - 22 - (area.getX() + lead);
    contentWidth = total.x;
    scrollX = juce::jlimit (0, std::max (0, contentWidth - available), scrollX);
    const int startX = (contentWidth <= available ? area.getX() + lead + (available - contentWidth) / 2 : area.getX() + lead - scrollX);
    wireFrom = { mixerArea.getRight() + 4, centreY - 1, 2, 2 };

    std::function<void (const std::vector<fx::Node>&, const fx::ChainPath&, int, int, bool)> place
        = [&] (const std::vector<fx::Node>& chain, const fx::ChainPath& path, int x, int cy, bool stacked) {
              const int gapH = stacked ? stackedH : 120;
              gapList.push_back ({ path, 0, juce::Rectangle<int> (x - gapW + 4, cy - gapH / 2, gapW - 8, gapH) });
              for (int i = 0; i < static_cast<int> (chain.size()); ++i)
              {
                  const auto& n = chain[static_cast<std::size_t> (i)];
                  const auto s = nodeSize (n, stacked);
                  if (n.kind == fx::Node::Kind::effect)
                  {
                      Tile t;
                      t.id = n.fx;
                      t.bounds = juce::Rectangle<int> (x, cy - s.y / 2, s.x, s.y);
                      t.light = juce::Rectangle<int> (12, 12).withCentre ({ t.bounds.getRight() - 13, t.bounds.getY() + 13 });
                      t.bypassed = fx::isLegacy (n.fx.type) ? n.bypassed : ! fx::isOn (fx::Settings {}, n.fx);
                      if (! fx::isLegacy (n.fx.type))
                      {
                          const auto param = juce::String ("analog.fx.") + fx::typeKey (n.fx.type) + "." + juce::String (n.fx.slot + 1) + ".on";
                          t.bypassed = host.apvts().getParameter (param) != nullptr && host.parameterValue (param) < 0.5f;
                      }
                      t.stacked = stacked;
                      placed.push_back (t);
                  }
                  else
                  {
                      Group group;
                      group.bounds = { x, cy - s.y / 2, s.x, s.y };
                      group.split = n.kind == fx::Node::Kind::split;
                      int y = group.bounds.getY();
                      for (int lane = 0; lane < static_cast<int> (n.lanes.size()); ++lane)
                      {
                          const auto ls = chainSize (n.lanes[static_cast<std::size_t> (lane)], true);
                          const int h = std::max (ls.y, stackedH);
                          const int laneCy = y + h / 2;
                          group.laneCentres.push_back (laneCy);
                          auto lanePath = path;
                          lanePath.push_back ({ i, lane });
                          place (n.lanes[static_cast<std::size_t> (lane)], lanePath, x + branch + (s.x - 2 * branch - ls.x) / 2, laneCy, true);
                          y += h + stackGap;
                      }
                      groups.push_back (group);
                  }
                  x += s.x;
                  gapList.push_back ({ path, i + 1, juce::Rectangle<int> (x + 4, cy - gapH / 2, gapW - 8, gapH) });
                  x += gapW;
              }
          };
    place (g.chain, {}, startX, centreY, false);
    // The top chain's first and last gaps reach the wire's ends (drops before the first effect
    // and right before OUTPUT).
    for (auto& gap : gapList)
        if (gap.path.empty())
        {
            if (gap.index == 0)
                gap.bounds = gap.bounds.withLeft (area.getX());
            else if (gap.index == static_cast<int> (g.chain.size()))
                gap.bounds = gap.bounds.withRight (outputBox.getX() - 4);
        }
}

int FxRoutingView::tileAt (juce::Point<int> p) const
{
    if (p.x >= chainClipRight() || p.x <= mixerArea.getRight())
        return -1;   // (scrolled out of sight: not there)
    for (int i = 0; i < static_cast<int> (placed.size()); ++i)
        if (placed[static_cast<std::size_t> (i)].bounds.contains (p))
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
void FxRoutingView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0x1d3c3225));
    g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 13.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff9f5ee), r.getX(), r.getY(), juce::Colour (0xfff2ece3), r.getRight(), r.getBottom(), false));
    g.fillRoundedRectangle (r.reduced (0.5f), 13.0f);
    g.setColour (juce::Colour (0xffd3cabe));
    g.drawRoundedRectangle (r.reduced (0.5f), 13.0f, 1.0f);

    // Header: the dot and ROUTING; CLASSIC / CUSTOM and what it means; the rule under it.
    g.setColour (sage);
    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ static_cast<float> (headerArea.getX()) + 3.5f, static_cast<float> (headerArea.getCentreY()) }));
    g.setColour (ink);
    g.setFont (font (11.0f, fonts::Weight::semibold, 0.075f));
    g.drawText ("ROUTING", headerArea.withTrimmedLeft (15), juce::Justification::centredLeft, false);
    const bool custom = route().custom;
    {
        const auto statusArea = juce::Rectangle<int> (add->getRight() + 6, headerArea.getY(), gear->getX() - 12 - add->getRight() - 6, headerArea.getHeight());
        juce::AttributedString status;
        status.setJustification (juce::Justification::centredRight);
        status.append (custom ? "CUSTOM" : "CLASSIC", font (10.0f, fonts::Weight::semibold, 0.02f), custom ? warm : green);
        status.append (juce::String::fromUTF8 (custom ? " \xc2\xb7 Your signal path" : " \xc2\xb7 Original signal path"), font (10.0f), juce::Colour (0xff978b7f));
        status.draw (g, statusArea.toFloat());
    }
    g.setColour (juce::Colour (0xffe5dbcf));
    g.fillRect (juce::Rectangle<int> (headerArea.getX(), headerArea.getBottom() + 4, headerArea.getWidth(), 1));

    paintMixer (g);

    // The chain zone's titles.
    g.setColour (juce::Colour (0xff7b7168));
    g.setFont (font (10.0f, fonts::Weight::semibold, 0.1f));
    g.drawText ("FX CHAIN", zoneArea.withHeight (22), juce::Justification::centredLeft, false);
    g.setColour (juce::Colour (0xffa79b8d));
    g.setFont (font (8.0f, fonts::Weight::medium, 0.08f));
    g.drawText (custom ? "SERIES + PARALLEL PATHS" : "FX INSERTS + PARALLEL RETURNS", zoneArea.withHeight (22), juce::Justification::centredRight, false);

    // The wire: one line from the mixer through the chain to OUTPUT (tiles sit on it).
    const auto area = chainArea();
    {
        juce::Graphics::ScopedSaveState clip (g);
        // (A long chain scrolls under a clean edge before OUTPUT: OUTPUT is never covered.)
        g.reduceClipRegion (juce::Rectangle<int> (mixerArea.getRight(), area.getY() - 8, chainClipRight() - mixerArea.getRight(), area.getHeight() + 16));
        const float cy = static_cast<float> (area.getCentreY());
        g.setColour (wire);
        g.fillRect (juce::Rectangle<float> (static_cast<float> (mixerArea.getRight() + 4), cy - 0.6f, static_cast<float> (outputBox.getX() - mixerArea.getRight() - 4), 1.2f));
        // The groups' splits and joins: down / up to each lane, across, back to the line.
        for (const auto& grp : groups)
        {
            const float x0 = static_cast<float> (grp.bounds.getX()), x1 = static_cast<float> (grp.bounds.getRight());
            const float gy = static_cast<float> (grp.bounds.getCentreY());
            const float top = static_cast<float> (grp.laneCentres.front()), bottom = static_cast<float> (grp.laneCentres.back());
            juce::Path p;
            p.startNewSubPath (x0, gy);
            p.lineTo (x0 + 7.0f, gy);
            p.startNewSubPath (x0 + 7.0f, top);
            p.lineTo (x0 + 7.0f, bottom);
            for (const int lc : grp.laneCentres)
            {
                p.startNewSubPath (x0 + 7.0f, static_cast<float> (lc));
                p.lineTo (x1 - 7.0f, static_cast<float> (lc));
            }
            p.startNewSubPath (x1 - 7.0f, top);
            p.lineTo (x1 - 7.0f, bottom);
            p.startNewSubPath (x1 - 7.0f, gy);
            p.lineTo (x1, gy);
            // (the middle of the line under the group is not signal: cleared)
            g.setColour (juce::Colour (0xfff5f0e8));
            g.fillRect (juce::Rectangle<float> (x0 + 7.5f, gy - 1.0f, x1 - x0 - 15.0f, 2.0f));
            g.setColour (grp.split ? juce::Colour (0xff9aa4b8) : juce::Colour (0xff91aa9e));
            g.strokePath (p, juce::PathStrokeType (1.3f));
            g.setFont (font (8.0f, fonts::Weight::semibold, 0.08f));
            g.setColour (juce::Colour (0xff86958a));
            g.drawText (grp.split ? "SPLIT" : "PARALLEL", juce::Rectangle<float> (x0, gy - 5.0f, x1 - x0, 10.0f), juce::Justification::centred, false);
        }
        // The gaps' chevrons (the drop target, while dragging, a +).
        for (int i = 0; i < static_cast<int> (gapList.size()); ++i)
        {
            const auto& gap = gapList[static_cast<std::size_t> (i)];
            const bool target = dragging && i == dropGap;
            if (gap.bounds.getWidth() > 40 && ! target)
                continue;   // (the wire's long ends: no chevron)
            const auto c = juce::Point<float> (static_cast<float> (gap.bounds.getCentreX()), static_cast<float> (gap.bounds.getCentreY()));
            if (target)
            {
                g.setColour (juce::Colour (0x88d9e7df));
                g.fillRoundedRectangle (gap.bounds.toFloat(), 6.0f);
                g.setColour (juce::Colour (0xffc9dfd0));
                g.fillEllipse (juce::Rectangle<float> (29.0f, 29.0f).withCentre (c));
                g.setColour (juce::Colour (0xfff5f0e8));
                g.fillEllipse (juce::Rectangle<float> (19.0f, 19.0f).withCentre (c));
                g.setColour (juce::Colour (0xff426f5a));
                g.setFont (font (13.0f, fonts::Weight::bold));
                g.drawText ("+", juce::Rectangle<float> (19.0f, 19.0f).withCentre (c), juce::Justification::centred, false);
                continue;
            }
            g.setColour (juce::Colour (0xfff5f0e8));
            g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (c));
            g.setColour (juce::Colour (0xffaca095));
            g.setFont (font (11.0f));
            g.drawText (juce::String::fromUTF8 ("\xe2\x80\xba"), juce::Rectangle<float> (12.0f, 12.0f).withCentre (c).translated (0.5f, -0.5f), juce::Justification::centred, false);
        }
        for (const auto& t : placed)
            paintTile (g, t);
        if (scrollX < contentWidth - (chainClipRight() - area.getX()))
        {
            // More chain to the right: it fades into the edge (the wheel pans it).
            const auto fade = juce::Rectangle<float> (static_cast<float> (chainClipRight() - 28), static_cast<float> (area.getY() - 8), 28.0f, static_cast<float> (area.getHeight() + 16));
            g.setGradientFill (juce::ColourGradient (juce::Colour (0x00f5f0e8), fade.getX(), 0.0f, juce::Colour (0xfff5f0e8), fade.getRight(), 0.0f, false));
            g.fillRect (fade);
        }
    }
    // The wire's last stretch into OUTPUT (outside the chain's clip).
    g.setColour (wire);
    g.fillRect (juce::Rectangle<float> (static_cast<float> (chainClipRight() - 1), static_cast<float> (area.getCentreY()) - 0.6f, static_cast<float> (outputBox.getX() - chainClipRight() + 1), 1.2f));

    // OUTPUT: the small terminal at the wire's end.
    g.setColour (juce::Colour (0xfffaf6ee));
    g.fillRoundedRectangle (outputBox.toFloat(), 5.0f);
    g.setColour (juce::Colour (0xffc9bfb2));
    g.drawRoundedRectangle (outputBox.toFloat().reduced (0.5f), 5.0f, 1.0f);
    g.setColour (soft);
    g.setFont (font (8.5f, fonts::Weight::semibold, 0.08f));
    g.drawText ("OUTPUT", outputBox, juce::Justification::centred, false);

    // Footer: the path in words; what stays upstream; the actions.
    g.setColour (juce::Colour (0xffe5ddd3));
    g.fillRect (footerArea.withHeight (1));
    const auto firstButton = footerButtons[0]->getBounds();
    auto text = footerArea.withTrimmedTop (6).withRight (std::min (footerArea.getRight() - 240, firstButton.getX() - 142));   // (clear of the selection's name)
    g.setColour (juce::Colour (0xff80776d));
    g.setFont (font (10.0f));
    g.drawText (summaryText(), text.removeFromTop (16), juce::Justification::centredLeft, true);
    g.setColour (juce::Colour (0xffaea395));
    g.setFont (font (8.5f));
    g.drawText (juce::String::fromUTF8 ("VOICE FILTER \xc2\xb7 MORPH \xc2\xb7 DRIFT \xc2\xb7 CHORUS stay upstream"), text.removeFromTop (13), juce::Justification::centredLeft, true);
    g.setColour (juce::Colour (0xff9a9086));
    g.setFont (font (9.0f));
    g.drawText (selected ? juce::String (fx::typeName (selected->type)) : juce::String ("Select an effect"),
                juce::Rectangle<int> (firstButton.getX() - 130, firstButton.getY(), 122, firstButton.getHeight()), juce::Justification::centredRight, false);
}

void FxRoutingView::paintMixer (juce::Graphics& g) const
{
    g.setColour (juce::Colour (0xffd9d0c5));
    g.fillRect (juce::Rectangle<int> (mixerArea.getRight(), mixerArea.getY(), 1, mixerArea.getHeight()));
    auto area = mixerArea.withTrimmedRight (18);
    auto top = area.removeFromTop (22);
    g.setColour (juce::Colour (0xff7b7168));
    g.setFont (font (10.0f, fonts::Weight::semibold, 0.1f));
    g.drawText ("ENGINE MIXER", top, juce::Justification::centredLeft, false);
    g.setColour (juce::Colour (0xffa79b8d));
    g.setFont (font (8.0f, fonts::Weight::medium, 0.08f));
    g.drawText ("TO FX", top, juce::Justification::centredRight, false);
    const juce::Colour tiles[3] { juce::Colour (0xffc77f50), juce::Colour (0xff819aaf), juce::Colour (0xff879f8c) };
    const char* names[3] { "OSC 1", "OSC 2", "HYBRID" };
    const juce::String kinds[3] { textOf (host, params::osc1Model), textOf (host, params::osc2Model), textOf (host, params::hybridType) };
    const juce::String levelIds[3] { params::osc1Level, params::osc2Level, "layerB.level" };
    for (int i = 0; i < 3; ++i)
    {
        auto row = area.removeFromTop (70);
        if (i < 2)
        {
            g.setColour (juce::Colour (0xffe7dfd5));
            g.fillRect (row.withTop (row.getBottom() - 1));
        }
        const auto letter = juce::Rectangle<float> (30.0f, 30.0f).withCentre ({ static_cast<float> (row.getX()) + 15.0f, static_cast<float> (row.getCentreY()) });
        g.setColour (tiles[i]);
        g.fillRoundedRectangle (letter, 5.0f);
        g.setColour (juce::Colours::white);
        g.setFont (font (16.0f, fonts::Weight::medium));
        g.drawText (juce::String::charToString (static_cast<juce::juce_wchar> ('A' + i)), letter, juce::Justification::centred, false);
        g.setColour (juce::Colour (0xff3d413d));
        g.setFont (font (10.0f, fonts::Weight::semibold, 0.035f));
        g.drawText (names[i], juce::Rectangle<int> (row.getX() + 42, row.getCentreY() - 12, 80, 12), juce::Justification::centredLeft, false);
        g.setColour (juce::Colour (0xffa0968c));
        g.setFont (font (9.0f));
        g.drawText (kinds[i].toUpperCase(), juce::Rectangle<int> (row.getX() + 42, row.getCentreY() + 1, 80, 12), juce::Justification::centredLeft, true);
        g.setColour (juce::Colour (0xff687e75));
        g.setFont (font (10.0f));
        g.drawText (textOf (host, levelIds[i]), juce::Rectangle<int> (row.getRight() - 40, row.getY(), 40, row.getHeight()), juce::Justification::centredRight, false);
    }
    g.setColour (juce::Colour (0xff918779));
    g.setFont (font (9.0f));
    g.drawText (juce::String::fromUTF8 ("A + B + C \xe2\x86\x92 one shared FX path"), area.withTrimmedTop (9).withHeight (14), juce::Justification::centredLeft, false);
}

void FxRoutingView::paintTile (juce::Graphics& g, const Tile& t) const
{
    const auto accent = fxAccent (t.id.type);
    const auto r = t.bounds.toFloat();
    const float alpha = t.bypassed ? 0.55f : 1.0f;
    const bool isSelected = selected && *selected == t.id;
    const bool beingDragged = dragging && pressed && *pressed == t.id;
    juce::Graphics::ScopedSaveState state (g);
    g.setOpacity (beingDragged ? 0.36f : 1.0f);
    g.setColour (juce::Colour (0x16504639));
    g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 7.0f);
    const bool hover = hovered && *hovered == t.id;
    g.setGradientFill (juce::ColourGradient (hover ? juce::Colour (0xfffffaf3) : juce::Colour (0xfffbf8f1), 0.0f, r.getY(), hover ? juce::Colour (0xfff0e9de) : juce::Colour (0xffeee7dd), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (hover ? juce::Colour (0xffaaaea4) : juce::Colour (0xffccc3b7));
    g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
    {
        juce::Graphics::ScopedSaveState clip (g);
        juce::Path rounded;
        rounded.addRoundedRectangle (r, 7.0f);
        g.reduceClipRegion (rounded);
        g.setColour (accent.withMultipliedAlpha (alpha));
        g.fillRect (r.withWidth (3.0f));
    }
    // Icon, name, what it is set to.
    const auto iconBox = juce::Rectangle<float> (27.0f, 27.0f).withPosition (r.getX() + 11.0f, r.getCentreY() - 22.0f + (t.stacked ? 3.0f : 0.0f));
    g.setColour (accent.withAlpha (0.11f * alpha + 0.02f));
    g.fillRoundedRectangle (iconBox, 5.0f);
    auto icon = fxIcon (t.id.type);
    icon.applyTransform (icon.getTransformToScaleToFit (iconBox.reduced (5.0f), true));
    g.setColour (accent.withMultipliedAlpha (alpha));
    g.strokePath (icon, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colour (0xff3f4440).withMultipliedAlpha (alpha));
    g.setFont (font (11.0f, fonts::Weight::semibold, 0.04f));
    juce::String name = fx::typeName (t.id.type);
    if (! fx::isLegacy (t.id.type) && fx::capacity[static_cast<std::size_t> (t.id.type)] > 1)
        name << " " << (t.id.slot + 1);
    g.drawFittedText (name, juce::Rectangle<int> (static_cast<int> (iconBox.getRight()) + 8, static_cast<int> (iconBox.getY()), t.bounds.getRight() - static_cast<int> (iconBox.getRight()) - 26, 27),
                      juce::Justification::centredLeft, 1, 0.8f);
    g.setColour (juce::Colour (0xffa0988e).withMultipliedAlpha (alpha));
    g.setFont (font (9.0f));
    g.drawText (t.bypassed ? juce::String ("Bypassed") : subtitleOf (t.id), juce::Rectangle<int> (static_cast<int> (iconBox.getX()) + 2, static_cast<int> (iconBox.getBottom()) + 4, t.bounds.getWidth() - 24, 13),
                juce::Justification::centredLeft, true);
    // The bypass light (its own hit target: clicking the tile never flips the sound).
    const auto light = t.light.toFloat().withSizeKeepingCentre (7.0f, 7.0f);
    if (t.bypassed)
    {
        g.setColour (juce::Colour (0xffb8b2a9));
        g.drawEllipse (light, 1.2f);
    }
    else
    {
        g.setColour (juce::Colour (0xff6798b2).withAlpha (0.25f));
        g.fillEllipse (light.expanded (2.0f));
        g.setColour (juce::Colour (0xff6798b2));
        g.fillEllipse (light);
    }
    if (isSelected)
    {
        g.setColour (sage);
        g.drawRoundedRectangle (r.expanded (2.5f), 9.0f, 1.5f);
    }
    // While dragging over it: STACK (a drop here makes the two parallel).
    const bool target = dragging && dropTile >= 0 && placed[static_cast<std::size_t> (dropTile)].id == t.id;
    if (target)
    {
        g.setColour (juce::Colour (0xede1efe6));
        g.fillRoundedRectangle (r.reduced (1.0f), 6.0f);
        juce::Path dash;
        const float dashes[] { 4.0f, 3.0f };
        juce::Path outline;
        outline.addRoundedRectangle (r.reduced (1.0f), 6.0f);
        juce::PathStrokeType (1.5f).createDashedStroke (dash, outline, dashes, 2);
        g.setColour (juce::Colour (0xff739589));
        g.fillPath (dash);
        g.setColour (juce::Colour (0xff416a55));
        g.setFont (font (9.0f, fonts::Weight::semibold, 0.045f));
        g.drawText ("STACK IN PARALLEL", r, juce::Justification::centred, false);
    }
}

void FxRoutingView::paintOverChildren (juce::Graphics& g)
{
    // The dragged tile follows the pointer (a light copy).
    if (dragging && pressed)
    {
        Tile ghost;
        ghost.id = *pressed;
        ghost.bounds = juce::Rectangle<int> (tileW, tileH).withCentre (dragAt);
        ghost.light = juce::Rectangle<int> (12, 12).withCentre ({ ghost.bounds.getRight() - 13, ghost.bounds.getY() + 13 });
        juce::Graphics::ScopedSaveState s (g);
        g.setOpacity (0.85f);
        paintTile (g, ghost);
    }
}

//==============================================================================
void FxRoutingView::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (library != nullptr && library->isVisible() && ! library->getBounds().contains (e.getPosition()))
        showLibrary (false);
    if (quick != nullptr && tileAt (e.getPosition()) < 0)
        showQuick (std::nullopt);   // (a click elsewhere closes the quick settings)
    const int i = tileAt (e.getPosition());
    pressed.reset();
    if (i < 0)
    {
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
    setMouseCursor (i >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
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

void FxRoutingView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    // A long chain pans sideways (mixer and OUTPUT stay put).
    scrollX -= juce::roundToInt (120.0f * (std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY));
    layoutChain();
    repaint();
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
