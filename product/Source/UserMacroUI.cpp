#include "UserMacroUI.h"

#include "AnalogEngine.h"
#include "AnalogParameters.h"
#include "Design.h"
#include "OspLookAndFeel.h"
#include "ShellEditor.h"
#include "ShellProcessor.h"

#include <algorithm>
#include <cmath>

namespace andor::analog
{

using namespace osp::plugin;

namespace usermacros
{
    AnalogEngine* engineIf (InstrumentHost& host)
    {
        auto* shell = dynamic_cast<andor::shell::ShellProcessor*> (&host);
        return shell != nullptr ? dynamic_cast<AnalogEngine*> (&shell->productEngine()) : nullptr;
    }

    AnalogEngine& engineOf (InstrumentHost& host)
    {
        return dynamic_cast<AnalogEngine&> (dynamic_cast<andor::shell::ShellProcessor&> (host).productEngine());
    }

    bool eligible (InstrumentHost& host, const juce::String& id)
    {
        if (id.isEmpty() || id.startsWith ("analog.userMacro."))
            return false;   // (a macro never moves a macro: no feedback)
        auto* p = host.apvts().getParameter (id);
        // Continuous only: a float the engine reads every block. Switches, choices, modes,
        // sources, routing and anything latent are other parameter kinds, or not read here.
        if (p == nullptr || dynamic_cast<juce::AudioParameterFloat*> (p) == nullptr)
            return false;
        return params::readerIndexOf (id) >= 0;
    }

    juce::String targetName (InstrumentHost& host, const juce::String& id)
    {
        if (auto* p = host.apvts().getParameter (id))
            return p->getName (40).toUpperCase();
        return id;
    }

    juce::String valueText (InstrumentHost& host, const juce::String& id, float normalised)
    {
        auto* p = host.apvts().getParameter (id);
        if (p == nullptr)
            return {};
        auto text = p->getText (std::clamp (normalised, 0.0f, 1.0f), 24).trim();
        // A raw float ("2.9999998 s"): a musician's precision (3.00 s, 12.5 dB, 440 Hz).
        const auto number = text.upToFirstOccurrenceOf (" ", false, false);
        if (number.containsOnly ("-0123456789.") && number.fromFirstOccurrenceOf (".", false, false).length() > 2)
        {
            const double v = number.getDoubleValue();
            const int decimals = std::abs (v) >= 100.0 ? 0 : (std::abs (v) >= 10.0 ? 1 : 2);
            text = juce::String (v, decimals) + text.substring (number.length());
        }
        const auto label = p->getLabel().trim();
        if (label.isNotEmpty() && ! text.endsWithIgnoreCase (label))
            text << " " << label;
        return text;
    }

    namespace
    {
        /** The whole state before and after: undo and redo restore it exactly (and the engine
            recompiles its table each time). */
        struct UserMacroEdit final : public juce::UndoableAction
        {
            UserMacroEdit (AnalogEngine& e, UserMacroState b, UserMacroState a) : engine (e), before (std::move (b)), after (std::move (a)) {}
            bool perform() override
            {
                engine.setUserMacros (after);
                return true;
            }
            bool undo() override
            {
                engine.setUserMacros (before);
                return true;
            }
            int getSizeInUnits() override { return 1; }
            AnalogEngine& engine;
            UserMacroState before, after;
        };
    }

    void edit (InstrumentHost& host, const std::function<void (UserMacroState&)>& change, const juce::String& name)
    {
        auto* found = engineIf (host);
        if (found == nullptr)
            return;   // (a stand-in engine: nothing to edit)
        auto& engine = *found;
        auto next = engine.userMacros();
        change (next);
        if (next == engine.userMacros())
            return;
        host.undo().beginNewTransaction (name);
        host.undo().perform (new UserMacroEdit (engine, engine.userMacros(), std::move (next)), name);
    }

