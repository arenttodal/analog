# USER MACROS V1.1: gate 0, baseline and inventory

The specification is ANDOR User Macros V1.1, the Vital-refinement handoff of 11 November 2026: the spec, the design addendum, the execution prompt and the unmodified Analog reference screenshot. Its §3.2 (the top-edge CORE | USER micro-tab) and §3.2a (the drag-to-assign lens) take precedence. The earlier tall, vertical selector is rejected and was not implemented. Later the owner's folder-tab mockup (11 November) replaced §3.2's flat micro-tab: CORE and USER are now two small tabs rising from the panel's edge. See [USER_MACROS_V1_1.md](USER_MACROS_V1_1.md).

## Branches and decisions

| Repository | Branch | Base |
|---|---|---|
| `arenttodal/analog` | `feature/user-macros-v1` | `feature/shaper-dna-v1` `2d97232`, stacked as the owner chose |
| `arenttodal/OSP` | `feature/user-macros-v1` | `feature/shaper-dna-v1` `94563a1` |

The owner's choices (11 November):
- stacked on SHAPER DNA: state version 19, parameter version hint 34, so nothing collides with DNA's 18 / 33;
- gates G0–G5, with FX-library slot targets last;
- a draft PR at the end.

Not touched: `main`, `claude/release-candidate` (1.0.0), and the V6 / V7 / DNA branches.

## Baseline

The base is SHAPER DNA's final tree. Before any USER MACROS change:
- `analog_dsp_tests`: 228 / 228, with every golden;
- the full 13-program ctest run on that tree: results in SHAPER DNA's push.

## Inventory (what USER MACROS touches)

| Where | Finding | Use |
|---|---|---|
| `InstrumentFrame.cpp` | `macroCentres` = 128, 274, 420, 566, 858, 712 (data order: ECHO and SPACE swapped visually); names centred at y 719, knobs at y 818; the light under each knob at its centre + 65; the macro panel is 35, 682, 1378 × 216 | USER knobs take the CORE knobs' exact bounds; the tab sits in the 26-unit band above the first name; the lens sits on the light |
| `ShellEditor` | `frame::MacroRow macros`; `setUpMacro`; label click / double-click / right-click; `frame::paint` draws the lights from the row it is given | A second `MacroRow` for USER, shown or hidden; the painter is given the shown row |
| `ModulationOverlay` | Right-click menus on destinations (`contextMenu`); 16 routes, fixed order | An opt-in `extendContextMenu` hook appends "Assign to USER". The route matrix is untouched. |
| `AnalogEngine::process` | `reader.snapshot (raw)`, then global modulation offsets, then `reader.read`, then `fxReader.read` | The USER offsets go right after the snapshot (they move the knob's base); `Reader::moveIndex` adds normalised, clamped offsets |
| `params::Reader` | `ids()`, `ranges`, `maxValues` 448 | Target index = `readerIndexOf (id)`; the six macro values are read through the same snapshot |
| `fxparams::Reader` | Fixed entries per type and slot (`analog.fx.<type>.<n>.<name>`) | G5: an overlay read path |
| `FxRoutingView::apply` | Every route edit is one `RouteEdit` undo action | G5: orphaning goes into the same transaction |
| `Interface.xml` | The interface preferences (WellEditor) | The bank shown (`MacroBank shown="core|user"`) |

## No-change list

- Every existing parameter ID, range, order and default.
- The 16 modulation routes and their destination order.
- CORE's names, popovers, racks and automation.
- The six knobs' places.
- The macro panel, the cards, the rack (214 px), the arpeggiator, the keyboard and the window height.
- Plugin IDs, factory sounds, the FX graph schema, DSP algorithms.
- ANDOR OSP: no tab, no parameters, the same pixels. Every addition is opt-in through `ProductInfo::userMacros` and never in the parity render.
