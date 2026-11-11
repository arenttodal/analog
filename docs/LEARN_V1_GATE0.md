# ANDOR Learn V1, Gate 0: baseline and boundary map

## Branches and pins

| Repository | Learn branch | Started from | Notes |
|---|---|---|---|
| `arenttodal/analog` | `feature/andor-learn-v1` | `origin/main` at `4bc6c41` (ANDOR Analog 1.0.0, release candidate merged) | `claude/release-candidate` (`2fc9856`) and `main` are never written. The Cinematic V6 branch (`claude/upbeat-allen-3i0jey`) is not included, so Learn can merge into either 1.x or V6. |
| `arenttodal/osp` (submodule `external/osp`) | `feature/andor-learn-v1` | `adf8ed6`, the OSP pin on Analog `main` (`d6cda7b`, the release candidate pin, merged into `claude/space-presets`) | V6's OSP commits (`fa299d3`, `a0339b0`: the arp companion) are not included. |

Learn is developed in a separate worktree (`/home/user/learn-analog`) with its own build directory, so the V6 checkout and its build stay as they were.

## What the baseline is

These are recorded from the code. Learn must change none of them.

- **Plugin identity.** `andor_add_instrument(ANDOR_Analog …)` sets:
  - `PRODUCT_NAME "ANDOR Analog"`, `SLUG analog`;
  - `PLUGIN_CODE Aanl`, manufacturer `Ospx`;
  - `BUNDLE_ID com.osp.andoranalog`.
- **APVTS schema.** `product/golden/parameter-schema.txt` lists 544 product parameters, after ANDOR OSP's shared schema. Learn adds none; the schema test runs as part of `andor_analog_tests`.
- **State.** `ProductInfo::stateVersion` is 13, and the root type is `ANDOR_ANALOG`. Learn writes nothing into the processor state, the presets or the session.
- **`Interface.xml`.** It sits at `<Documents>/ANDOR Analog/Interface.xml` (override: `ANDOR_DOCUMENTS_DIR`) and holds interface preferences only (the pinned well editor).
  - Learn keeps its own file elsewhere: `<user application data>/ANDOR/Learn/analog.json`, on macOS `~/Library/Application Support/ANDOR/Learn`. The override is `ANDOR_LEARN_DIR`.
  - This means there is never a second writer of `Interface.xml`.

## ARP user gestures

These are the handlers Learn reports from, after they wrote their parameter:

| Gesture | Handler | Parameter written |
|---|---|---|
| Light | `ArpControl::mouseDown` → `toggleEnabled()` | `arp.enabled` (one host gesture) |
| Card (not its light) | `ArpControl::mouseDown` → `onToggleEditor` → `ShellEditor::setArpExpanded` | none (a view setting) |
| Pattern row | `ArpInlinePanel::mouseDown` → `choosePattern` | `arp.pattern`, or `analog.arp.cardB/C.pattern` while unlinked and editing B or C |
| Octave key | `ArpInlinePanel::mouseDown` → `chooseOctaves` | `arp.octaves` or the card's own |
| PLAYS A / B / C | `ArpInlinePanel::mouseDown` → `toggleCard` | `analog.arp.cardA/B/C` (shown letters, whatever the storage layer) |
| LINK | `ArpInlinePanel::mouseDown` → `toggleLink` | `analog.arp.link` |
| EDIT A / B / C | `ArpInlinePanel::mouseDown` → `setEditCard` | none: `setArpViewCard`, a view setting |
| RATE | `MiniKnob` with a choice order: `slider.onValueChange` | `arp.rate` or the card's own |

RATE needs a note:

- The attachment updates the slider with `dontSendNotification`.
- So `onValueChange` fires only for a drag, the wheel, a double-click or an accessibility client: never for automation or presets.

Analog's visible cards map to storage layers as A = storage 0, B = storage 2, C = storage 1 (`threeEngines.order {0,2,1}`). The per-card arpeggiator's IDs are named by the visible letter.

## Popups, panels and views

- **Popovers.** `ShellEditor::openPopup` / `closePopup` handle them, and the outside-click watcher closes them on a press elsewhere.
  - Escape closes the popover first, then the well editor.
  - Learn's Escape goes before both, and only while Learn shows something.
- **ARP panel.** `setArpExpanded` relays out the instrument. The processor keeps the flag (`arpEditorExpanded`), and the timer follows it after a session recall.
- **Racks.** `setRackExpanded` and `toggleRackFromLabel` open them. A double-click on a macro's name, or the name's menu, opens its rack.
- **Source view (FX ROUTING).** `setSourceViewShown` creates the view on first use and keeps it afterwards.
- **Coordinates.** Every child of `frame::Content` is in reference units, and the window scales the content. So Learn's layers, as children of `content`, scale with the instrument.

## Boundary map (where Learn touches existing code)

| File | Change | Effect without a catalog |
|---|---|---|
| `shared/andor-ui/ArpSection.*` | `onLearnAction` on `ArpControl` and `ArpInlinePanel`, called from the existing click handlers; `isLinked()` and `shownRateKnob()` accessors | null by default: nothing |
| `shared/andor-ui/ShapingPopups.*` | `MiniKnob::onUserChange`, called after the ordered knob writes its parameter | null by default: nothing |
| `shared/andor-shell/ShellProduct.h` | `LearnDescriptor`, `ProductInfo::learn` (optional) | unset: nothing |
| `shared/andor-shell/ShellProcessor.h` | `learnEnabled()` (off in the parity render) | false |
| `shared/andor-shell/ShellEditor.*` | `createLearn()`, menu items, Escape first, `tick()` in the existing timer, user events from four existing click handlers, read-only accessors | no controller: the menu, keys, timer and handlers behave exactly as before |
| `shared/andor-shell/AndorInstrument.cmake` | Learn sources compiled into shell products; `LearnTests.cpp` in each product's test program | inert code |
| `shared/andor-learn/` (new) | core (data, catalog checks, runner, progress store), juce (targets, touch bridge, overlay, browser, controller) | — |
| `product/Source/AnalogLearn.*` (new), `Product.cpp` (one line), `CMakeLists.txt` | Analog's catalog, targets and capabilities | — |

## Baseline tests and screenshots (before any Learn change)

The test subset was run on `main` with the Learn build directory:

```
ctest --test-dir build-plugin -R "analog_dsp_tests|andor_analog_tests|andor_example_tests|osp_visual_reference|andor_bootstrap_tool"
```

The result is in [LEARN_V1_REPORT.md](LEARN_V1_REPORT.md) ("Baseline").

Five existing screenshot sets were captured before Learn was built, as the Learn-OFF reference: `[screenshots]`, `[screenshots-final]`, `[screenshots-routing]`, `[screenshots-rack]` and `[screenshots-three]`, 724 PNGs in all. The same sets are compared pixel for pixel after Learn is built.