    float macroValue (InstrumentHost& host, int macro)
    {
        if (auto* p = host.apvts().getParameter (params::userMacroId (macro)))
            return p->getValue();
        return 0.0f;
    }

    std::pair<float, float> defaultRange (float b0)
    {
        constexpr float span = 0.25f, least = 0.1f;
        const float up = std::min (1.0f, b0 + span), down = std::max (0.0f, b0 - span);
        return { b0 - down >= least ? down : up,     // LOW: below, unless the control sits at its bottom
                 up - b0 >= least ? up : down };     // HIGH: above, unless it sits at its top
    }

    bool assign (InstrumentHost& host, int macro, const juce::String& id)
    {
        if (macro < 0 || macro >= usermacro::macroCount || ! eligible (host, id) || engineIf (host) == nullptr)
            return false;
        const auto& now = engineOf (host).userMacros();
        if (now.find (macro, id) >= 0)
            return true;   // (already: its row is shown, never a duplicate)
        if (now.assignmentCount (macro) >= usermacro::mapsPerMacro)
            return false;
        UserMacroAssignment a;
        a.target = id;
        a.map.anchorMacro = macroValue (host, macro);
        a.map.anchorTarget = host.apvts().getParameter (id)->getValue();   // the target's own value now
        // A visible default range around it, towards the room there is (a quarter of the travel
        // each way). The anchor keeps it exactly where it is at the macro's position now: no jump.
        const auto range = defaultRange (a.map.anchorTarget);
        a.map.low = range.first;
        a.map.high = range.second;
        edit (host, [macro, &a] (UserMacroState& s) { s.maps[static_cast<std::size_t> (macro)].push_back (a); },
              "Assign " + targetName (host, id) + " to " + now.displayName (macro));
        return true;
    }

    namespace
    {
        constexpr float anchorEpsilon = 1.0e-6f;

        /** A map rebased on the control's value now: its offsets kept, b0 = the base, so LOW and
            HIGH are where macro 0 % and 100 % put the control (clamped to the travel). */
        void rebase (usermacro::Mapping& map, float base)
        {
            const float shift = base - map.anchorTarget;
            map.low = std::clamp (map.low + shift, 0.0f, 1.0f);
            map.high = std::clamp (map.high + shift, 0.0f, 1.0f);
            map.anchorTarget = base;
        }

        /** The one range drag in progress (message thread; one pointer). */
        struct RangeDrag
        {
            InstrumentHost* host = nullptr;
            int macro = -1;
            juce::String target;
            UserMacroState start;
            float paramStart = 0.0f;
            bool paramMoved = false;
        };
        RangeDrag& rangeDrag()
        {
            static RangeDrag drag;
            return drag;
        }
    }

    std::vector<andor::shell::ProductInfo::UserMacros::Range> ranges (InstrumentHost& host, const juce::String& id)
    {
        std::vector<andor::shell::ProductInfo::UserMacros::Range> out;
        auto* engine = engineIf (host);
        auto* p = host.apvts().getParameter (id);
        if (engine == nullptr || p == nullptr)
            return out;
        const float base = p->getValue();
        const auto& state = engine->userMacros();
        for (int m = 0; m < usermacro::macroCount; ++m)
        {
            const int i = state.find (m, id);
            if (i < 0)
                continue;
            const auto& a = state.maps[static_cast<std::size_t> (m)][static_cast<std::size_t> (i)];
            if (a.orphaned)
                continue;
            const float mv = macroValue (host, m);
            andor::shell::ProductInfo::UserMacros::Range r;
            r.macro = m;
            r.base = base;
            r.macroValue = mv;
            r.enabled = a.map.enabled;
            // This assignment alone, as the engine adds it to the control's own value (then clamps).
            auto at = [&] (double position) { return std::clamp (base + static_cast<float> (usermacro::offsetOf (a.map, position)), 0.0f, 1.0f); };
            r.atMin = at (0.0);
            r.atMax = at (1.0);
            r.now = at (mv);
            out.push_back (r);
        }
        return out;
    }

