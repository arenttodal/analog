# ANDOR — PERFORMANCE MODE R1
## Shared implementation specification for ANDOR OSP and ANDOR ANALOG

Status: QUEUED (received 22 Nov 2026, owner's specification, recorded verbatim in substance).
Starts after the AUX KONTAKT rack / AUX strip tweaks. Dedicated branch; no merge into signed
beta or release branches without approval.

**Objective:** Introduce a beautiful, minimal Performance Mode that transforms ANDOR into a focused instrument for playing rather than editing.

**Products:** ANDOR OSP, ANDOR ANALOG. **Repositories:** `arenttodal/OSP`, `arenttodal/analog`.

**Implementation:** Shared interface components and state model wherever possible, with minimal product-specific integration.

**Priority:** High-value interface feature; no DSP changes permitted.

---

# 1. PRODUCT VISION

Performance Mode is a second presentation of the same instrument. Edit Mode: build and customize sounds. Performance Mode: play and express those sounds. Both operate on exactly the same underlying instrument state. Entering Performance Mode never changes the sound, routing, macros, modulation, arpeggiator, effects or loaded sources — only how the interface is presented.

Design goals:
1. Make the instrument visually quieter and more approachable.
2. Give source visualizations significantly more prominence.
3. Present up to eight large, customizable performance macros.
4. Keep the keyboard, pitch wheel and modulation wheel visible.
5. Hide unnecessary technical controls and editing tools.
6. Switch modes instantly with a single icon.
7. Persist the user's preferred mode and visible macros.
8. Match the existing premium ceramic/walnut/wood ANDOR design.
9. Implement for both products without maintaining separate behavior.
10. Preserve all existing functionality and backwards compatibility.

# 2. MODE TOGGLE

- Small icon-only toggle in the top header, beside the existing routing/source-view controls; existing small header-button visual language; no permanent text label.
- Edit Mode active: gauge/performance icon, tooltip "Enter Performance Mode". Performance Mode active: pencil icon, tooltip "Edit Instrument". The icon always represents the action available. No permanently highlighted Performance button; no mode labels in the header.
- One click switches. No confirmation, no audio interruption, no parameter changes, no macro reset, no instrument reload, no MIDI interruption; held notes preserved; sensible keyboard focus.
- One stable button component whose icon changes; do not recreate the editor.
- Keyboard-accessible action and meaningful accessibility label; any shortcut must not conflict with existing plugin or DAW shortcuts.

# 3. PERFORMANCE MODE LAYOUT

**A. Header** — keep: branding, preset name, preset navigation, favorite, master volume, main menu, Performance/Edit toggle; existing save access without new emphasis. Normal header height.

**B. Source visualization cards** — the existing loaded source visualizations, simplified; 1, 2 or 3 sources; current card artwork, rendering, waveform colours and source-specific behaviour (no fake renderer).

Hide (representative, not exhaustive — find every source-edit widget): A/B/C badges; root pitch labels (A4, E4...); source edit menus; Mute/Solo; source selection/edit buttons; recording controls; link; reverse; loop; follow; synth mode selectors; granular controls; source EQ; source knobs (START, TUNE, PAN, LEVEL, REIMAGINED, others); small utilities within the display; hover-revealed editors.

Visual: visualizations occupy nearly the entire card interior; remove header/footer spacing of hidden controls. Preserve card contours, ceramic background, waveform identity, source colour, rounded dark display, subtle shadows, correct waveform scale, playback visualization. "Three elegant visual windows, not three miniature workstations."

No visible A/B/C or pitch labels in Performance Mode (accessibility labels / subtle tooltips allowed). Edit Mode keeps them exactly.

Status: loaded source shows its real visualization; silent/inactive subtly dimmed by existing principles; a muted source not shown as active; empty instrument shows a quiet "Load a sound" state; Edit Mode always reachable.

Sizing: one source full width; two share evenly; three equal cards; no unused columns. Works with OSP samples and ANALOG oscillator, noise, organic and hybrid visualizations (not every ANALOG source is a recording).

# 4. HIDDEN EDITING AREAS

Hidden: modulation envelope/LFO panel; modulation routing overlays; advanced panels; open FX racks; expanded and collapsed ARP controls; STRUM and SATELLITE editing; ANALOG SUB editing; source routing editor; AUX controls and strip; CORE/USER bank tabs; source/engine configuration overlays.

Hide the interfaces, not the functions: ARP, STRUM, SUB, FX routing, modulation, granular all keep running.

Returning to Edit Mode restores the previous layout where practical: selected source, ARP expanded state, CORE/USER bank, FX routing view, rack expansion, AUX visibility, editing preferences. Close transient popovers safely on transition. Define a clear policy for Learn mode, file operations, recording or other uninterruptible workflows; never silently discard unsaved work.

# 5. MACRO PERFORMANCE STRIP

- One row of up to eight visible macros using the existing ANDOR macro knobs (ceramic artwork, colours, value arcs, bindings, automation, modulation feedback, tooltips). No second macro implementation; same parameters as Edit Mode.
- Core macros from the actual product configuration, in their current display order. OSP: LIFE + its dynamics/character/filter, DRIVE, MOVEMENT, ECHO, SPACE equivalents. ANALOG: MORPH, DRIVE, FILTER, MOVEMENT, ECHO, SPACE. Do not rename or remap.
- Default (no customization): all six core macros + up to two assigned USER macros; fewer than eight if fewer are assigned; never show unassigned USER knobs.
- Custom selection: six core + six USER slots (where available and functional); max eight; at least one; replacements and smaller sets allowed.
- Reordering persists; up/down or drag list in the configuration popover; no reorder controls on the main surface.
- Stable identity: core by product parameter ID; USER by stable slot identity (not display name). Renaming must not break selection. No pointers or visual indices.
- A selected USER macro that becomes unassigned: keep it in preferences, don't show a knob, mark it unassigned in the configuration UI, reappear automatically when reassigned, never silently overwrite.

# 6. THE MACRO CONFIGURATION FLAP

- Small tab/flap on the upper-right edge of the existing macro housing, integrated into the ceramic frame, small sliders icon, no text, no pill, no prominent active colour.
- **The macro panel must not become taller because of this flap.** No new toolbar row, no added top padding, knobs not moved down, outer panel height unchanged, keyboard not shifted. The flap may overlap the panel's top edge or sit in its upper-right margin, within the existing allocation. Accessible hit target even if visually small.
- Click opens a compact popover anchored to it; tooltip "Choose visible macros"; click again / outside / Escape closes; the popover never resizes the editor.
- Popover: title "Performance Macros"; core and USER macros with checkboxes; selected count ("7 of 8"); simple reorder; "Reset to Default". USER macros show current names and assignment status; unassigned: quiet "Assign in Edit Mode". Enforce max eight and never empty. Immediate preview, no Apply; persisted safely; opening it changes no values.
- Shared ANDOR popover/menu styling: warm ceramic, consistent radii, compact controls, typography, minimal borders, subtle hover, correct shadow, proper anchoring. No oversized modal.

# 7. MACRO LAYOUT GEOMETRY

- Existing macro panel height. Performance Mode replaces the macro/modulation presentation with one macro row across the available width (the modulation panel's width gives the extra space).
- 1..8 macros; 6..8 distributed evenly; standard knob diameter and label treatment (don't shrink for eight).
- Current knob baseline and vertical rhythm; the flap never overlaps labels, arcs, values or assignment indicators.
- No CORE/USER tabs in Performance Mode; mixed selection allowed; an optional subtle divider must not disrupt the chosen order.
- **Hard acceptance:** at the same UI scale, the Performance Mode macro housing is no taller than the standard one — verified by screenshot comparison.

# 8. KEYBOARD AND PERFORMANCE CONTROLS

Keep the bottom keyboard, keys, octave range, pitch and mod wheels (automation and MIDI unchanged), held-note visualization. Same height; no MIDI routing or wheel behaviour changes. Keep help/info access; advanced keyboard utilities may be hidden.

# 9. PERSISTENCE — CRITICAL

Survive close/reopen: active mode, selected macros, macro order, default-selection customization. They are UI presentation settings, not musical parameters.

- A. Product-level preferences in the existing per-product interface-preferences mechanism (`Interface.xml`); OSP and ANALOG separate. Illustrative fields: `performanceMode`, `performanceMacroIds`, `performanceMacroOrder`, `performanceViewVersion` (follow current conventions; no new preferences subsystem).
- B. DAW session instance state: each instance keeps its own presentation.
- Precedence: valid instance view state → product preferences → factory default (Edit Mode). Older projects load normally.
- Presets: loading a preset never changes the view or macro selection; saving a preset never embeds the UI layout (filter view fields if session and preset serialization are shared).
- Safeguards: stable IDs; ignore unknown IDs; dedupe; clamp to eight; keep valid selections; handle missing USER support and old versions; never write settings from the audio thread; no public parameter schema change.
- Changing the selection never changes a macro value; mode changes never change the sound.

# 10. SHARED IMPLEMENTATION STRATEGY

OSP: `apps/plugin/Source/PluginEditor.*`, `shared/andor-ui/Source/EngineCard.*`, `InstrumentFrame.*`, `UserMacroController.*`. ANALOG: `shared/andor-shell/Source/ShellEditor.*`, `ShellProcessor.*`, `ShellProduct.h`, `product/Source/Product.cpp`, shared USER macro hooks and bank components.

One shared presentation model (mode, selection, identity validation, serialization, defaults, layout calculations, toggle semantics) and shared GUI components (mode key, flap, popover, macro strip, simplified EngineCard). Separate integration glue for the OSP editor and ShellEditor only. Suggested boundaries: PerformanceViewState, PerformanceMacroSelection, PerformanceMacroPanel, PerformanceMacroPicker, PerformanceModeKey, EngineCard presentation mode (conceptual names). Reuse existing macro bindings; no duplicate parameters or second automation/modulation mapping; hidden controls stay connected where required.

# 11. ANALOG-SPECIFIC REQUIREMENTS

Support independent oscillators, hybrid sources, C2, SUB, product FX routing; existing ANALOG source representations (no invented sample waveforms); ANALOG's own six core macros; USER macros only if installed and enabled in the current revision (else six core macros, no inert knobs); shared functionality developed in OSP's shared code and ANALOG updated to an approved tested pin, coordinated with the OSP/ANALOG parity integration; preserve walnut materials, copper/sage accents — native to ANALOG.

# 12. EDGE CASES (test explicitly)

One / two / three sources; empty instrument; missing sample; modulation active; ARP active; SUB active; USER macro unassigned / renamed / reassigned; preset changed; project reopened; multiple instances (no cross-talk); UI scale changes; modulation overlay open; toggle during playback (no note loss, click, reset or interruption); Learn/tutorial active (safe transition, Learn targets updated).

# 13. VISUAL REGRESSION REQUIREMENTS

Matrix: OSP and ANALOG × 1/2/3 sources × Edit/Performance. Performance also: six, eight, fewer than six macros; mixed core + USER; open flap/popover; empty source state. Scales 100 %, 150 %, 200 %.

Verify: header height unchanged; mode icon placed; no letter badges; no pitch labels; no per-card editing controls; visualizations fill their regions; no gaps; modulation area hidden; ARP/SUB UI hidden; keyboard placed; no clipped/overlapping knobs; macro panel height unchanged; flap adds no vertical space; popover anchored; no broken shadows or seams; consistent ceramic/walnut. Compare against the approved Performance Mode mockup.

# 14. ENGINE AND SESSION REGRESSION TESTS

- Sound preservation: render a deterministic phrase in Edit, Performance, Edit again — bit-identical (document any case where a controlled comparison is used instead). Presets: basic; complex three-source; active ARP; modulation routes; effects + reverb; STRUM; ANALOG SUB and MORPH; USER macros on active destinations.
- Parameter compatibility: no new automatable parameters; no changes to IDs, ordering, modulation indices, automation, plugin identifiers, preset extensions, sample references, migrations.
- Persistence: mode and selection/order survive editor close/open and DAW reload; preset load keeps the view; two instances differ; old states load; missing IDs fall back; default available; OSP/ANALOG preferences separate.
- Performance: switching is UI-thread only; no allocation, file access, plugin reconstruction or synchronization changes on the audio thread; no resets of readers, voices or FX.

# 15. GATES

0 Audit (architecture, macro components, USER macro support per product, view preferences, EngineCard constraints, session/preset separation, dependency conflicts; baseline build/test). 1 Shared presentation state + serialization (pass: no sound/parameter change; persistence works without GUI). 2 Mode toggle (pass: never reinitializes DSP or changes values). 3 Simplified EngineCard (pass: all visualizations work; Edit restores controls). 4 Performance macro layout (pass: same panel height, bindings intact, no clipping). 5 Configuration flap + popover (pass: no height or keyboard change). 6 Persistent preferences (pass: no unexpected resets). 7 OSP integration (pass: works, no editing regressions; OSP suites). 8 ANALOG integration (pass: equivalent experience, sound unchanged; V6 regressions). 9 Full visual QA. 10 DAW validation (record automated vs manual; never claim an untested host).

# 16. DEVELOPMENT SAFETY

Dedicated branch/worktree; no merge into signed beta/release without approval; no sound-engine changes for visibility; no divergent copies; factory presets untouched; no deleting hidden components; no unrelated large refactors; no silent replacement of state files; reversible, incremental, testable patches.

# 17. REQUIRED FINAL REPORT

Commits/branches; shared architecture; OSP status; ANALOG status; persistence; macro selection; screenshot comparisons; automated tests; DAW tests; limitations; remaining compatibility work; recommended polish. PASS / PARTIAL / BLOCKED per area; never claim untested visual or DAW checks.

# FINAL ACCEPTANCE

A musician can: open an instrument; click one small Performance icon; see sources, selected macros and the keyboard; play undistracted; use up to eight macros; click a tiny flap to choose them; close/reopen without losing choices; load another preset keeping them; click the pencil to return to Edit; hear the exact same sound throughout.

**The macro panel never becomes taller because of the flap. A/B/C badges and root pitch labels are completely hidden in Performance Mode. OSP and ANALOG audio and state compatibility preserved.**

*Edit the instrument when you need to. Play it when you want to.*
