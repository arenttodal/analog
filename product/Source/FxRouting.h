#pragma once

#include "fx/FxGraph.h"
#include "InstrumentHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <optional>

namespace andor::analog
{

class AnalogEngine;

/**
    FX ROUTING (docs/FX_ROUTING_IMPLEMENTATION.md): the view that takes the source cards' place
    when the header's routing icon is on (ShellProduct::sourceAreaView). Interface only: opening
    it, selecting, hovering or dragging never changes the sound; a drop, a menu choice or a key
    that edits the route does (and makes it CUSTOM), as one undoable step.

    Left, the cards' mixer (a third of the width at most: the cards' levels, the path they take);
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
    /** A group summary (V4): what the compact board shows in place of effects it cannot fit at
        a readable size (a parallel stage, or the chain's tail). Never hidden: its count, its
        members' bypass state, and a menu that reaches each of them. */
    struct Summary
    {
        juce::Rectangle<int> bounds;
        std::vector<fx::InstanceId> members;
        bool group = false;   ///< a parallel / split stage (else the chain's last effects)
        int lanes = 0;
    };
    const std::vector<Summary>& summaries() const noexcept { return summaryList; }
    /** One row, or two (a serial chain wrapped left to right, never reversed). */
    int rowCount() const noexcept { return static_cast<int> (rows.size()); }
    juce::Rectangle<int> continuationOut() const noexcept { return contOut; }
    juce::Rectangle<int> continuationIn() const noexcept { return contIn; }
    juce::Rectangle<int> chainBounds() const { return chainArea(); }
    juce::Rectangle<int> mixerBounds() const noexcept { return mixerArea; }
    juce::Slider* levelKnob (int card) const noexcept { return card >= 0 && card < 3 ? levels[static_cast<std::size_t> (card)].get() : nullptr; }
    /** V5: a tile's mini knobs (tests): their parameter IDs, and the knob for one. */
    juce::StringArray tileKnobParameters (fx::InstanceId id) const;
    juce::Slider* tileKnob (fx::InstanceId id, const juce::String& parameterId) const;
    /** The tiles' scale on this board (1 at the canonical width; larger in design units when
        the window is small, so names stay legible). */
    float tileScale() const noexcept { return metrics.b; }
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

    /** Cinematic V6 gate 4: a MAIN + SEND route's tabs (0 MAIN, 1 SEND, 2 OVERVIEW): the
        board shows MAIN's or SEND's chain (edited in place), or both in miniature; the mixer's
        knobs are the cards' levels on MAIN and their sends on SEND. Interface only. */
    int tab() const noexcept { return currentTab; }
    void setTab (int index);
    juce::Rectangle<int> tabBounds (int index) const noexcept { return tabAreas[static_cast<std::size_t> (std::clamp (index, 0, 2))]; }
    /** The musician's explicit conversion (gear): to MAIN + SEND (CLASSIC's inserts on MAIN, its
        ECHO and SPACE on SEND; card C sent at 25 % when nothing is sent yet), and back. */
    bool useMainSend();
    bool useSinglePath();

    juce::PopupMenu settingsMenu();
    /** FX CHAIN presets (the gear): the route and its new effects' settings, as a file. */
    bool saveChain (const juce::String& name);
    bool loadChain (const juce::File& file);
    juce::Component& settingsButton() noexcept;
    juce::Component& addEffectButton() noexcept;
    /** The library popover (shown: + ADD EFFECT). */
    juce::Component* libraryPopover() const noexcept;
    void showLibrary (bool shown);
    /** Opens an effect's full rack (the original four: their own racks / DRIVE's popover). */
    void openEditor (fx::InstanceId id);
    /** A tile's quick settings, anchored to it (nullopt: closes them). Interface only. */
    void showQuick (std::optional<fx::InstanceId> id);
    juce::Component* quickPopover() const noexcept { return quick.get(); }
    std::optional<fx::InstanceId> quickShown() const noexcept { return quickId; }

private:
    class IconButton;
    class Library;
    class RouteEdit;
    class TileKnob;

    void timerCallback() override;
    void layoutChain();
    void projectRoute();
    /** V5: each full-size tile's 2-3 small real knobs (the same parameters as its rack). */
    void placeTileKnobs();
    juce::Rectangle<int> chainArea() const;
    bool layoutAttempt (const fx::Graph&, float scale, const std::vector<bool>& collapsed, int tailFrom);
    int summaryAt (juce::Point<int> p) const;
    juce::Rectangle<int> mixerRow (int index) const;
    void showSummaryMenu (int index);
    void paintSummary (juce::Graphics&, const Summary&) const;
    bool apply (const fx::Graph& next, const juce::String& what);
    fx::Graph route() const;
    juce::String subtitleOf (fx::InstanceId id) const;
    juce::String signature() const;
    int tileAt (juce::Point<int> p) const;
    int gapAt (juce::Point<int> p) const;
    void paintTile (juce::Graphics&, const Tile&, bool ghost = false) const;
    void paintMixer (juce::Graphics&) const;
    void showTileMenu (fx::InstanceId id);
    void placeQuick();
    std::vector<fx::InstanceId> order() const;

    osp::plugin::InstrumentHost& host;
    AnalogEngine& engine;
    std::unique_ptr<IconButton> gear, close, add;
    std::unique_ptr<Library> library;
    std::unique_ptr<juce::Component> quick;
    std::optional<fx::InstanceId> quickId;
    std::array<std::unique_ptr<juce::Slider>, 3> levels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 3> levelAttachments;
    std::vector<std::unique_ptr<TileKnob>> tileKnobs;

    juce::Rectangle<int> headerArea, mixerArea, zoneArea, outputBox, wireFrom;
    std::vector<Tile> placed;
    std::vector<Gap> gapList;
    struct Group
    {
        juce::Rectangle<int> bounds;
        std::vector<int> laneCentres;
        bool split = false;
    };
    std::vector<Group> groups;
    std::vector<Summary> summaryList;
    struct Row
    {
        int centreY = 0, left = 0, right = 0;
    };
    std::vector<Row> rows;
    juce::Rectangle<int> contOut, contIn;   ///< CONT. 2 / FROM 1 (two rows only)
    struct Metrics
    {
        float b = 1.0f;
        int tileW = 192, tileH = 84, stackedH = 60, gapW = 22, stackGap = 8, branch = 18, rowGap = 28;
    };
    Metrics metrics;
    float layoutScale = 0.0f;   ///< the window scale the board was laid out for
    float mixerScale = 1.0f;    ///< the mixer's type and knobs (with the board's legibility)
    std::optional<fx::InstanceId> selected, pressed, hovered;
    bool dragging = false;
    juce::Point<int> dragAt;
    int dropGap = -1, dropTile = -1;   ///< (indices into gapList / placed while dragging)
    juce::String shown;
    int currentTab = 0;
    std::array<juce::Rectangle<int>, 3> tabAreas;
    void bindMixer();
    void paintTabs (juce::Graphics&) const;
    void paintOverview (juce::Graphics&) const;
};

/** The routing view for ShellProduct::sourceAreaView. */
std::unique_ptr<juce::Component> createFxRoutingView (osp::plugin::InstrumentHost& host);

/** A type's accent colour (its tile's left edge and icon) and its icon. */
juce::Colour fxAccent (fx::Type type);
juce::Path fxIcon (fx::Type type);

} // namespace andor::analog
