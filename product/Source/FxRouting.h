#pragma once

#include "fx/FxGraph.h"
#include "InstrumentHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace andor::analog
{

class AnalogEngine;

/**
    FX ROUTING (docs/FX_ROUTING_IMPLEMENTATION.md): the view that takes the source cards' place
    when the header's routing icon is on (ShellProduct::sourceAreaView). Interface only: opening
    it, selecting, hovering or dragging never changes the sound; a drop, a menu choice or a key
    that edits the route does (and makes it CUSTOM), as one undoable step.

    Left, the ENGINE MIXER (a third of the width at most: the cards' levels, the path they take);
    then one continuous wire from the mixer through the chain to a small OUTPUT. Effects are
    compact tiles (icon, name, what it is set to, its bypass light). A tile dropped in a gap
    goes there in series; dropped on another tile, the two are stacked in parallel (split above
    and below, joined after). The gear holds Restore original; + ADD EFFECT opens the library.
*/
class FxRoutingView final : public juce::Component, private juce::Timer
{
public:
    explicit FxRoutingView (osp::plugin::InstrumentHost& host);
    ~FxRoutingView() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    // What is drawn (tests and screenshots).
    struct Tile
    {
        fx::InstanceId id;
        juce::Rectangle<int> bounds, light;
        bool bypassed = false, stacked = false;
    };
    const std::vector<Tile>& tiles() const noexcept { return placed; }
    struct Gap
    {
        fx::ChainPath path;
        int index = 0;
        juce::Rectangle<int> bounds;
    };
    const std::vector<Gap>& gaps() const noexcept { return gapList; }
    juce::Rectangle<int> mixerBounds() const noexcept { return mixerArea; }
    juce::Rectangle<int> outputBounds() const noexcept { return outputBox; }
    juce::Rectangle<int> wireStart() const noexcept { return wireFrom; }
    std::optional<fx::InstanceId> selection() const noexcept { return selected; }
    void select (std::optional<fx::InstanceId> id);
    juce::String statusText() const;
    juce::String summaryText() const;

    // Edits (as the gestures make them): each one undoable, each makes the route CUSTOM.
    bool moveTo (fx::InstanceId id, const fx::ChainPath& path, int index);
    bool stackOn (fx::InstanceId id, fx::InstanceId target);
    bool toggleBypass (fx::InstanceId id);
    bool restoreOriginal();
    bool insertNew (fx::Type type);
    bool remove (fx::InstanceId id);

    juce::PopupMenu settingsMenu();
    juce::Component& settingsButton() noexcept;
    juce::Component& addEffectButton() noexcept;
    /** The library popover (shown: + ADD EFFECT). */
    juce::Component* libraryPopover() const noexcept;
    void showLibrary (bool shown);
    /** Opens an effect's editor (the original four: their racks / DRIVE's popover). */
    void openEditor (fx::InstanceId id);

private:
    class IconButton;
    class Library;
    class RouteEdit;

    void timerCallback() override;
    void layoutChain();
    juce::Rectangle<int> chainArea() const;
    bool apply (const fx::Graph& next, const juce::String& what);
    fx::Graph route() const;
    juce::String subtitleOf (fx::InstanceId id) const;
    juce::String signature() const;
    int tileAt (juce::Point<int> p) const;
    int gapAt (juce::Point<int> p) const;
    void paintTile (juce::Graphics&, const Tile&) const;
    void paintMixer (juce::Graphics&) const;
    void showTileMenu (fx::InstanceId id);
    std::vector<fx::InstanceId> order() const;

    osp::plugin::InstrumentHost& host;
    AnalogEngine& engine;
    std::unique_ptr<IconButton> gear, close, add;
    std::unique_ptr<Library> library;
    std::array<std::unique_ptr<juce::Slider>, 3> levels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 3> levelAttachments;
    std::array<std::unique_ptr<juce::TextButton>, 4> footerButtons;   // <- -> PARALLEL SERIES

    juce::Rectangle<int> headerArea, mixerArea, zoneArea, footerArea, outputBox, wireFrom;
    std::vector<Tile> placed;
    std::vector<Gap> gapList;
    struct Group
    {
        juce::Rectangle<int> bounds;
        std::vector<int> laneCentres;
        bool split = false;
    };
    std::vector<Group> groups;
    int scrollX = 0, contentWidth = 0;
    std::optional<fx::InstanceId> selected, pressed, hovered;
    bool dragging = false;
    juce::Point<int> dragAt;
    int dropGap = -1, dropTile = -1;   ///< (indices into gapList / placed while dragging)
    juce::String shown;
};

/** The routing view for ShellProduct::sourceAreaView. */
std::unique_ptr<juce::Component> createFxRoutingView (osp::plugin::InstrumentHost& host);

/** A type's accent colour (its tile's left edge and icon) and its icon. */
juce::Colour fxAccent (fx::Type type);
juce::Path fxIcon (fx::Type type);

} // namespace andor::analog