    float offsetNow (InstrumentHost& host, const juce::String& id)
    {
        auto* engine = engineIf (host);
        if (engine == nullptr)
            return 0.0f;
        double sum = 0.0;
        const auto& state = engine->userMacros();
        for (int m = 0; m < usermacro::macroCount; ++m)
            if (const int i = state.find (m, id); i >= 0)
                if (const auto& a = state.maps[static_cast<std::size_t> (m)][static_cast<std::size_t> (i)]; ! a.orphaned)
                    sum += usermacro::offsetOf (a.map, macroValue (host, m));
        return static_cast<float> (sum);
    }

    void editRange (InstrumentHost& host, int macro, const juce::String& id, int end, float position, int phase)
    {
        auto* engine = engineIf (host);
        auto* p = host.apvts().getParameter (id);
        if (engine == nullptr || p == nullptr || macro < 0 || macro >= usermacro::macroCount)
            return;
        auto& drag = rangeDrag();
        if (phase == 0)
        {
            drag = {};
            drag.host = &host;
            drag.macro = macro;
            drag.target = id;
            drag.start = engine->userMacros();
            drag.paramStart = p->getValue();
            // One undo step for the whole drag: the range, and the control's own value when the
            // dragged end is the anchor's (below). (Changes not yet recorded go first, in their own.)
            host.apvts().copyState();
            host.undo().beginNewTransaction ("Macro range");
            auto next = drag.start;
            if (const int i = next.find (macro, id); i >= 0)
            {
                rebase (next.maps[static_cast<std::size_t> (macro)][static_cast<std::size_t> (i)].map, p->getValue());
                engine->setUserMacros (next);
            }
            return;
        }
        if (drag.host != &host || drag.macro != macro || drag.target != id)
            return;
        if (phase == 1)
        {
            auto next = engine->userMacros();
            const int i = next.find (macro, id);
            if (i < 0)
                return;
            auto& map = next.maps[static_cast<std::size_t> (macro)][static_cast<std::size_t> (i)].map;
            position = std::clamp (position, 0.0f, 1.0f);
            const float a = map.anchorMacro;
            // The end the anchor sits on is the control's own value there (an anchored map adds
            // nothing at its anchor): that end moves the knob itself, the other end stays put.
            const bool anchoredEnd = end == 0 ? a <= anchorEpsilon : a >= 1.0f - anchorEpsilon;
            if (anchoredEnd)
            {
                if (! drag.paramMoved)
                {
                    p->beginChangeGesture();
                    drag.paramMoved = true;
                }
                p->setValueNotifyingHost (position);
                map.anchorTarget = position;
                (end == 0 ? map.low : map.high) = position;
            }
            else
                (end == 0 ? map.low : map.high) = position;
            engine->setUserMacros (next);
            return;
        }
        if (phase == 2)
        {
            const auto after = engine->userMacros();
            if (drag.paramMoved)
            {
                p->endChangeGesture();
                host.apvts().copyState();   // (the parameter's change joins this step)
            }
            engine->setUserMacros (drag.start);
            if (! (after == drag.start))
                host.undo().perform (new UserMacroEdit (*engine, drag.start, after), "Macro range");
            else
                engine->setUserMacros (after);
            drag = {};
            return;
        }
        // Cancelled: as it was.
        engine->setUserMacros (drag.start);
        if (drag.paramMoved)
        {
            p->setValueNotifyingHost (drag.paramStart);
            p->endChangeGesture();
        }
        drag = {};
    }

    void resetEnd (InstrumentHost& host, int macro, const juce::String& id, int end)
    {
        auto* p = host.apvts().getParameter (id);
        if (p == nullptr)
            return;
        const float base = p->getValue();
        edit (host, [macro, &id, end, base] (UserMacroState& s) {
            if (const int i = s.find (macro, id); i >= 0)
            {
                auto& map = s.maps[static_cast<std::size_t> (macro)][static_cast<std::size_t> (i)].map;
                rebase (map, base);
                (end == 0 ? map.low : map.high) = base;
            }
        }, "Reset macro range end");
    }

