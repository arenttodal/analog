#pragma once

#include "InstrumentHost.h"
#include "ShapingPopups.h"
#include "ShellProduct.h"
#include "UserMacroState.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace andor::analog
{

class AnalogEngine;

/**
    USER MACROS V1.1 for ANDOR ANALOG (docs/USER_MACROS_V1_1.md): the target registry (which
    controls a USER macro may move, and their names), the undoable edits, and the assignments'
    panel. Message thread only.
*/
namespace usermacros
{
    AnalogEngine& engineOf (osp::plugin::InstrumentHost& host);
    /** The same, or nullptr when the host runs another engine (a test's stand-in). */
    AnalogEngine* engineIf (osp::plugin::InstrumentHost& host);

    /** A continuous, real-time-safe parameter the engine reads every block (not a switch, a
        choice, a mode, a source or anything that changes latency or routing). */
    bool eligible (osp::plugin::InstrumentHost& host, const juce::String& parameterId);
    /** "OSC 2 LEVEL", "RESONANCE", "SPACE DECAY". */
    juce::String targetName (osp::plugin::InstrumentHost& host, const juce::String& parameterId);
    /** A target's normalised value shown in its own units ("700 Hz", "41 %"). */
    juce::String valueText (osp::plugin::InstrumentHost& host, const juce::String& parameterId, float normalised);

    /** One undoable change of the names / assignments (the whole state before and after). */
    void edit (osp::plugin::InstrumentHost& host, const std::function<void (UserMacroState&)>& change, const juce::String& name);
    /** A neutral assignment of `parameterId` to `macro`, anchored where the macro and the target
        are now (no jump). An existing one is kept. False: refused (not eligible, or eight already). */
    bool assign (osp::plugin::InstrumentHost& host, int macro, const juce::String& parameterId);
    /** The macro's position now (0..1). */
    float macroValue (osp::plugin::InstrumentHost& host, int macro);

    /** The descriptor the shell shows the USER bank with. */
    andor::shell::ProductInfo::UserMacros descriptor();
} // namespace usermacros

/**
    A USER macro's assignments: its name, then at most eight rows (target, its range in its own
    units, its state), the chosen row's LOW and HIGH (the target's value at macro 0 and 100 %),
    SWAP, EASE, BYPASS, REBASE and REMOVE, and + ADD ASSIGNMENT (the next click on a control).
    The SUB / SPACE popovers' card; every change is one undo step.
*/
class UserMacroPanel final : public osp::plugin::MiniPanel
{
public:
    UserMacroPanel (osp::plugin::InstrumentHost& host, int macro, const juce::String& select);
    ~UserMacroPanel() override;

    static constexpr int width = 380, rowHeight = 26;
    bool compact() const override { return true; }
    juce::Point<int> cardSize() const override;
    void paint (juce::Graphics&) override;
    void refreshContent() override;
    void settleNow() override { refreshContent(); }
    void mouseUp (const juce::MouseEvent&) override;

    int macro() const noexcept { return macroIndex; }
    int selected() const noexcept { return selectedRow; }
    void select (int row);
    int rowCount() const;
    juce::Rectangle<int> rowArea (int row) const;
    /** What a row shows ("RESONANCE  15 % -> 65 %", "... EFFECT REMOVED"). */
    juce::String rowText (int row) const;
    juce::Slider& lowSlider() noexcept { return low; }
    juce::Slider& highSlider() noexcept { return high; }
    juce::TextButton& addButton() noexcept { return add; }
    juce::TextButton& swapButton() noexcept { return swap; }
    juce::TextButton& removeButton() noexcept { return remove; }
    juce::TextButton& easeButton() noexcept { return ease; }
    juce::TextButton& bypassButton() noexcept { return bypass; }
    juce::TextButton& rebaseButton() noexcept { return rebase; }

protected:
    void layoutContent (juce::Rectangle<int> area) override;

private:
    const UserMacroState& state() const;
    void syncControls();
    void editSelected (const std::function<void (UserMacroAssignment&)>& change, const juce::String& name);
    void beginRangeDrag();
    void endRangeDrag();

    osp::plugin::InstrumentHost& host;
    int macroIndex;
    int selectedRow = -1;
    std::uint64_t shownRevision = 0;
    juce::Slider low, high;
    juce::TextButton add { "+ ADD ASSIGNMENT" }, swap { "SWAP" }, remove { "REMOVE" }, ease { "EASE" }, bypass { "BYPASS" }, rebase { "REBASE" };
    juce::Rectangle<int> listArea, editorArea, lowCaption, highCaption;
    std::unique_ptr<UserMacroState> dragStart;   ///< (a LOW / HIGH drag: one undo step at its end)
};

} // namespace andor::analog