    void resetRange (InstrumentHost& host, int macro, const juce::String& id)
    {
        auto* p = host.apvts().getParameter (id);
        if (p == nullptr)
            return;
        const float base = p->getValue();
        edit (host, [macro, &id, base] (UserMacroState& s) {
            if (const int i = s.find (macro, id); i >= 0)
            {
                auto& map = s.maps[static_cast<std::size_t> (macro)][static_cast<std::size_t> (i)].map;
                map.anchorTarget = base;
                const auto range = defaultRange (base);
                map.low = range.first;
                map.high = range.second;
            }
        }, "Reset macro range");
    }

    void unassign (InstrumentHost& host, int macro, const juce::String& id)
    {
        if (engineIf (host) == nullptr || macro < 0 || macro >= usermacro::macroCount)
            return;
        edit (host, [macro, &id] (UserMacroState& s) {
            auto& list = s.maps[static_cast<std::size_t> (macro)];
            list.erase (std::remove_if (list.begin(), list.end(), [&id] (const UserMacroAssignment& a) { return a.target == id; }), list.end());
        }, "Remove " + targetName (host, id) + " from " + engineOf (host).userMacros().displayName (macro));
    }

    void unassignAll (InstrumentHost& host, const juce::String& id)
    {
        if (engineIf (host) == nullptr)
            return;
        edit (host, [&id] (UserMacroState& s) {
            for (auto& list : s.maps)
                list.erase (std::remove_if (list.begin(), list.end(), [&id] (const UserMacroAssignment& a) { return a.target == id; }), list.end());
        }, "Remove macro assignments");
    }

    andor::shell::ProductInfo::UserMacros descriptor()
    {
        andor::shell::ProductInfo::UserMacros d;
        for (int m = 0; m < usermacro::macroCount; ++m)
            d.parameterIds[static_cast<std::size_t> (m)] = params::userMacroId (m);
        // (Each answers neutrally for a stand-in engine - the shell's tests run ANALOG's product
        // on one: the default names, nothing assigned, no panel.)
        d.name = [] (InstrumentHost& host, int m) {
            auto* engine = engineIf (host);
            return engine != nullptr ? engine->userMacros().displayName (m) : UserMacroState::defaultName (m);
        };
        d.rename = [] (InstrumentHost& host, int m, const juce::String& typed) {
            const auto clean = UserMacroState::sanitisedName (typed);
            // (the default name again: no name of its own)
            const auto stored = clean.equalsIgnoreCase (UserMacroState::defaultName (m)) ? juce::String() : clean;
            edit (host, [m, stored] (UserMacroState& s) { s.names[static_cast<std::size_t> (m)] = stored; }, "Rename user macro");
        };
        d.activeCount = [] (InstrumentHost& host, int m) {
            auto* engine = engineIf (host);
            return engine != nullptr ? engine->userMacros().activeCount (m) : 0;
        };
        d.targets = [] (InstrumentHost& host, int m) {
            juce::StringArray ids;
            auto* engine = engineIf (host);
            if (engine == nullptr)
                return ids;
            for (const auto& a : engine->userMacros().maps[static_cast<std::size_t> (std::clamp (m, 0, usermacro::macroCount - 1))])
                if (! a.orphaned)
                    ids.add (a.target);
            return ids;
        };
        d.eligible = [] (InstrumentHost& host, const juce::String& id) { return eligible (host, id); };
        d.assign = [] (InstrumentHost& host, int m, const juce::String& id) { return assign (host, m, id); };
        d.clear = [] (InstrumentHost& host, int m) {
            edit (host, [m] (UserMacroState& s) { s.maps[static_cast<std::size_t> (m)].clear(); }, "Clear assignments");
        };
        d.panel = [] (InstrumentHost& host, int m, const juce::String& select) -> std::unique_ptr<MiniPanel> {
            if (engineIf (host) == nullptr)
                return nullptr;
            return std::make_unique<UserMacroPanel> (host, m, select);
        };
        d.targetName = [] (InstrumentHost& host, const juce::String& id) { return targetName (host, id); };
        d.ranges = [] (InstrumentHost& host, const juce::String& id) { return ranges (host, id); };
        d.offsetNow = [] (InstrumentHost& host, const juce::String& id) { return offsetNow (host, id); };
        d.editRange = [] (InstrumentHost& host, int m, const juce::String& id, int end, float position, int phase) { editRange (host, m, id, end, position, phase); };
        d.resetEnd = [] (InstrumentHost& host, int m, const juce::String& id, int end) { resetEnd (host, m, id, end); };
        d.resetRange = [] (InstrumentHost& host, int m, const juce::String& id) { resetRange (host, m, id); };
        d.unassign = [] (InstrumentHost& host, int m, const juce::String& id) { unassign (host, m, id); };
        d.unassignAll = [] (InstrumentHost& host, const juce::String& id) { unassignAll (host, id); };
        d.revision = [] (InstrumentHost& host) {
            auto* engine = engineIf (host);
            return engine != nullptr ? engine->userMacroRevision() : std::uint64_t { 0 };
        };
        return d;
    }
} // namespace usermacros

//==============================================================================
UserMacroPanel::UserMacroPanel (InstrumentHost& h, int m, const juce::String& selectTarget)
    : MiniPanel (usermacros::engineOf (h).userMacros().displayName (m), "MACRO " + juce::String (m + 1)), host (h), macroIndex (m)
{
    const auto colour = design::colour::macro (macroIndex);
    for (auto* s : { &low, &high })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s->setRange (0.0, 1.0, 0.0);
        s->setColour (juce::Slider::trackColourId, colour.withAlpha (0.7f));
        s->setColour (juce::Slider::thumbColourId, colour);
        s->setColour (juce::Slider::backgroundColourId, palette::recessed);
        s->onDragStart = [this] { beginRangeDrag(); };
        s->onDragEnd = [this] { endRangeDrag(); };
        addAndMakeVisible (*s);
    }
    low.setTitle ("Low (the target at macro 0 %)");
    high.setTitle ("High (the target at macro 100 %)");
    low.onValueChange = [this] {
        if (dragStart == nullptr)
            editSelected ([v = static_cast<float> (low.getValue())] (UserMacroAssignment& a) { a.map.low = v; }, "Assignment range");
        else
            editSelected ([v = static_cast<float> (low.getValue())] (UserMacroAssignment& a) { a.map.low = v; }, {});
    };
    high.onValueChange = [this] {
        if (dragStart == nullptr)
            editSelected ([v = static_cast<float> (high.getValue())] (UserMacroAssignment& a) { a.map.high = v; }, "Assignment range");
        else
            editSelected ([v = static_cast<float> (high.getValue())] (UserMacroAssignment& a) { a.map.high = v; }, {});
    };
    swap.setTooltip ("Swap LOW and HIGH (the other direction)");
    swap.onClick = [this] { editSelected ([] (UserMacroAssignment& a) { std::swap (a.map.low, a.map.high); }, "Swap assignment"); };
    ease.setTooltip ("A gentler curve near the anchor");
    ease.setClickingTogglesState (false);
    ease.onClick = [this] {
        editSelected ([] (UserMacroAssignment& a) { a.map.curve = a.map.curve == usermacro::Curve::ease ? usermacro::Curve::linear : usermacro::Curve::ease; }, "Assignment curve");
    };
    bypass.setTooltip ("Silence this assignment (kept)");
    bypass.onClick = [this] { editSelected ([] (UserMacroAssignment& a) { a.map.enabled = ! a.map.enabled; }, "Bypass assignment"); };
    rebase.setTooltip ("Rebase at current: the target keeps the sound it has now, and the macro's range starts from here");
    rebase.onClick = [this] {
        // The sound now becomes the target's own value (one gesture on it), the assignment
        // anchored here: nothing jumps, the range keeps its ends.
        if (selectedRow < 0)
            return;
        const auto& a = state().maps[static_cast<std::size_t> (macroIndex)][static_cast<std::size_t> (selectedRow)];
        auto* p = host.apvts().getParameter (a.target);
        if (p == nullptr)
            return;
        const float m = usermacros::macroValue (host, macroIndex);
        const float now = std::clamp (p->getValue() + static_cast<float> (usermacro::offsetOf (a.map, m)), 0.0f, 1.0f);
        host.undo().beginNewTransaction ("Rebase assignment");
        p->beginChangeGesture();
        p->setValueNotifyingHost (now);
        p->endChangeGesture();
        auto next = state();
        auto& b = next.maps[static_cast<std::size_t> (macroIndex)][static_cast<std::size_t> (selectedRow)];
        b.map.anchorMacro = m;
        b.map.anchorTarget = now;
        usermacros::edit (host, [&next] (UserMacroState& s) { s = next; }, "Rebase assignment");
    };
    remove.setTooltip ("Remove this assignment");
    remove.onClick = [this] {
        if (selectedRow < 0)
            return;
        const int row = selectedRow;
        usermacros::edit (host, [this, row] (UserMacroState& s) {
            auto& list = s.maps[static_cast<std::size_t> (macroIndex)];
            if (row < static_cast<int> (list.size()))
                list.erase (list.begin() + row);
        }, "Remove assignment");
        selectedRow = std::min (row, rowCount() - 1);
        refreshContent();
    };
    add.setTooltip ("Click a control to assign it (Esc cancels)");
    add.onClick = [this] {
        if (auto* editor = findParentComponentOfClass<andor::shell::ShellEditor>())
            editor->beginUserMacroPick (macroIndex);
    };
    for (auto* b : { &swap, &ease, &bypass, &rebase, &remove, &add })
        addAndMakeVisible (*b);
    const int found = state().find (macroIndex, selectTarget);
    selectedRow = found >= 0 ? found : (rowCount() > 0 ? 0 : -1);
    shownRevision = usermacros::engineOf (host).userMacroRevision();
    syncControls();
}

UserMacroPanel::~UserMacroPanel()
{
    if (dragStart != nullptr)
        endRangeDrag();   // (a panel closed mid-drag: the drag's one undo step)
}

const UserMacroState& UserMacroPanel::state() const
{
    return usermacros::engineOf (host).userMacros();
}

int UserMacroPanel::rowCount() const
{
    return state().assignmentCount (macroIndex);
}

juce::Point<int> UserMacroPanel::cardSize() const
{
    // Header 32, the rows (one line when empty), the chosen row's editor, + ADD, gaps.
    const int rows = std::max (1, rowCount());
    const int editor = selectedRow >= 0 ? 8 + 74 : 0;
    return { width, 32 + rows * rowHeight + editor + 10 + 26 + 12 };
}

juce::Rectangle<int> UserMacroPanel::rowArea (int row) const
{
    return { listArea.getX(), listArea.getY() + row * rowHeight, listArea.getWidth(), rowHeight };
}

juce::String UserMacroPanel::rowText (int row) const
{
    const auto& list = state().maps[static_cast<std::size_t> (macroIndex)];
    if (row < 0 || row >= static_cast<int> (list.size()))
        return {};
    const auto& a = list[static_cast<std::size_t> (row)];
    juce::String t = usermacros::targetName (host, a.target) + "  ";
    if (a.orphaned)
        return t + (a.reason.isNotEmpty() ? a.reason : juce::String ("EFFECT REMOVED")) + juce::String::fromUTF8 (" \xc2\xb7 Reconnect");
    t << usermacros::valueText (host, a.target, a.map.low) << juce::String::fromUTF8 (" \xe2\x86\x92 ") << usermacros::valueText (host, a.target, a.map.high);
    if (! a.map.enabled)
        t << "  BYPASSED";
    return t;
}

void UserMacroPanel::select (int row)
{
    const int before = selectedRow;
    selectedRow = rowCount() > 0 ? std::clamp (row, 0, rowCount() - 1) : -1;
    syncControls();
    if ((before < 0) != (selectedRow < 0) && onSizeChanged != nullptr)
        onSizeChanged();
    resized();
    repaint();
}

void UserMacroPanel::syncControls()
{
    const bool has = selectedRow >= 0 && selectedRow < rowCount();
    for (auto* c : std::initializer_list<juce::Component*> { &low, &high, &swap, &ease, &bypass, &rebase, &remove })
        c->setVisible (has);
    add.setEnabled (rowCount() < usermacro::mapsPerMacro);
    if (! has)
        return;
    const auto& a = state().maps[static_cast<std::size_t> (macroIndex)][static_cast<std::size_t> (selectedRow)];
    if (dragStart == nullptr)
    {
        low.setValue (a.map.low, juce::dontSendNotification);
        high.setValue (a.map.high, juce::dontSendNotification);
    }
    ease.setToggleState (a.map.curve == usermacro::Curve::ease, juce::dontSendNotification);
    bypass.setToggleState (! a.map.enabled, juce::dontSendNotification);
    for (auto* c : std::initializer_list<juce::Component*> { &low, &high, &swap, &ease, &rebase })
        c->setEnabled (! a.orphaned);
}

void UserMacroPanel::editSelected (const std::function<void (UserMacroAssignment&)>& change, const juce::String& name)
{
    if (selectedRow < 0 || selectedRow >= rowCount())
        return;
    const int row = selectedRow;
    auto apply = [this, row, &change] (UserMacroState& s) {
        auto& list = s.maps[static_cast<std::size_t> (macroIndex)];
        if (row < static_cast<int> (list.size()))
            change (list[static_cast<std::size_t> (row)]);
    };
    if (name.isEmpty())
    {
        // (during a LOW / HIGH drag: live, the drag's one undo step at its end)
        auto next = state();
        apply (next);
        usermacros::engineOf (host).setUserMacros (next);
    }
    else
        usermacros::edit (host, apply, name);
    syncControls();
    repaint();
}

void UserMacroPanel::beginRangeDrag()
{
    dragStart = std::make_unique<UserMacroState> (state());
}

void UserMacroPanel::endRangeDrag()
{
    if (dragStart == nullptr)
        return;
    auto before = std::move (*dragStart);
    dragStart.reset();
    auto after = state();
    if (after == before)
        return;
    // One undo step for the whole drag: back to where it started, then the drag's end.
    auto& engine = usermacros::engineOf (host);
    engine.setUserMacros (before);
    usermacros::edit (host, [&after] (UserMacroState& s) { s = after; }, "Assignment range");
}

void UserMacroPanel::refreshContent()
{
    const auto revision = usermacros::engineOf (host).userMacroRevision();
    if (revision == shownRevision)
        return;
    shownRevision = revision;
    const auto oldSize = cardSize();
    title = state().displayName (macroIndex);
    if (selectedRow >= rowCount())
        selectedRow = rowCount() - 1;
    if (selectedRow < 0 && rowCount() > 0)
        selectedRow = 0;
    syncControls();
    if (cardSize() != oldSize && onSizeChanged != nullptr)
        onSizeChanged();
    resized();
    repaint();
}

void UserMacroPanel::layoutContent (juce::Rectangle<int> area)
{
    listArea = area.removeFromTop (std::max (1, rowCount()) * rowHeight);
    if (selectedRow >= 0)
    {
        area.removeFromTop (8);
        editorArea = area.removeFromTop (74);
        auto e = editorArea.reduced (8, 6);
        auto lowRow = e.removeFromTop (20);
        lowCaption = lowRow.removeFromLeft (150);
        low.setBounds (lowRow);
        e.removeFromTop (2);
        auto highRow = e.removeFromTop (20);
        highCaption = highRow.removeFromLeft (150);
        high.setBounds (highRow);
        e.removeFromTop (6);
        auto keys = e.removeFromTop (20);
        const int w = keys.getWidth() / 5;
        for (auto* b : { &swap, &ease, &bypass, &rebase, &remove })
            b->setBounds (keys.removeFromLeft (w).reduced (2, 0));
    }
    else
        editorArea = {};
    area.removeFromTop (10);
    add.setBounds (area.removeFromTop (26).withSizeKeepingCentre (180, 24));
}

void UserMacroPanel::paint (juce::Graphics& g)
{
    MiniPanel::paint (g);
    const auto colour = design::colour::macro (macroIndex);
    g.setFont (type::popupLabel (11.0f));
    if (rowCount() == 0)
    {
        g.setColour (palette::textDim);
        g.drawText ("No assignments yet", listArea, juce::Justification::centred, false);
    }
    for (int r = 0; r < rowCount(); ++r)
    {
        const auto area = rowArea (r);
        const auto& a = state().maps[static_cast<std::size_t> (macroIndex)][static_cast<std::size_t> (r)];
        if (r == selectedRow)
        {
            g.setColour (colour.withAlpha (0.12f));
            g.fillRoundedRectangle (area.toFloat().reduced (2.0f, 1.0f), 4.0f);
        }
        g.setColour (a.orphaned || ! a.map.enabled ? palette::textDim : palette::text);
        g.drawText (usermacros::targetName (host, a.target), area.withTrimmedLeft (10).withWidth (area.getWidth() / 2), juce::Justification::centredLeft, true);
        juce::String right;
        if (a.orphaned)
            right = (a.reason.isNotEmpty() ? a.reason : juce::String ("EFFECT REMOVED")) + juce::String::fromUTF8 (" \xc2\xb7 Reconnect");
        else
        {
            right << usermacros::valueText (host, a.target, a.map.low) << juce::String::fromUTF8 (" \xe2\x86\x92 ") << usermacros::valueText (host, a.target, a.map.high);
            if (! a.map.enabled)
                right << "  (BYPASSED)";
        }
        g.drawText (right, area.withTrimmedRight (10).withTrimmedLeft (area.getWidth() / 2), juce::Justification::centredRight, true);
        if (r > 0)
        {
            g.setColour (palette::hairline.withAlpha (0.6f));
            g.fillRect (area.getX() + 8, area.getY(), area.getWidth() - 16, 1);
        }
    }
    if (selectedRow >= 0 && selectedRow < rowCount())
    {
        const auto& a = state().maps[static_cast<std::size_t> (macroIndex)][static_cast<std::size_t> (selectedRow)];
        g.setColour (palette::recessed.withAlpha (0.5f));
        g.fillRoundedRectangle (editorArea.toFloat(), 5.0f);
        g.setColour (palette::textDim);
        g.setFont (type::popupLabel (10.0f));
        g.drawText ("LOW  " + usermacros::valueText (host, a.target, a.map.low), lowCaption.withTrimmedLeft (4), juce::Justification::centredLeft, true);
        g.drawText ("HIGH  " + usermacros::valueText (host, a.target, a.map.high), highCaption.withTrimmedLeft (4), juce::Justification::centredLeft, true);
    }
}

void UserMacroPanel::mouseUp (const juce::MouseEvent& e)
{
    if (listArea.contains (e.getPosition()))
    {
        const int row = (e.getPosition().y - listArea.getY()) / rowHeight;
        if (row < rowCount())
            select (row);
        return;
    }
    MiniPanel::mouseUp (e);
}

} // namespace andor::analog
