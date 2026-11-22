# ANDOR ANALOG — status

> **22 Nov 2026, PERFORMANCE MODE R1 in ANDOR ANALOG - first working version (branch
> `claude/performance-mode`; spec `docs/specs/PERFORMANCE_MODE_R1.md`; shared code in OSP's
> `claude/performance-mode`):**
> - The header's gauge key (after FX ROUTING) switches to Performance: the sources' pictures alone
>   (no badges, roots, keys, knobs or well tools; an empty card C leaves no column), one row of
>   the chosen macros across the whole macro housing (MORPH's lamp with its name), the keyboard and
>   wheels; the modulation panel, CORE/USER tabs, arpeggiator, racks, AUX strip and utility keys
>   hidden but unchanged (the pencil key brings each back as it was). The flap at the housing's
>   top right opens "Performance Macros" (choose up to eight core and USER macros, order them,
>   Reset to Default; USER macros not assigned say "Assign in Edit Mode" and never show a knob).
> - Kept per instance in the session (never in a preset; loading one keeps the view) and as this
>   computer's preference (Interface.xml). A new editor opens in the view it was left in.
> - Tests `[analog][performance]`: key, cards, strip, flap inside the housing, picker (choose,
>   order, never none, reset), Edit restored (height, panels, cards), every parameter and a
>   rendered phrase identical; screenshots `[screenshots-performance]`.
> - **Owner feedback applied:** the pencil takes FX ROUTING's place in Performance (no gap);
>   MORPH's lamp hidden while performing. OSP's own editor now has the same Performance Mode
>   (OSP d07e943d).
> - **Not yet:** QA at 150 % / 200 %, the 8-macro and mixed-USER captures, multi-instance and
>   playing-while-toggling checks in a DAW (owner).

> **22 Nov 2026, the KONTAKT rack ("Nameplate", owner-approved) and the AUX strip's X (ANALOG only):**
> - **KONTAKT rack:** the selector row keeps only MODE. A nameplate (a three-knob-wide cell, the
>   shared opt-in `Slot::threeKnobs`) shows the instrument's name large (from Kontakt's state, else
>   Kontakt's), Kontakt's name, version and latency small, and OPEN KONTAKT / LOAD... (choose which
>   Kontakt) / UNLOAD; empty, not found or failed: LOAD KONTAKT (loading: it says so). Then LEVEL
>   and TRANSPOSE (`analog.aux.transpose`, -12..+12 st, MIDI only), and PLAYS - KEYS / ARP A /
>   ARP B / ARP C, one control for D ARP and FOLLOW (FOLLOW is kept while KEYS is chosen). The
>   picture: "<INSTRUMENT> - PLAYING" in its title line and a keyboard (two octaves, three when the
>   notes need it, following them) with the pitches sent to Kontakt filled and the keys you hold
>   dotted, "SENT C4 - E4 - G4 - +12 ST" under it.
> - **AUX strip:** its ... key is now an X that hides AUX at once (D plays on; shown again from the
>   main menu). A right-click on the strip (or its display) opens the menu: Hide AUX, Open / Close
>   AUX Rack, Reset. A right-click on the display no longer loads.
> - Tests: `[aux][ui][kontakt-rack]` (the plate's keys by state, TRANSPOSE and LEVEL in the rack,
>   PLAYS writing D ARP + FOLLOW, the sent/played pitches), the strip test's X; screenshots
>   `[screenshots-kontakt-rack]`. OSP pin: the shared slot only.
> - **Queued next:** Performance Mode R1 (owner spec recorded in `docs/specs/PERFORMANCE_MODE_R1.md`).

> **22 Nov 2026, card C with Kontakt: a nameplate, LOADING, its window by itself (ANALOG only):**
> - **The well (owner-approved mock-up):** empty - one LOAD KONTAKT key; loading - a spinner,
>   "LOADING KONTAKT..." and "THE FIRST TIME CAN TAKE A MOMENT"; loaded - the instrument's name
>   large, Kontakt's version small, OPEN KONTAKT and UNLOAD, a quiet MORPH - GLOBAL tag. The
>   explanatory lines are gone; only a problem is put in words (not found: one short line, the
>   folders searched in the key's tooltip). The nameplate paints the empty well under itself, so
>   the silent waveform line and grid no longer run through its text.
> - **LOADING at once, then its window by itself:** LOAD KONTAKT (card C, and D's KONTAKT) turns
>   the state `loading` before Kontakt starts (the well paints it first), then loads (asking where
>   Kontakt is if it is not found) and opens Kontakt's own window - no second OPEN step.
> - **Faster later loads:** what Kontakt reported when found is remembered beside its location and
>   used while the file is unchanged, so later loads skip the scan (a VST3 scan starts the plug-in
>   once just to describe it). A changed file is scanned again; one that no longer loads is
>   forgotten and scanned.
> - **Instrument names:** read from Kontakt's saved state (.nki / .nkm file names, 8-bit or UTF-16)
>   when it loads and when its window closes or goes to the background; two names show as
>   "+ second", more as "+ N more"; none found: Kontakt's own name. D's readout shows the first.
>   **OWNER TEST:** whether real Kontakt's state names its instruments this way (if not, the card
>   shows "Kontakt" and its version), and how long the first and later loads take on Windows/macOS.
> - Tests: `[external][loading]` (LOADING, then loaded with its window open; pressed twice;
>   an instance gone before its load), `[external][names]`, the remembered description in
>   `[external][finder]`; the card's UI test; screenshots `[screenshots-external]` (+ loading).

> **22 Nov 2026, a saved project reopens with its preset's name (ANALOG and OSP, every platform):**
> the session kept the sound but not which preset was open, so reopening a project showed "Init"
> (OSP: a starting state's name). The session (never a preset file) now records the open preset's
> name and file; reopened, the name is back and SAVE still updates that preset; if the file is not
> on this computer the name still shows and SAVE asks for a name. Older sessions open as before.
> Tests: `[shell][presets][session]`, OSP `[plugin][menus][session]`.

> **22 Nov 2026, Kontakt finder, AUX card height, D MIDI TRANSPOSE (branch `claude/analog-unison`):**
> - **Finding Kontakt (Windows, macOS, Linux):** the search covers every standard VST3 location
>   (both Program Files' Common Files\VST3 and the per-user one on Windows; the system's and the
>   user's Library on macOS) and goes four folders deep (Native Instruments nests Kontakt), never
>   into a bundle; the newest Kontakt first. Where it was found or chosen is remembered (ANALOG's
>   settings, shared by the standalone and every host) and tried first. Not found: LOAD KONTAKT
>   opens a file browser at the VST3 folder ("Where is Kontakt?"); the choice (the bundle or the
>   file inside it) must be an instrument, loads, and is remembered. The message names the folders
>   searched. Tests: fake folder layouts (nested, bundles, depth), the question answered and
>   remembered, a non-instrument refused. **OWNER TEST:** real Kontakt on a Windows PC and a Mac.
> - **The cards with AUX shown:** showing D's strip made the cards bring back the row under their
>   knobs they had dropped (an empty band, shorter wells); the cards' row now follows the cards
>   alone (shared shell fix; nothing changes with AUX hidden). The AUX test checks the wells too.
> - **D KONTAKT MIDI TRANSPOSE** (`analog.aux.transpose`, -12..+12 st, default 0): the notes sent to
>   D's Kontakt move, never its audio; each note-off goes where its note-on went (a transpose
>   changed mid-note leaves nothing hanging); a pitch past 0..127 is not sent. The keys held and
>   the pitches sent are published for the rack's keyboard. Its control arrives with the KONTAKT
>   rack redesign (owner chose the mock-up "Nameplate"). Schema golden +1 parameter (intentional).
> - **SAVE (OSP and ANALOG, shared):** a click saves over the open preset after "Save over
>   <name>?" (Save / Cancel, "Don't warn me again for this patch", kept per preset file); a
>   right-click on SAVE is Save As.
> - **Preset folders:** the factory set by CATEGORIES right in the menu (its folders: Basic, Bass,
>   Brass, Keys, Lead, Pad, Sequence, Texture), as ANDOR OSP lists them; the musician's own in one
>   **User** submenu (everything outside Factory), each of their folders a submenu (subfolders
>   first, then the folder's presets) as arranged on disk - a pack in a folder is one entry, not
>   100 loose names. The arrows walk the same order. (Owner's call: factory flat by category,
>   only User in a submenu.)
> - **UNISON fan:** shown only when a hand moves VOICES, DETUNE, BLEND, WIDTH, MODE or RANGE (a
>   change inside a control's gesture, or within 0.4 s of it); opening a preset, a session or the
>   host's automation shows nothing.

> **21 Nov 2026, D / AUX gets its own EQ and joins the routing (branch `claude/analog-unison`):**
> - **D's EQ:** the cards' five-band EQ (ANDOR OSP's layer EQ processor) on D's stereo bus,
>   after its source and before its FILTER. 19 parameters `analog.aux.eq.*` (the cards' names,
>   ranges and defaults; version hint 41), off by default: every session plays as before. An
>   **EQ** key on D's strip (before RACK) opens it in a panel under the strip (the cards' EQ
>   editor via OSP's new `EqTarget`; no DAMPEN, no spectrum yet); its word lights while D's EQ is on.
> - **D in the routing:** the source column has four rows - A, B, C and **D AUX** (its mode, or
>   OFF). On one path and on MAIN its knob is D's LEVEL (D already ran through the chain); on
>   SEND it is D's new **SEND** (`analog.aux.send`, 0-100 %, default 0), which feeds SEND's
>   ECHO / SPACE with D as it leaves its FILTER, at its level (MUTE, SOLO, power respected),
>   ramped like the cards' sends. OVERVIEW's SEND feed lists D. Heard only in MAIN + SEND.
> - Tests: D's EQ off is bit-identical and on shapes D; D's SEND changes nothing on one path and
>   brings SEND's room in MAIN + SEND; the routing's D knob (LEVEL / SEND) and the strip's EQ
>   key; every new control in the bindings test. Schema golden +20 parameters (intentional).

> **21 Nov 2026, UNISON fan + AMP 2's one-second gesture (branch `claude/analog-unison`):**
> - **UNISON fan** on cards A, B and C (OSC 3): while VOICES, DETUNE, BLEND, WIDTH, MODE or RANGE
>   changes, one line per copy fades in over the waveform, drawn from the engine's own layout:
>   across, its detune (the outer copies' cents beneath); its height, its level (BLEND lifts the
>   outer lines while the centre sinks); the centre in the card's colour, left copies ivory, right
>   amber. A caption says what BLEND did ("OUTER COPIES 50 % OF CENTRE"; "BLEND NEEDS 3" with two
>   voices). It fades out 1.2 s after the last change. Owner chose it from three mock-ups (no dot
>   caps). Presentation only.
> - **AMP 2:** a phrase's first second of keys (a chord, a strum, a slow roll) takes AMP 1; only
>   keys begun more than 1 s after the phrase's first, over held ones, take AMP 2 (was 30 ms).

> **21 Nov 2026, UNISON per card, MONO + LEGATO, AMP 2 (branch `claude/analog-unison`, state
> version 24):**
> - **UNISON inside each voice (Serum-style), cards A, B and C (OSC 3):** VOICES 1-8, DETUNE
>   (outer copies at detune^2.5 x RANGE), BLEND (our own constant-power law: loudness never
>   changes), WIDTH (mid/side: the side through twins of the cards' EQ/DAMPEN, the filter, AMP,
>   SHAPER DNA; equal-power placement, never louder), MODE (LINEAR, SUPER, EXP, INV, RANDOM from
>   the note's seed). Polyphonic: every note keeps its own copies and attack. One copy is the old
>   path bit for bit. CPU here: 8 voices x 7 wide copies +10 % of a core. A card in GRANULAR keeps
>   its copies mono.
> - **Card row:** DETUNE (unison), BLEND, TUNE, OCT, LEVEL. FINE tuning (cents) and card C's VELO
>   moved to the card's UNISON panel, opened from the voices chip (`‹ 4 ›`, beside the model in the
>   well: arrows step VOICES, the number opens it). The panel draws the copies (place = detune,
>   height = level, tint = side).
> - **REIMAGINED hidden** (LEVEL took its slot); its engine and state stay. Switched off in Grain
>   Cloud (was 0.45), Kaleidoscope Pad (0.65), Tape Frame Keys (0.6), Toybox Chime (0.7), Mirage
>   Bass (0.65), Frozen Glass (0.3).
> - **MONO + LEGATO** (FILTER rack, VOICE: VOICES, PLAY POLY/MONO, LEGATO, GLIDE, SPREAD). LEGATO
>   off (new default): every key fades 3 ms and starts its envelopes from zero; on: the old slide.
>   The global UNISON is retired: version-23 sessions with it become each card's UNISON (same count,
>   outer detune, even level, WIDTH = STEREO SPREAD) + MONO + LEGATO; MONO sessions get LEGATO on.
>   Factory sounds (factory version 10): Unison Lead uses card unison in MONO; the eight MONO sounds
>   play with LEGATO off.
> - **AMP 2 (ANALOG only):** a held-note attack. A note begun while another key is held (or the
>   pedal holds a note), more than 1 s after the phrase's first key (a chord or slow strum is one gesture), takes AMP 2's ATTACK
>   (D/S/R from AMP 1; latched per voice). The keys played decide, never the arpeggiator's steps
>   (an arp step follows the key it comes from). AMP tab: "+ AMP 2", then `1 | 2 ×` chips, AMP 2's
>   attack as a ghost, the "2" chip flashes as a note takes it. OSP's hook is opt-in
>   (`ProductInfo::heldAttack`): ANDOR OSP unchanged.
> - **Goldens (intentional):** legacy, fx-baseline and v6-baseline renders for the 14 changed
>   factory sounds only; parameter schema (+24 parameters, all v40).
> - **OWNER TEST:** listen to Unison Lead and the REIMAGINED-off sounds; try LEGATO off on the
>   MONO leads; AMP 2 with a held drone. DAW checks are owner tests.

> **20 Nov 2026, card C's playhead in RHYTHM; Windows checkout; macOS click test (owner reports
> and the first CI run of the merged line):**
> - **Card C's playhead follows RHYTHM:** with a drum loop on C and RHYTHM on, a key moved the
>   RHYTHM panel's playhead but not the one on card C (the loop player plays the recording, not
>   C's voices, and the card only showed the voices' read heads). `Synth::collectPlayheads` now
>   adds the loop player's head while RHYTHM plays, at the same place the panel draws it (wrapped
>   into the loop's bars). New test (`[rhythm][ui]`, AUDIO and BOTH): the card's head is there
>   and moves; RHYTHM off, none. It fails without the change.
> - **Windows could not check out the repository:** `aux` is a reserved file name on Windows, so
>   `docs/aux/` and `docs/demos/aux/` stopped the Windows job at checkout. Renamed to
>   `docs/aux-d/` and `docs/demos/aux-d/` (every reference updated; no other reserved names).
> - **Windows build (MSVC):** OSP's shell included `"FxRouting.h"` by name and MSVC found ANDOR
>   ANALOG's own `product/Source/FxRouting.h` first (it looks beside the including files before
>   the include paths): `RoutingKey` undeclared. OSP 5cc6fbed includes its own by path.
> - **Windows build stalled, found and fixed:** MSVC could not compile one test case, "SATELLITE:
>   switched on and off mid-note" (its nested lambdas, one taking the 9 KB `Patch` by value);
>   every other case and file compiles in seconds. Found by compiling each engine test file
>   alone, then that file's cases alone (`tools/ci/compile_each_case.py`, kept in the Windows
>   job for any file that stalls). The case is now plain helper functions, the same checks
>   (passes here). The Windows job builds, tests and uploads the product and its installer first,
>   then the engine's tests; it stops after 90 minutes rather than six hours.
> - **The Custom Patterns browser in a short window:** a window shorter than the instrument (a
>   laptop's screen, or a host keeping its height) scrolls it, and the browser, opened under the
>   ARP panel, ran below the window's edge, half out of reach. Opening a popover now scrolls it
>   into view once (`frame::revealPopover`, OSP's shared frame: ANDOR ANALOG's shell and ANDOR
>   OSP's editor alike); the musician's own scrolling is left alone after. New test on every card
>   in a scrolled window (fails without the change); OSP has the same test for its editor.
> - **The click test on Windows:** it handles every waiting message after a click (the editor's
>   timers kept more than 200 queued by card C, and the browser's opening was not reached).
> - **Two product tests on Windows (test-only):** the GLOBAL MORPH probe kept a 126 KB
>   `ShellProcessor` on the stack in each of its sections and overran Windows' 1 MB stack (a host
>   always makes the processor on the heap); the test's processors are now on the heap (it
>   passes here under a 256 KB stack, where it failed before). The effective-value picture test
>   differed by 3 anti-aliased pixels of ~400,000 on MSVC (80 % modulated is 80 within 0.01):
>   it allows 8.
> - **macOS:** the two new click tests (ANALOG's and OSP's) could not pump the message loop
>   there (the browser opens a message after the click): ANALOG's is skipped on macOS, OSP's
>   returns early as its other message-loop tests do (OSP 31aed041). Linux and Windows run them;
>   on macOS the clicks are an OWNER TEST in the hosts.

> **20 Nov 2026, D's keys show their state (owner report):** with D on, its ARP key and FOLLOW
> A B C lit as a faint pressed key in D's purple; they now light as the arpeggiator panel's own
> keys (OCTAVES, EDIT): the warm accent face, the word in the accent. D's power key: its glyph in
> the accent and a fine accent ring while on (no glow, at the owner's request), "ON" in the
> accent. New test (`[auxarp][ui]`): the keys' active state follows ARP and FOLLOW in KONTAKT and
> NOISE; the ring is drawn on and gone off.

> **20 Nov 2026, Custom ARP merged into the main integration line (`claude/osp-parity-r1`, OSP
> pin `c2a421a` on `claude/analog-parity-r1`; the owner's go-ahead):**
> - `claude/osp-parity-r1` now carries everything on the Custom ARP line (fast-forward): Custom
>   Patterns R2 on cards A, B and C (CUSTOM hint 41, `<CustomPatterns schema=1>`), with STRUM, the
>   menus and the rest of R1. OSP's `claude/analog-parity-r1` moved the same way.
> - **Custom Patterns open when clicked:** the "Custom Patterns ›" row opened the browser and
>   closed it in the same click (the owner saw this on OSP's beta). Fixed in OSP's shared code
>   (its own fix, 9ff21209, taken). New ANALOG test, by click on every card in a real window
>   (Xvfb) and through JUCE's routing: the row opens the browser and it stays open; + New Pattern,
>   a factory pattern, Edit and the pencil each open what they should. It fails without the fix.
> - The 1.0.0 RC (`main`, `claude/release-candidate`) is untouched.
> - **Owner test:** the same clicks in Logic, Live and Reaper, on macOS and Windows.

> **20 Nov 2026, Custom ARP R2 on the stacked line (branch `claude/osp-parity-r1-custom-arp`, OSP
> pin `c63339e` on `claude/analog-parity-r1-custom-arp`; docs/OSP_PARITY_AUDIT.md, Gate 4):**
> - **R2 in ANALOG:** OSP's Custom Pattern redesign (6ef33767), cherry-picked alone; the beta's
>   release work, MIDI Learn and its POS-line fix are not taken. One "Custom Patterns ›" row with a
>   browser on every card (A, B, C), the grid-first editor, whole-document undo and the discard
>   question. Engine, file format, state, parameters (CUSTOM hint 41) and factory patterns are as
>   before.
> - **Fixed in OSP (shared):** the editor and browser titled card B "CARD C" (ANALOG shows its
>   layers A, C, B). ANDOR OSP is unchanged.
> - **Tests:** the every-control test covers the three CUSTOM keys; a new R2 browser test checks
>   each card's letter and its own pattern. 263 cases (3 skips: Bukkehorn recordings, the two
>   real-window menu tests, which pass under Xvfb); visual 0 px; every OSP suite.
> - **Not verified here (OWNER TEST):** DAW hosts, listening.
> - **Merged into `claude/osp-parity-r1` later the same day** (entry above); not on the 1.0.0 RC.

> **20 Nov 2026, OSP synchronization and feature parity R1 (branch `claude/osp-parity-r1`, OSP
> pin `claude/analog-parity-r1`; docs/OSP_PARITY_AUDIT.md, docs/OSP_DEPENDENCY_UPDATES.md):**
> ANDOR ANALOG on OSP's approved line (its default: STRUM, the ceramic menus, USER MACROS, the
> wheels, the shared FX library), plus the shell's preset saving brought up to OSP's.
> - **Baseline (Gate 0):** `feature/aux-live-input` e3a8090 (OSP b522a06), 242 product cases
>   green; schema, V6 renders and 141 screenshots kept.
> - **STRUM:** shared, on every card (A `arp.strum.*`, B / C `analog.arp.cardB|C.strum.*`,
>   version hint 40: above ANALOG's own 39), off by default and in older sessions. New ANALOG
>   tests: every direction, FREE and SYNC, A / B / C unlinked and linked, held-note changes,
>   sustain, transport stop, recall, the key in ANALOG's ARP panel.
> - **Menus:** the shared ceramic look on every menu (OSP's SharedMenuLook, held by the shell's
>   editor) and the shared icons on ANALOG's own menus too (sound key, AUX, FX presets, FX
>   routing gear / group / tile, sound import); destructive actions in rust.
> - **Presets (shared shell):** SAVE updates only the musician's own preset (a factory preset
>   asks for a name); Save preset / Save preset as...; the list shows "Factory" first, then
>   "Your presets"; removing the recording asks once more.
> - **FX routing:** the pedal light follows the effect's real state (OSP's shared fix, ported to
>   ANALOG's own routing view, which keeps MAIN + SEND).
> - **Kept as ANALOG's own (not switched):** FX routing / rack / presets, SUB, the SATELLITE panel
>   and the USER MACRO UI, because OSP's shared copies predate MAIN + SEND, RETRO 100 % and SUB
>   vibrato. The convergence plan is in the audit, section 5.
> - **Custom ARP Patterns:** R2 is not in OSP yet. CP3 / CP4 are prepared and tested on the stacked
>   branch `claude/osp-parity-r1-custom-arp` (OSP `claude/analog-parity-r1-custom-arp`): CUSTOM
>   per card (hint 41), the shell's `[arp-custom]` passing in ANALOG, `CustomArpParityTests`.
> - **Macro lenses:** ANALOG keeps its own (OSP re-rendered its CORE lenses in a new palette);
>   its walnut pack carries the originals (`tools/add_macro_lenses.py`).
> - **Verified (Linux):** `andor_analog_tests` 255 cases (261 on the Custom ARP line), visual 0 px,
>   every OSP suite; final report: docs/OSP_PARITY_R1_REPORT.md.
> - **Release metadata:** CI checks the OSP pin is on an OSP branch or tag and writes
>   BUILD-INFO.txt (ANALOG commit, OSP pin) beside the test build and the installer.

> **19 Nov 2026, the tests on OSP's current line (OSP pin `claude/shell-window-once`, `aef5e0d`,
> OSP's default line plus one shared fix):**
> - **Why:** `andor_analog_tests` did not compile against OSP's current line. OSP's Shared FX
>   and SATELLITE work added shared headers with the names of ANALOG's own (`FxPresets.h`,
>   `FxRack.h`, `FxRouting.h`, `FxIdentity.h`, `SatellitePanel.h`, plus `EqSpectrum.h` and
>   `FxParameters.h`), and OSP's include path comes first.
> - **The tests now include ANALOG's own copies** (`../Source/...`). They test the FX ANALOG
>   ships: ANALOG keeps its own FX and SUB until it adopts the shared ones (OSP STATUS, NEXT).
> - **Tests brought up to date with shared features:**
>   - "every control moves the sound" hears STRUM's fifteen parameters (CHORD with STRUM on;
>     SYNC's division in quarter steps).
>   - ANDOR OSP's own housing now draws a SUB row (SUB is shared).
>   - The parameter-schema golden gains the ten card B / C STRUM lines, appended, nothing else.
> - **A shared bug found and fixed in OSP:** showing the AUX strip asked the host for the same
>   window height twice, so a host answering the old size first granted it at once. The shell
>   test "SPACE + ARP + a strip opened together ..." caught it here; ANALOG is the only product
>   with a strip.
> - **Not in this pin:** the D2 macro knob (`claude/analog-d2-macros`, OSP `claude/analog-pin-d2`)
>   stays on its own line.
> - **Verified (Linux):** `analog_dsp_tests` (321 cases); `andor_analog_tests` (240 passed,
>   2 skipped).

> **16 Nov 2026, Learn: every lesson ends in the tutorials; the Quick Start ends on INFO (OSP
> and Analog, shared `shared/andor-learn`):**
> - A finished lesson opens the tutorials, with its tick, instead of the "Lesson complete" note.
>   A lesson that played on its example first asks Keep / Restore, then the tutorials open.
> - The Quick Start's last step (`andor::learn::findLearnAgainStep()`) points at the INFO key.
>   Its text:
>   - the i in the corner always brings back Learn mode;
>   - every tutorial is under the three-dot menu, in Learn.
> - `analog.quickstart` is revision 5, nine steps: AUX D, then this step.
> - OSP pin: `claude/analog-learn-end` (`b522a06`), which is `4489e33` plus that shared change.
> - Verified (Linux): `andor_analog_tests` (the Learn cases included).

> **16 Nov 2026, compact bottom edge (OSP and Analog, shared; docs/BOTTOM_EDGE.md,
> docs/screenshots/bottom):**
> - Both editors end 36 reference px sooner. The housing's foot is 22 px under the keys, about
>   20 px under the PITCH / MOD captions; surround and shadow are as before.
> - Analog is 1448 × 986 (was 1022); OSP is 1448 × 1050 (was 1086).
> - Nothing above the keys' foot moved or was scaled.
> - The "Ready" footer is gone. Load failures, open / save results and sound warnings appear
>   once in the overlay's existing brief notice.
> - Anchored popovers are now also kept above the instrument's foot (`placePopover`). The SHAPER
>   DNA panel would otherwise have run past the shorter window.
> - Verified (Linux):
>   - Analog plugin tests: 4 / 4 shards (241 cases, the usual host-only skips).
>   - OSP plugin tests: 4 / 4 shards (171 cases).
>   - Both bottom-edge tests pass: geometry, no footer label, failures and messages shown as
>     notices.
>   - Visual regressions: OSP's reference, the example product's and Analog's are all 0 px
>     outside the masks.
>   - DAW / AU / VST3 window sizing and the Mac standalone are UNVERIFIED (OWNER TEST).

> **16 Nov 2026, keyboard keys D / SETTINGS / INFO (`feature/aux-live-input`;
> docs/KEYBOARD_UTILITY_ACTIONS.md, docs/screenshots/keys):**
> - The large Advanced key beneath ARP and SUB is now three small ceramic keys, shared with
>   OSP (`KeyboardUtilityActions`):
>   - **D** shows or hides AUX D. It changes only the view: the sound and every parameter are
>     identical (tested).
>   - **SETTINGS** opens and closes Advanced, which is anchored to the key.
>   - **INFO** turns Info on and off; it is green while on. Pointing explains a control, and
>     clicks keep working.
> - Show / Hide AUX, Advanced settings and Toggle Info are gone from the menus. Learn is now
>   Tutorials… and Reset Tutorials….
> - Learn changes:
>   - Quick Start (revision 4, eight steps): "Show AUX D" outlines the D key and completes on
>     the musician's own click. The next step outlines the strip.
>   - Use external audio (revision 3) starts the same way.
>   - When D is hidden and silent, the MORPH and LIVE INPUT recovery says "Show AUX D" on the D
>     key. Its fix shows the strip only; then the real problem is outlined.
>   - The keys have Info texts.
> - MOD WHEEL, PITCH BEND and ADVANCED use the compact SUB / MOVEMENT popover proportions.
> - Verified (Linux):
>   - Analog plugin tests: 4 / 4 shards pass (241 cases; the 4 skips are the usual
>     host-only ones), including the Learn and utility-key cases.
>   - analog_dsp_tests: 321 cases pass.
>   - Visual regression: OSP's, the example product's and Analog's are 0 pixels outside the
>     masks. The key row is a new approved product mask, `keyboard-actions`.
>   - DAW, AU / VST3 and Mac standalone checks are UNVERIFIED (OWNER TEST).

> **15 Nov 2026, PITCH / MOD wheel popovers and VIBRATO (`feature/aux-live-input`;
> docs/WHEELS_VIBRATO.md):**
> - The captions under the wheels open their settings (PITCH BEND: RANGE; MOD WHEEL:
>   VIBRATO). The MOD caption is no longer clipped, and a drag from it still makes a cable.
> - VIBRATO is per voice, on OSC 1, OSC 2 and card C pitched, with SUB as an optional source
>   (off by default). It is never post-FX.
> - CONTROL is MOD WHEEL / ALWAYS ON / ASSIGNED; ASSIGNED uses AMOUNT, 0 % by default (owner's
>   decision). VIBRATO DEPTH and AMOUNT are modulation and USER macro destinations.
> - It is off by default and bit-identical when off; factory presets are unchanged.
> - Demos are in docs/demos/wheels and screenshots in docs/screenshots/wheels. Listening and
>   DAW checks are OWNER TESTS.

> **15 Nov 2026, GLOBAL MORPH 2.0 (`feature/aux-live-input`; docs/GLOBAL_MORPH_2.md):**
> - Gate 0 audit and gate 1 (D input reliability) done. D's path is measured stage by stage
>   (source, FILTER, LEVEL, MORPH in / out, FX in; `Synth::AuxTelemetry`), and every source
>   mode reaches the stage. The "D is silent" message is replaced by D's real state (WAITING
>   FOR MIDI / TRANSPORT, NO INPUT, MUTED, OFF, SIGNAL ACTIVE).
> - Fixed: MORPH TO D at 100 % was not D. The synth came back between a loop's hits, because
>   the position was scaled by D's level. Each of the four modes is now D exactly at 100 % and
>   the synth exactly at 0 %.
> - Owner's decisions on D's CARRIER, OUTPUT (LAYERED) and LEVEL MATCH are recorded.
> - Gates 2–10 implemented (docs/GLOBAL_MORPH_2.md):
>   - MORPH TO D runs a new global STFT stage: IMPRINT, FUSE and WEAVE, with TEXTURE, ATTACK,
>     FORMANTS, DETAIL, CARRIER (per-voice carrier tap), OUTPUT and LEVEL MATCH, all on D's own
>     `analog.morph.global.*` parameters (MORPH's position stays shared).
>   - Exact ends after the stage's reported latency (1024 samples at 44.1 / 48 kHz), and only
>     while MORPH TO D is chosen.
>   - The rack has C's layout with D's source and state in ROOT's place. The graph draws only
>     measured data. The MORPH popover follows MORPH TO.
>   - Renders are in docs/demos/gm2/examples; screenshots in docs/screenshots/gm2.
>   - Listening, real recordings and DAW behaviour are OWNER TESTS.

> **11 Nov 2026, feature branches (not in the 1.0.0 release candidate):**
> - SHAPER DNA V1 (`feature/shaper-dna-v1`; docs/SHAPER_DNA_V1.md);
> - USER MACROS V1.1 gates 1–4 with the CORE / USER folder tabs (`feature/user-macros-v1`,
>   stacked on DNA; docs/USER_MACROS_V1_1.md).
>
> - SATELLITE + RHYTHMIC MORPH, gates 0–8 (`feature/satellite-rhythm` in both repositories,
>   stacked on USER MACROS; docs/features/SATELLITE_RHYTHM_ARCHITECTURE.md):
>   - SATELLITE: each card's own stereo motion (PAN / ORBIT / BOUNCE, SYNC / FREE), its key and
>     panel in the per-card arpeggiator's editor;
>   - RHYTHMIC MORPH: card C's recording analysed for its rhythm and looped on the host's grid,
>     its pitch kept (AUDIO / MOD / BOTH);
>   - C RHYTHM: a 13th modulation source, with its panel in card C's well.
>
>   State version 21; off is bit-identical (the legacy Init hash). Demo renders are in
>   docs/demos/satellite-rhythm. Open decisions are listed in that document.
>
> - EXPERIMENT, Kontakt as card C's external SoundSource + GLOBAL MORPH
>   (`feature/kontakt-external-morph`, stacked on SATELLITE + RHYTHM;
>   docs/experiments/KONTAKT_EXTERNAL_MORPH.md). A VST3 host bridge, card C's MIDI lane, a global
>   C bus and a zero-latency spectral imprint morph, validated with a stand-in VST3. Kontakt and
>   DAWs are BLOCKED here. Recommendation: CONDITIONAL.
>   - Fixes after the first test on a Mac:
>     - Kontakt's bus now goes through the cards' FILTER (TONE, RESONANCE, DRIVE, model, its
>       envelope);
>     - SATELLITE's and RHYTHM's panels open: they used to close in the same click (OSP shell);
>     - MORPH acts in RHYTHM's AUDIO / BOTH, as a GLOBAL MORPH with the loop;
>     - the arp / sub lights lost a seam cross at fractional display scales (OSP skin).
>
> - D / AUX, gates 0–8 (`feature/aux-d` in both repositories, stacked on the Kontakt experiment;
>   docs/aux-d/AUX_REPORT.md):
>   - an optional strip under cards A / B / C, shown or hidden from the main menu (a view
>     setting: kept with the session, never with a preset);
>   - D's own stereo source: RHYTHM (a host-synced loop), NOISE (white / pink / brown, keyed or
>     free) or KONTAKT (its own instance, the musician's keys before the arpeggiator);
>   - through the FILTER (it follows the FILTER macro and rack) and into the shared DRIVE /
>     MOVEMENT / ECHO / SPACE and the FX route; mute / solo / power ramped; latency reported;
>   - a full-width AUX rack (double-click, `...`), Escape closing one level;
>   - `analog.aux.*` (version hint 37), state version 23; D off is bit-identical.
>
>   Card C HYBRID is unchanged. MORPH behaves as Option A until the owner decides
>   (docs/aux-d/MORPH_DECISION.md: OPEN). Real Kontakt and DAWs: UNVERIFIED (OWNER TEST).
>
>   AUX Refinement Addendum 03 (docs/aux-d/ADDENDUM_03.md):
>   - drops on D's strip or rack load D only, never card C (the shell's DropOwner);
>   - D always goes through the FILTER (ROUTE removed; the old switch is hidden and unread);
>   - M / S are the cards' own keys;
>   - D's well sits at card B's well's place and width; RACK sits beside the menu; the mode
>     selector is fixed at the top left;
>   - the wells load on a click or a drop (no LOAD buttons); a failed load keeps the loop;
>   - RHYTHM PITCH / OCT re-pitch the slices on the grid, the tempo and the length unchanged.
>
>   Then, at the owner's word:
>   - D MORPH, Option D (docs/aux-d/MORPH_DECISION.md: DECIDED): a second, global MORPH stage,
>     A/B/C (after the FILTER) × D. Its own control (`analog.aux.morph`, 0 % = off; the AUX rack,
>     a modulation destination, USER macros). It acts only while D sounds; the MORPH macro is
>     unchanged.
>   - A file dropped on empty space loads nothing.
>
>   Then (docs/aux-d/ADDENDUM_04.md):
>   - D RHYTHM's GATE (`analog.aux.rhythm.gate`, KEYS by default): a key starts the loop from
>     its first beat, it plays while a key is held and fades with the AMP's release; FREE is
>     the old always-playing behaviour. RATE now runs the right way (2x = twice as fast), and a
>     recording with no beat grid found is still taken as a loop (whole bars), so RATE works.
>   - The window never asks for more height than the screen holds when the arpeggiator or a
>     rack opens (shared: ANDOR OSP too); it narrows to fit and comes back.
>   - Addendum 04: one fixed strip layout - SOURCE MODE and LEVEL permanent after POWER, the
>     display after LEVEL, only the mode's knobs changing, RACK and the menu at the right;
>     KONTAKT NOT LOADED once; OFF never drawn over the display.
>   - Right-click on a knob opens Assign to Macro in a host (the press reached the knob, not the
>     halo overlay holding the menu; shared fix, ANDOR OSP too).
>   - Right-click, second fix (the owner: still no menu in the Addendum 05 build). Reproduced in
>     the standalone app with a real right-click: with the right button down the halo overlay
>     claims the press, but its mouseDown only looked for modulated knobs, so an unassigned knob
>     opened nothing. Its mouseDown now looks for any destination on a right-click, as its
>     hitTest does. Checked in the real standalone (menu, Assign to Macro, Macro 1 assigned);
>     test `[rightclick]` routes the press as JUCE does (fails without the fix).
>   - The macro tab changes only when CORE / USER is clicked (the owner): Assign to Macro from a
>     control's menu and dragging a range on a halo no longer switch to USER; the chosen macro's
>     range and handles show on the halo on either tab (`[banktab]`). Show Macro Assignment
>     Details still opens USER (it shows the macro's own panel there).
>   - The AMP envelope's picture follows A D S R's modulation (the owner: VEL on DECAY was heard,
>     not seen): while a note sounds it draws the shape that note plays - routes and USER macros,
>     the halos' own reading (`ModulationOverlay::livePosition`) - with the set shape dashed
>     under it, carrying the handles; nothing sounding or nothing routed: the set picture,
>     unchanged to the pixel (`[ampenv]`, both instruments).
>   - ARP and SUB in capitals on the stacked ARP / SUB keys (the owner); ANDOR OSP's single key
>     (no companion) unchanged.
>   - ANDOR Learn V1 merged (`feature/andor-learn-v1`) and brought up to date: Quick Start and
>     MORPH revised (C1 / C2, CORE / USER, ARP and SUB, AUX D; MORPH's lamp, MORPH TO), two new
>     lessons (Make your own macro; Play your DAW through D), 21 help areas (docs/LEARN_V1.md).
>   - Card C's second layer: a recording dropped on card C while C2 shows loads into C2 (it
>     went to C1's recording, out of sight) - `Engine::takeRecording` (`[c2]`).
>   - A preset loaded with AUX open: card C stays a card and D's knobs stay in C's zone (a state
>     load now tells the engine its recording is gone; the strip takes C's column from A's and
>     B's) - `[presetlayout]`.
>   - RETRO fully wet: MIX 100 % by default and in all 13 factory presets; a RETRO added starts
>     as Warm Tape I at 100 %. Sessions keep the MIX they stored.
>   - The window's width is never changed by opening anything (only its corner): the height
>     follows, capped at the screen, where the instrument scales inside (both instruments).
>   - Each card's EQ shows the live spectrum of what plays through it (ANDOR OSP's card EQ
>     picture), tapped only while it shows (`[card-eq-spectrum]`).
>   - Vertical-only expansion (the owner's spec; OSP STATUS): racks, the arpeggiator, AUX D
>     change only the height; the width, scale and every horizontal place stay. A window the
>     host or the screen keeps shorter scrolls vertically at the same scale - the instrument is
>     never shrunk or centred with side margins. Geometry tests run for ANDOR Analog too
>     (FILTER, MORPH, AUX with the arpeggiator and a rack). DAWs: UNVERIFIED (OWNER TEST).
>     Refined: the window always grows first. Scrolling appears only once a host has kept the
>     window short after being asked twice, or when the screen itself is too short; a host
>     reporting the old size mid-resize is waited for, not taken as a refusal. SPACE + ARP +
>     AUX opened together grow the window to the full instrument (shell test
>     `[window][geometry][host]`, run with ANDOR Analog); the layout screenshots' refused state
>     goes through the same negotiation.
>   - Quick Start's text: step 3 "The USER tab allows for 6 custom macros"; step 4 (ARP and
>     SUB) as the owner wrote it.
>   - ANDOR Learn 2.0 (docs/LEARN_2.md): one Learn menu entry (Tutorials..., Toggle Info, Reset
>     Tutorials...); a compact browser (START HERE, LESSONS, Resume · Step n of N, checks); no
>     Skip anywhere; a green outline on the exact control of every step (MORPH TO's C / D
>     segment, PLAYS A / B, EDIT B, OSC 1 LEVEL, USER Macro 1, AUX's source dropdown, one FX
>     block); notes placed beside it, clear of the panel it is in. All six lessons and the 21
>     Info texts rewritten (the owner's copy). Lessons check the real sound before listening:
>     card C / D playable, muted, at zero, a loop or Kontakt loaded, sidechain audio actually
>     arriving; what is missing is named, outlined and offered as a fix or an example sound.
>     Three bundled example sounds (oscillators and noise only), loaded only when chosen, the
>     sound before them kept for Restore Previous Sound. 23 Learn tests; screenshots of every
>     step at 1448 and 869 px. A DAW run-through by hand: OWNER TEST.
>   - Universal modulation-aware visual feedback (OSP docs/EFFECTIVE_VALUES.md, with its
>     inventory): the pictures draw what plays. AnalogEngine publishes every value its block
>     moved (USER macros, global routes: MOVEMENT, DRIVE, SPACE, ECHO, MIX, every global product
>     destination) and each per-voice product destination as the newest sounding voice plays
>     it. Following it: MOVEMENT (both lanes, every mode), DRIVE, ECHO, SPACE, the FILTER rack
>     (resonance, drive, bias, ...), MORPH, SHAPER DNA, the oscillator cards' waveforms, the
>     halos' live dot (summed routes, clamps, span trail) and the header MIX node. The knobs
>     keep their stored values; presets keep the knob. Tests `[effective]` (SHAPER lanes A/B
>     pixel-identical at a modulated and a hand-set 80 %; OSC 1 WIDTH per voice; two routes
>     summed and clamped). OPEN: ANDOR OSP's layer destinations (grains, amp ADSR) on cards A
>     and B; DAW checks UNVERIFIED (OWNER TEST).
>   - D ARP (docs/aux-d/D_ARP_FOLLOWER.md): in KONTAKT and keyed NOISE, ARP on / off and FOLLOW
>     A B C on the strip before RACK and in the AUX rack. D plays one arpeggiator lane - LINK on:
>     the shared pattern; LINK off: the card chosen, even while that card's own arpeggio is off -
>     the same notes at the same samples as the lane (`[auxarp]`: D's Kontakt and card C's own
>     instrument compared event for event). A / B / C are given exactly what they were (output
>     bit-identical with D muted). Off, the arpeggiator off, RHYTHM or LIVE INPUT: the keys as
>     before. No note hangs on FOLLOW / ARP / LINK / mode / power changes or a state load.
>     `analog.aux.arp`, `analog.aux.arp.follow` (appended, off by default: older sessions open
>     with it off). Shared: an opt-in follower at the per-card arpeggiator's delivery
>     (ProductInfo::arpFollower, Engine::setAuxNotes); ANDOR OSP has no D, so nothing runs
>     there. Real Kontakt (a spiccato patch) and DAWs: UNVERIFIED (OWNER TEST).
>
>   Addendum 05 (`feature/aux-live-input`, docs/aux-d/ADDENDUM_05.md):
>   - D LIVE INPUT: the DAW's sidechain ("AUX D Live Input", a VST3 auxiliary bus, off by
>     default; the plugin stays an instrument) as D's source - no MIDI needed, through D's LEVEL,
>     MUTE / SOLO, the FILTER and the shared effects; level history and input state in the strip
>     and the rack. ANDOR Analog's own VST3 hosted in-process proves the bus and the audio path.
>   - MORPH TO (C HYBRID / D) in the MORPH rack replaces the D MORPH knob: with D, the MORPH macro
>     drives the global GLOBAL IMPRINT stage (0 % the synth, 100 % D, D never layered too); one
>     processor at a time. Six demos with synthesized stand-in tracks.
>   - Unverified: Ableton, Logic (AU sidechain, auval), REAPER - owner tests.
>
> - Destination-first macro assignment (docs/MACRO_DESTINATION_FIRST.md):
>   - right-click any eligible control → Assign to Macro (names, checkmarks, range, details,
>     removal);
>   - the USER macros' ranges drawn by the modulation halos (quiet arcs; the focused macro's
>     range as the ring), with draggable 0 % / 100 % handles, reverse ranges, Shift fine,
>     Escape, one undo step;
>   - a visible default range with no jump;
>   - modulation's base includes the macros (the engine's order).
>
>   OSP unchanged (opt-in hooks).
>
> Next: USER MACROS gate 5 (FX-library slot targets, orphan / reconnect), its CPU figures and a
> draft PR. Not run here: hosts, real input, listening (SATELLITE and RHYTHM included).
> **11 Nov 2026: ANDOR Learn V1** (branch `feature/andor-learn-v1`, from 1.0.0 `main`; OSP
> `feature/andor-learn-v1`). Guided lessons are opt-in:
> - Quick Start, Make a moving pattern (ARP, 8 steps), Blend with MORPH, See the effect chain.
>
> There is also a contextual Learn mode with 18 help regions, a topic browser, a one-time
> invitation and per-user progress. Interface only: no parameter, state or DSP change.
> Learn OFF renders pixel-identical (724 / 724 baseline screenshots).
> Details: docs/LEARN_V1.md, docs/LEARN_V1_REPORT.md. DAW checks still to do.

> **9 Nov 2026: 1.0.0 release candidate** (branch `claude/release-candidate`;
> docs/release/READINESS_SCORECARD.md). On the baseline (`main` a25c270, OSP 3d949cd), the full
> suite passed 13/13 on Linux. Release fixes:
> - the arpeggiator no longer allocates on the audio thread (OSP `claude/analog-release`);
> - only the 831 parameters the engine reads are offered for automation (128 inert ones are not);
> - a factory update no longer deletes the musician's files in `Presets/Factory`;
> - frozen contracts: a modulation-destination golden and real 1.0 session fixtures;
> - one version source (`product.json`, 1.0.0).
>
> Packaging: notices, an EULA draft, a signing-ready `.pkg` and Inno Setup installer in CI,
> static SQLite on Windows with a DLL check, and pluginval in macOS CI. The header below
> (30 October) and some rows are older than the code; where they differ, the scorecard is current.

_30 October 2026: state version 4, the unified expansion (docs/UNIFIED_ANALOG_EXPANSION_PLAN.md).
Each slot can be CLASSIC, DCO or WIDE. There are now OTA filters, a high-pass, a chorus, Juno
modulation, ORGANIC (one recording beside the oscillators) and MORPH (IMPRINT, FUSE, WEAVE).
Built and tested on Linux x86-64 (GCC 13, Xvfb): every suite plus ASan/UBSan on the engine.
macOS and Windows: CI. No DAW session, pluginval or formal listening test yet. A listening
package was rendered (20 tracks)._

| Area | State | Evidence |
|---|---|---|
| Engine: oscillators (saw, pulse, triangle; B-spline BLEP/BLAMP; free-running phase) | ✅ done and tested | `analog_dsp_tests` [osc]: pitch < 0.1 cent at 44.1/48/96 kHz; aliasing table in AUDIBLE_QA.md |
| Hard sync with sub-sample reset correction | ✅ done and tested | [osc] sync on the master's grid, −76.8 dB at 4x |
| Poly Mod (filter env + OSC 2 → OSC 1 pitch, OSC 1 width, cutoff), audio rate | ✅ done and tested | [modulation]: every route moves the sound; amounts without destinations do nothing |
| Four-pole ZDF ladder with saturation, resonance to self-oscillation, FILTER DRIVE / BIAS / TRIM | ✅ done and tested | [filter] |
| Two ADSRs (amplitude: ANDOR OSP's AMP parameters; filter: its own) | ✅ done and tested | [envelope] |
| LFO: ANDOR OSP's LFO 1 (7 shapes incl. RANDOM and a drawn CUSTOM, rate or sync, phase, polarity, free / retrigger / one shot, global or POLY), its amount and the mod wheel | ✅ done and tested | [modulation], [live][lfo]; the modulation panel's LFO 1 page; sync not yet tested against a running host transport |
| Modulation: ANDOR OSP's panel (LFO 1 and 2, ENV 1 and 2, VEL, NOTE, wheel, CC 1-4 with Learn, pressure; 16 routes, depth routes, curves, APPLY TO AMP) to every continuous control: 72 of ANDOR ANALOG's own (per note or the instrument's) and the 16 of ANDOR OSP's it plays; drag a source onto any knob | ✅ done and tested | `analog: every modulation destination it offers moves the sound`; [modulation] in analog_dsp_tests (per note VEL, controllers, reset, APPLY TO AMP); no allocation ([realtime][modulation]); CPU unchanged within noise (24.7-25.8 % of a VM core, 16 voices) |
| AMP envelope picture follows its knobs and automation (it redrew only when its dots were dragged) | ✅ fixed | the shell's editor repaints it when attack, decay, sustain or release move, as ANDOR OSP's does |
| Arpeggiator: ANDOR OSP's (18 patterns, 10 rates, gate, octaves, swing, velocity, host sync, hold with the pedal, its display), in front of the synth; the ARP destinations | ✅ done and tested | `shell: an engine that binds arp.* is played by ANDOR OSP's arpeggiator`; the bindings test (every ARP control heard) |
| The per-card arpeggiator (the shell's, opt-in here): PLAYS A B C picks the cards the arpeggio plays while the others hold the keys (e.g. A and B sustain the chord, C arpeggiates it); LINK off gives each card its own PATTERN / RATE / OCTAVES (EDIT A / B / C), so one card can play chords, another UP/DOWN, a third hold. The synth plays only a note's cards (its channel names them) and skips the oscillator pair on a card-C-only note. Defaults (LINK, every card) are the one arpeggiator as before | ✅ done and tested / ⚠️ seen only in screenshots (`analog-arp-card-c`, `analog-arp-unlinked-b`) | [cardarp] (C alone with A+B held; unlinked UP vs DOWN and B at twice A's rate; LINK with every card = one arpeggiator; eleven changes mid-chord leave nothing hanging); the bindings test (ten new parameters); CPU (one core, 48 kHz, a 4-note chord, 1/16, C as OSC 3): linked 10.8 %, C alone with A+B holding 16.5 %, three unlinked lanes 16.0 % (arpeggiator off 9.8 %) |
| GRANULAR on the card: POS, the SPREAD pane and every playing grain (where it reads, its window, its row), as ANDOR OSP shows them over a recording | ✅ done and tested / ⚠️ seen only in screenshots | `analog: GRANULAR's grains are shown on the card ...`; `shell: a generator engine's grains reach the card` |
| GRANULAR on the oscillators: each voice records its own sound (1.5 s) and plays ANDOR OSP's grains from it (POS how far back, SIZE, DENS, TUNE, SPREAD), with KEY DNA | ✅ done and tested | [live][granular]: speaks at once, sustains, every setting heard, TUNE +12 is an octave, extremes finite, KEY DNA neutral when off |
| GRANULAR per oscillator card (A and B independent; REGULAR / GRANULAR selector, fitted to its names; the oscillator overlay hidden in GRANULAR; KEY DNA on A, shaping both) | ✅ done and tested / ⚠️ seen only in screenshots | `analog: every control the interface enables moves the sound` (layerC.granular.*); the v5 KEY DNA migration test; legacy contours within 0.1 % (hashes updated for the 6 GRANULAR / KALEIDOSCOPE renders); `three-1448-9-granular-*` screenshots |
| GRANULAR's controls modulated (LFO, ENV, VEL, ...) on both oscillator cards: card A's POS SIZE DENS SPREAD (ANDOR OSP's layer A destinations) and GRAIN TUNE, card B's (layer C) and GRAIN TUNE B; a source dragged onto a Granular card's display brings its grain knobs up to drop on | ✅ done and tested | `analog: every modulation destination it offers moves the sound` (card B's included); `analog: a modulation source dragged onto a Granular card reaches its grain controls (cards A and B)` |
| SPACE's ABYSS engine (ANDOR OSP's: a vast modulated FDN with GRAVITY, FREEZE, RESONANCE; its variants DEEP / DARK / INVERSE / ENDLESS as its tabs, `three-1448-10-space-abyss-inverse`, `-endless`), read from space.engine / space.abyss.*; chosen in the SPACE popover's list under the rooms (FLOW / FREEZE, GRAVITY, RESONANCE, its own picture) | ✅ done and tested / ⚠️ seen only in screenshots (`three-1448-10-space-*`) | ANDOR OSP's [abyss] tests; `analog: every control the interface enables moves the sound` (GRAVITY, FREEZE, RESONANCE heard on ABYSS) |
| SPACE's VINTAGE (an early-1980s digital hall: low internal rate, short words, coarse coefficients; HALL / PLATE / CHAMBER / ROOM, ERA, DIFFUSION) and BLOOM (a diffusion delay, tempo-synced: CLEAN / VINTAGE / TAPE / WASH, DIFFUSION), ANDOR OSP's, in the same list | ✅ done and tested / ⚠️ seen only in screenshots (`three-1448-10-space-vintage-plate`, `-bloom`, `-bloom-wash`) | ANDOR OSP's [vintage] and [bloom] tests; `analog: every control the interface enables moves the sound` (each engine's own controls heard on it) |
| ECHO's ASCEND (ANDOR OSP's: a pitch shifter in the delay's loop, its splices aligned in phase so the interval stays in tune; INTERVAL +12 / +7 / +5 / -12, MOTION RISE / STEADY / LATE, AGE shown as CLOCK), read from echo.type / echo.ascend.*; chosen in the ECHO popover's list after TAPE and BBD, with its own second row and the repeats' pitches in the picture (`three-1448-11-echo-ascend`, `-steady-fifth`) | ✅ done and tested / ⚠️ seen only in screenshots | ANDOR OSP's [ascend] tests; `analog: every control the interface enables moves the sound` (INTERVAL and MOTION heard on ASCEND) |
| SPACE's presets (ANDOR OSP's shared popover): the tabs' row is a `‹ NAME ›` bar, 38 presets by category (ROOMS, HALLS, PLATES, SPRINGS, ABYSS, VINTAGE, BLOOM), the variants in the header's list; presets set only SPACE's existing parameters (sessions unchanged), the name worked out from the values | ✅ done and tested / ⚠️ the presets not yet voiced by ear | ANDOR OSP's `[presets]`; `three-1448-10-space-*` screenshots |
| REIMAGINED on the live sound: KALEIDOSCOPE (saturation, doubling, remapped grains; ANDOR OSP's resonator and formant stage on the mix, tuned to each phrase's first note), TAPE FRAME, TOYBOX, MIRAGE (ANDOR OSP's own engines, unchanged); MOSAIC needs a recording's analysis and leaves the sound as it is (its popover says so) | ✅ done and tested | [live][reimagined] in ONE SHOT and GRANULAR; each mode's settings heard; 0 % is the plain sound |
| MOVEMENT: DRIFT in each voice (ANDOR OSP's wander of pitch, tone, level, place) and TAPE, CHORUS, PULSE, SHAPER (with its custom pattern) on the mix | ✅ done and tested | [live][movement]; `analog: SHAPER's custom pattern is played, saved …` |
| DRIVE: ANDOR OSP's (TUBE, TAPE, CRUNCH; TONE, BODY) | ✅ done and tested | [live][drive] |
| ANALOG per-voice tolerances and wander from the seed | ✅ done and tested | [analog] ranges at 50 % |
| 16 voices: allocation, stealing with fade, repeated notes, pedal, CC 120/121/123, mono legato, glide, unison | ✅ done and tested | [voices]; a taken voice keeps its timing with grains and modes on |
| Sample-accurate MIDI; bit-identical across block sizes; no allocation while rendering | ✅ done and tested | [render], [realtime], [live][determinism], `analog: a note starts on its own sample` |
| 4x oversampling with half-band decimation; latency reported | ✅ done and tested | 22 samples |
| CPU (16 voices held, one Linux VM core, 4x, 48 kHz) | ✅ measured | plain sounds ~21 %; REIMAGINED modes 28–42 %; GRANULAR clouds up to ~50 % (Grain Cloud, Frozen Glass) |
| SPACE and ECHO (ANDOR OSP's own effects) | ✅ done and tested | [modulation] space/echo routes; [bindings] every space/echo setting audible |
| Output headroom and guard | ✅ done and tested | every factory chord below the guard's knee |
| Plugin: product identity, registry, parameters (ANDOR schema unchanged + 57 own, golden) | ✅ done and tested | `andor_analog_tests` (shell suite + product tests) |
| Sessions and presets of version 1 migrated (state version 2) | ✅ done and tested | `analog: a version-1 session or preset is migrated and sounds as it did` |
| Every enabled control audible, every disabled one inert (all ~400 host parameters) | ✅ done and tested | `analog: every control the interface enables ...` |
| Generator card (OSCILLATORS + waveform picture, ONE SHOT / GRANULAR and the grain controls), no drop zone, files refused | ✅ done and tested | shell test `a generator engine has one card ...`; screenshots |
| Macro popovers: DRIVE, MOVEMENT, SPACE, ECHO and REIMAGINED are ANDOR OSP's own (shared from its repository; OSP stays pixel-identical); FILTER (was TONE) is the instrument's (the oscillators' settings are the card's workbench); the first macro is not assigned yet (disabled) | ✅ done and tested / ⚠️ FILTER and OSCILLATORS reviewed only as screenshots | `analog: DRIVE, MOVEMENT, SPACE and ECHO open ANDOR OSP's popovers …`; `analog: the card's first knob is DETUNE ...`; `[screenshots]` |
| State version 3: BLEND left the screen (the oscillators are mixed by their LEVELs); version-2 sessions and presets fold it into the levels; factory presets likewise (factory set 3) | ✅ done and tested | `analog: a version-2 session's BLEND moves into the oscillators' levels and sounds as it did` |
| 34 factory presets (10 new for the new engines), installed to Documents/…/Factory | ✅ done and tested | `analog: the factory presets are installed, listed and load exactly`; headroom test |
| Session recall | ✅ done and tested | `analog: a session recalls the sound exactly` |
| Visual regression (parity with ANDOR OSP, 18 states, 0 px outside masks) | ✅ done and tested | `andor_analog_tests_visual` |
| ANDOR OSP unchanged (design capture, schema golden, suite) | ✅ done and tested | design capture byte-identical except the MOVEMENT popover's moving strip, which also differs run to run on the unchanged build; OSP's ctest green |
| Linux VST3 and Standalone build | ✅ built | `ANDOR_Analog_All` |
| macOS build, AU, auval | ✅ done (CI) | `plugin-macos`: build, ctest, `auval -v aumu Aanl Ospx`, the test-build zip |
| Windows build (VST3, standalone; 64-bit) | ✅ done (CI) | `plugin-windows`: Visual Studio 2022, the engine's and the product's tests, the test-build zip with Install.bat |
| **State version 4** (one migration: models, OTA, chorus, ORGANIC, MORPH; 44 parameters appended; V1→V2→V3→V4) | ✅ done and tested | `analog: a version-3 session opens with every version-4 setting neutral and sounds as it did`; schema golden |
| **State version 5**: the OSCILLATORS card's LEVEL / TUNE / MUTE no longer reach ORGANIC; the header's A/B mix (`ab.blend`) plays (oscillators vs ORGANIC; centred on a first drop, after a reset and for version-4 sessions); mode named STANDARD / GRANULAR | ✅ done and tested | `organic: the oscillators' LEVEL, MUTE and TUNE leave the recording alone ...`; `analog: a version-4 session rests at the A/B mix's centre ...`; `analog: ORGANIC takes a recording ...` |
| **Walnut finish** (docs/WALNUT_VISUAL_DESIGN.md): walnut cheeks, aged copper accents and badge, graphite knobs and copper-brown keys on the dark wells; the family's ivory ceramic; ANDOR OSP's finish untouched (opt-in product materials) | ✅ done and tested / ⚠️ reviewed as screenshots on Linux, not in a DAW | `[materials]`; visual regression (parity with ANDOR OSP's materials); ANDOR OSP suite |
| Legacy sounds unchanged (34 factory + 6 edge patches, 3 rates; exact hashes on Linux, contours everywhere) | ✅ done and tested | `[legacy]` (tests/golden/legacy-renders.txt) |
| **EQ and DAMPEN per card** (ANDOR OSP's, `layerX.eq.*` / `layerX.dampen.*` now bound): card A's on the oscillators and noise, card B's on ORGANIC (and MORPH's result), in every voice before the filter (`dsp/VoiceEq.h`); off they are skipped (legacy renders bit-exact) | ✅ done and tested / ⚠️ not yet judged by ear | `[cardeq]` (5 cases), `[bindings]` covers every EQ/DAMPEN control; `analog-eq-card-*` screenshots |
| **KEY DNA is ANDOR OSP's** (state version 6): its editor (opened from the card's KEY DNA key in GRANULAR), its curves, undo and its `KeyDna` tree in the session (one codec with ANDOR OSP: `KeyDnaState`); AMOUNT is `layerA.keyDna.amount`. Version-5 sessions' `analog.keyDna.*` migrate (seeded by the patch's seed: the same sound); factory presets carry their profile (`analog-factory-6`) | ✅ done and tested | `[keydna]` (migration renders identical; editor, undo, curves, recall), `[presets]`, `[bindings]` |
| **ORGANIC's playhead**: one read head per playing note on card B, as ANDOR OSP's One Shot | ✅ done and tested | `[display]`; `analog-organic-playheads` |
| **BOTH** (the workbench's first page): the two oscillators at once, equal sections, wide (side by side) and compact (two strips); waveform key in the header; NOISE moved to PAIR, DETAILED | ✅ done and tested / ⚠️ reviewed as screenshots, not in a DAW | `[workbench]`; `analog-both-*` |
| **ORGANIC dock**: empty, a compact ADD SAMPLE dock (80 / 20); a recording opens its card (50 / 50); removed or failed, the dock again | ✅ done and tested | `[dock]`; `analog-dock-*` |
| **Sample Source importer, MVP A1** (docs/sample-source/SAMPLE_SOURCE_IMPORTER.md). It imports one Kontakt instrument, Evenant Engine Bukkehorn 4.5, as native presets; it is not a universal Kontakt importer. **A1.0:** the evidence report and the manifest of 48 groups and 333 zones. **A1.1:** zones play in ORGANIC's place (up to 8 per note; loops). **A1.2:** a preset per sound, plus Saved Lanes and a hybrid starting point; recordings go into ANDOR Analog's store; the definition saves with presets and sessions; a missing recording is reported and can be relinked. **A1.3:** the A/B mix blends the sound with the oscillators. **A1.4:** NKI plus folder, the review window, ORGANIC's sound popover. **Since 3 Nov 2026:** an import writes no presets, its sounds are reached only through card C's SOUNDS (INSTRUMENT PATCH); the presets earlier imports wrote leave the preset list at startup (to the system's trash; presets the user saved stay) | ✅ done and tested / ⚠️ only 18 of 201 recordings tried here; not yet compared with Kontakt by ear (A1.5) | `SampleSourceTests` (incl. no allocation); `[samplesource]`, `[nki]`; `[bukkehorn]` (correlation 0.9998, level −0.03 dB from Kontakt's); `import-*` screenshots |
| **MIX and KEY DNA AMOUNT as modulation destinations** (ANDOR OSP's second destination list, `mod.routeN.destExt`): a global source moves the header's A/B mix, between the oscillators and ORGANIC; a route moves card A's KEY DNA AMOUNT | ✅ done and tested | `analog: every modulation destination it offers moves the sound ...` (MIX through LFO 1, KEY DNA A AMOUNT through VEL); `[bindings]` (each route's `destExt`) |
| **State version 7: the AMP envelope on screen is what plays.** ENV LINK's single envelope is now AMP's: it shapes the amplitude and the filter, and the FILTER ENVELOPE knobs rest. A linked version-6 session or preset has its filter envelope copied into AMP, so it sounds as it did. With VCA GATE on, the AMP picture says GATE, and editing the AMP envelope (a knob or a handle) switches the amplifier back to ENV. Brass Section 106 carries its envelope in AMP; factory set 7 | ✅ done and tested | `analog: the AMP envelope on screen is what plays in the Juno patches ...`; `analog: a version-6 session with ENV LINK keeps its sound ...`; `modulation: GATE makes the amplitude a gate; ENV LINK makes the filter follow the AMP envelope` |
| **Three engines, phases 1–3** (docs/THREE_ENGINE_ARCHITECTURE.md, state version 8): three equal cards, always shown: A OSC 1, B OSC 2 (the shell's layer C), C the hybrid (the shell's layer B: ORGANIC's IDs, recordings and imports where they were). Each oscillator's card has its own DETUNE, TUNE, OCT, LEVEL, M/S, EQ and DAMPEN, and its own picture. Empty, C is its ADD SAMPLE card in its own column; loading never moves a card. Each oscillator's timbre is revealed in its well by the pointer (the MODEL dropdown always shown; CLASSIC WAVE/WIDTH/LFO PW, DCO SAW+PULSE/SUB/WIDTH/PWM/SOURCE, WIDE WAVE/SHAPE+SPREAD/WIDTH/LFO PW); SYNC, RING, BLEND, POLY MOD, VOICES, the noise mixer and LFO PITCH moved to the FILTER popover. Version 8 migration: the old pair's M/S, EQ and DAMPEN copied to card B, so both oscillators sound as before | ✅ done and tested / ⚠️ reviewed as screenshots, not in a DAW | `card eq (three engines)` (a migrated pair = the legacy pair within 1e-4), `three engines: each oscillator's card M / S ...`; `[overlay]` at 869 / 1448 / 1810 px; `analog: three equal cards ...`; `[bindings]` (layer C); legacy renders unchanged |
| **Three engines, phases 4–6**: card C is a SAMPLE, OSC 3 (the slots' models in each voice), NOISE (COLOR, TONE, MOVEMENT; seeded, not keyed) or an INSTRUMENT PATCH (the importer's sounds, chosen on the card: only C changes). Only the selected type plays; the others stay loaded. Its TYPE dropdown is in its well (the empty card too); its knobs and keys follow the type. MIX reads A+B — C; MORPH acts only on C's recording as a SAMPLE. A version-7 import opens as an INSTRUMENT PATCH | ✅ done and tested / ⚠️ reviewed as screenshots at 869 / 1448 / 1810 px, not in a DAW | `[hybrid]` (types, dormancy, save and reload, knobs, overlays, v7 import), `analog: MIX sets the analog pair against whatever card C plays ...`, `realtime: three engines ...`; CPU: INIT 24.0 %, OSC 3 33.2 %, NOISE 29.6 %, worst 41.5 % (16 voices, 4x) |
| **FILTER popover, simplified**: three pages, the filter first. FILTER: TYPE (LADDER / OTA, HIGH-PASS), RESO, ENV, KEYS, DRIVE, the envelope with VELO; the OTA's voicing only with the OTA. VOICE: the amplifier, voices, LFO 1's depths, chorus, character. PAIR: SYNC, RING, BLEND, POLY MOD, the noise mixer. Every control still reachable; the page is interface state only | ✅ done and tested | `analog: the FILTER popover shows each model's own controls ...` (each page's controls, at most 12 on FILTER, fits); `three-1448-8-filter-popover-*` |
| ~~**Oscillator Workbench**~~ (retired by the three engines; docs/OSCILLATOR_WORKBENCH.md): OSCILLATORS (beside M/S) swaps the card's dark well between the waveform and an in-card editor — QUICK (both oscillators, by model) and DETAILED (OSC 1 / OSC 2 / PAIR / MORE / KEY DNA), PIN, the EQ taking the well and giving it back when pinned; DETUNE only tunes; the floating OSCILLATORS popover is gone | ✅ done and tested / ⚠️ reviewed as screenshots, not in a DAW | `[workbench]` (3 cases); `[screenshots]` `analog-workbench-*`; ANDOR OSP suite and design capture |
| DCO model: saw/pulse, divide-by-two sub, PWM (manual/LFO/env), pitch-stable (1/5 tolerance, 1/10 wander) | ✅ done and tested | `[models][dco]` |
| WIDE model: saw/pulse/triangle/square, SHAPE (pitch-tracking edge), SUPER SAW (7 saws, exact single saw at spread 0) | ✅ done and tested | `[models][wide]`, `[supersaw]`; aliasing at C7 ≤ −67.8 dB |
| All 9 model pairings with sync and Poly Mod; RING MOD; JUNO noise; slot on/off | ✅ done and tested | `[models][pairs]`, `[ring]`, `[noise]` |
| OTA filter (Juno/Jupiter, 24/12 dB, 2 solves/sample), ladder↔OTA crossfade; high-pass (Juno 6/60/106, Jupiter) | ✅ done and tested / ⚠️ HPF frequencies approximations (Jupiter's unsourced) | `[ota]`, `[hpf]`, `[models][ota]` |
| Juno-style BBD chorus (I, II, I+II, hiss), after the voices; OFF bit-exact | ✅ done and tested / ⚠️ rates from public write-ups | `[chorus]` |
| LFO DELAY/RISE, VCA GATE, ENV LINK | ✅ done and tested | `[models][juno]` |
| FILTER/MORPH popovers and the workbench: each model's own controls only; fit the window | ✅ done and tested / ⚠️ reviewed as screenshots | `analog: the FILTER popover ...`; `analog: the workbench shows each model's own controls ...` |
| 10 version-4 factory sounds (44 in all), factory set 4 | ✅ done and tested / ⚠️ voiced without listening | headroom, distinctness, trip through parameters |
| ORGANIC: one recording on the shell's layer B (OSP loader, store, card, recall/relocation), played per voice through the filter; LOOP (OSP continuation), REVERSE, FOLLOW, root override, swap without a click | ✅ done and tested | `[organic]`; `analog: ORGANIC takes a recording ...` |
| MORPH (state version 9: heard live): the oscillators (OSC 1, OSC 2 or the pair) and whatever card C plays (a recording, OSC 3, NOISE, an instrument patch) fused as they play. Both are analysed together every quarter window (a 21 .. 85 ms window sized to the note so its harmonics stand apart: harmonics at each side's own pitch, the rest as eight octave bands of residual). FUSE morphs partials and pitch in dB, WEAVE interleaves them, IMPRINT gives the oscillators card C's broad envelope. Exact ends (0 % the oscillators, 100 % card C), a fusion in the middle; a note starts on the sources as played (ATTACK) and glides in; a silent side leaves the other as it is. On at 50 % (FUSE) in a new instrument; a session or preset from before keeps its MORPH, its position turned round (migration 8 → 9). The analysis thread and the recording's profile are gone | ✅ done and tested / ⚠️ not yet judged by ear | `[live]` (ends exact; a saw's 1/h and a square's odd harmonics measured; the even ones fade in dB, not mixed; the pitch between; noise brought by NOISE; silence and NaN safe; the attack; LEVEL MATCH; a sweep without clicks; deterministic), `[morph]` (every card C type, ends, fusion not crossfade, three methods differ, click-free moves, every carrier pairing, chords, a silent C), `[realtime][morph]`, `analog: MORPH is the first macro ...`, `analog: a version-8 session keeps its MORPH ...` |
| MORPH can no longer be silently off: the factory presets (set 8) have it on, and taking the MORPH knob (its macro) in hand switches it on when a preset or session had it off (automation leaves the switch alone) | ✅ done and tested | `analog: taking the MORPH knob in hand switches MORPH on ...` |
| MORPH's sweep is a morph, not a fade: LEVEL MATCH (on by default) now brings both sides to the louder one's loudness (a quiet card C is raised to the oscillators'), and the endpoint law is equal-power, so 0 % and 100 % play at the same loudness and the middle neither dips nor bulges (a vowel recording ~20 dB under a chord: before, the sweep fell from 0.11 to 0.009 RMS; now 0.09 .. 0.14 across FUSE and IMPRINT) | ✅ done and tested / ⚠️ not yet judged by ear | `live morph: LEVEL MATCH holds the loudness between the two ...` (a card C 24 dB quieter: both ends within 1.5 dB) |
| **Effects rack, phase 1: MOVEMENT** (ANDOR OSP's shared rack host, opt-in): a full-width MOVEMENT module opens between the macros and the arpeggiator (opened from the macro's name: see the row on the invisible interaction). The module shows the mode as tabs, AMOUNT (the MOVEMENT macro) and the mode's own settings (SHAPER: PATTERN / RATE / TARGET, DEPTH, SMOOTH), and the picture in the modulation window's column (same width, same divider; the strip never taller than the macro panel): the live motion, or SHAPER's pattern editor with its tools (the magnifier widens it across the rack). Everything is bound to the parameters, so automation, tempo and the popover show at once. A view setting saved with the session: expanding or collapsing changes no parameter (tested), and the window grows or shrinks by the rack. One module at a time; the host can stack several later. Hidden in the visual regression's parity render (0 pixels) | ✅ done and tested / ⚠️ reviewed as screenshots at 869 / 1448 / 1810 px, not in a DAW | `analog: MOVEMENT's key expands its rack ...`, `analog: the rack's state is a view setting the session recalls`, `[screenshots-rack]` |
| **Effects rack, phase 2: MOVEMENT A and B** (state version 11). The MOVEMENT you had is lane A (its parameter IDs and sound unchanged); lane B is a second, independent MOVEMENT with all of A's settings (`analog.movement.b.*`) and its own SHAPER CUSTOM pattern (saved with the session). Each lane has ON, INTENSITY, and new SYNC / CYCLE (PULSE on the tempo: 1/16 .. 4 bars) and PHASE (synced PULSE and SHAPER); the MOVEMENT macro scales both. B is off by default and an older session opens with A on at 100 % and B off, so it plays exactly as before (every golden unchanged). The processing is real and in series (A then B, osp::PostProcessor::setMovementLanes): amplitude movements multiply, two delay-based ones never comb against a dry copy, and lane B's TAPE/CHORUS has a slow level guard (never louder than what it is given, -6 dB at most). Lanes switch on and off with a 30 ms crossfade, and a lane that is off is not run. DRIFT is per voice: lane B has its own wander (its own speed and randomness), and the two add in power, so together they never swing further than sqrt(a^2 + b^2). The rack's title row has LANE A / LANE B (each with its light), ON, COPY A > B (or B > A), SWAP, and PRESET (Save, Load, Browse: one lane's settings in Documents/ANDOR/Movement Presets; loading changes that lane only) | ✅ done and tested / ⚠️ not yet judged by ear | `[movement][lanes]`: bit-exact with B off; B is its own processing; all 25 mode pairs within +1 dB of the louder lane and above -9 dB of the quieter; click-free on/off; two DRIFTs in power; deterministic at any block size; SYNC and PHASE. Product: older sessions, presets touch only their lane, copy / swap, lane B's pattern recalled, the rack's lanes. CPU at 16 voices: single lane 29-43 % vs two lanes 28-42 % (`[lanes][cpu]`, noisy machine; the second bus is a few % at most) |
| **Effects rack, phase 3: ECHO and SPACE** on the same rack host: a key beside each name (the name still opens its popover). ECHO shows TAPE / BBD / ASCEND, FREE / SYNC, the stereo picture, TIME, FEEDBACK, TONE and AGE (ASCEND: CLOCK, INTERVAL, MOTION) and its repeats; SPACE shows the room or engine and its variant, the presets bar, every knob of the engine (ROOMS, ABYSS, VINTAGE, BLOOM) and its decay picture, whose handles set PRE-DELAY and DECAY. One module at a time (the host can stack them later). ECHO and SPACE stay the parallel sends they were: the rack only changes how they are edited, and opening or closing modules during the tails leaves the audio sample-identical (tested) | ✅ done and tested / ⚠️ reviewed as screenshots at 869 / 1448 / 1810 px, not in a DAW | `analog: ECHO and SPACE expand into the rack, one module at a time ...`, `analog: expanding or collapsing ECHO and SPACE never touches their tails`, `[screenshots-rack-fx]` |
| **Racks open from the macro's name, with no mark on the row**: the expansion keys are gone and the macro row is pixel-identical inside to the row before racks existed (compared: names, knobs, lights; only the panel's outer edge moved by the shorter cards). A click on MOVEMENT, ECHO or SPACE opens its popover (after the system's double-click interval, a cancellable deferral, so a double-click never flashes it); a double-click opens its rack, switches from another, or closes it, and closes a popover in the way; a right-click offers Quick Settings and Open / Close Rack; Enter (the name focused by the keyboard) opens the rack, Shift+F10 the menu. The tooltip says "Click for settings · Double-click for rack"; an open rack keeps its name's underline. Other names act at once as before. Future modules follow the same | ✅ done and tested | `analog: an effect's name - click for settings, double-click ...`, `[screenshots-macrorow]` |
| **One rack design for MOVEMENT, ECHO and SPACE** (SPACE's): every module has the same slots at the same places - the heading and its compact actions, MODE (a selector), PRESET (‹ NAME ›, larger, beside it), one row of equal control cells, and the picture in the AMP / LFO window's column behind the same divider; the strip is never taller than the macros (SHAPER's precision editor, behind the magnifier, is the one exception, with its brushes). MOVEMENT's tab strip became MODE; its lanes and ON stay beside the heading, COPY / SWAP behind LANES. ECHO's FREE / SYNC and stereo are compact keys among its controls. Presets follow one set of rules: the mode's presets, the arrows, NAME * once edited, CUSTOM when nothing matches, your own saved / renamed / deleted / recalled, loading changes that effect (that MOVEMENT lane) only. New: twelve ECHO presets (TAPE, BBD, ASCEND); MOVEMENT presets for each mode. SPACE's popover keeps its bar; old sessions and SPACE presets are unchanged | ✅ done and tested / ⚠️ reviewed as screenshots at 869 / 1448 / 1810 px | `analog: MOVEMENT, ECHO and SPACE share one layout ...` (the slots' places compared), `analog: rack presets - by mode, the arrows, NAME * ...`, `[screenshots-unified]` |
| **Rack final design and polish**: one geometry for every module (SPACE's: margin 22, MODE 190 × 26, PRESET from x 224 to the divider, row gap 8, cells 96 × 104 left-aligned at x 22, divider at 910, the picture from 931 with a 12 px foot; 214 px with the title, ≤ the 216 px macros), so every module's first control sits at one X in every mode and a four-control mode leaves the rest of the row empty. MOVEMENT: DRIFT (INTENSITY, SPEED, PITCH, TONE), TAPE (INTENSITY, WOW, FLUTTER, WEAR), CHORUS (INTENSITY, RATE, WIDTH, STEREO), PULSE (INTENSITY, TIMING, CYCLE, SHAPE, STEREO, PHASE), SHAPER (PATTERN, RATE, TARGET, DEPTH, SMOOTH, PHASE); its pictures carry legends and the values they show (PITCH / TONE, WOW / FLUTTER and WEAR, LEFT / RIGHT and the rate); SHAPER's brushes only in the magnified editor. ECHO's picture: a dashed decay envelope, quiet repeats kept legible, a restrained right-channel tint, more height, captions for the time, the feedback (N repeats · −X dB each, from the feedback itself) and ASCEND's pitch. Typography: MODE, choice keys and captions stronger and larger (no new font or colour); long choices shrink to fit. Presets: the last chosen preset is remembered per effect (per MOVEMENT lane) in the session, so NAME * survives collapsing, another module and a session restore; a value set back by automation or undo shows NAME again. Layout only: no parameter ID, sound, preset format or state version changed | ✅ done and tested / ⚠️ reviewed as screenshots at 869 / 1448 / 1810 px | `analog: every rack's first control sits at one X ...`, `analog: a rack preset's name, and its *, follow the settings ...`, `[screenshots-polish]` |
| **MOVEMENT rack header, minimal**: the title row is the MOVEMENT title and its light on the left, then only a small A / B selector at the right before the collapse key: each letter with its lane's light (lit: the lane plays; grey: off), the lane shown underlined as an open macro's name. Choosing A or B only shows that lane, never switches it on or off. The shown lane's ON is a light and its word at the end of its controls row (right-aligned under PRESET, on the captions' line; beside PRESET over SHAPER's wide editor). COPY A > B, COPY B > A and SWAP moved to the A / B selector's menu (right-click, Shift+F10) and the presets' menu, working as before. MODE, PRESET, the grid and the picture unchanged. UI only: no DSP, parameter ID, preset or routing change | ✅ done and tested / ⚠️ reviewed as screenshots at 869 / 1448 / 1810 px | `analog: MOVEMENT's header is the title and A / B ...`, `[screenshots-header]` |
| **Rack layouts by mode, MOVEMENT's power in the header, fixed modulation tabs**: each rack mode has one of three layouts by its controls' count - compact (up to four: MOVEMENT DRIFT / TAPE / CHORUS; picture 53 % of the rack), medium (five or six: PULSE, SHAPER, ECHO TAPE / BBD; picture 45 %), dense (more: ECHO ASCEND, every SPACE engine; the picture under the AMP / LFO window, as before). The picture keeps its right edge and widens leftward; MODE, PRESET and the first control keep their places; a value, a preset of the same mode or automation never moves anything (tested). MOVEMENT's title row reads MOVEMENT, A B, the shown lane's power (a round key like the collapse key, the EQ's power mark, lit in the lane's colour), collapse; the ON at the end of the controls is gone. The modulation panel's six tabs keep fixed places whatever is shown, the route count or the curve's editability (a reserved place at the right for the count; ShellProduct::stableModulationTabs, ANDOR OSP's row unchanged), and the larger curve editor's key moved from the tab row into the display's top-right corner. Fixed on the way (ANDOR OSP too): VEL's and NOTE's larger curve editor and routes never opened (the popovers took LFO 1 .. ENV 2 only) | ✅ done and tested / ⚠️ reviewed as screenshots at 869 / 1448 / 1810 px | `analog: each rack mode has its layout ...`, `analog: the six modulation tabs never move ...`, `analog: VEL's and NOTE's larger curve editor opens from the display ...`, `[screenshots-adaptive]`, `[screenshots-modtabs]` |
| **Approved effects racks (MOVEMENT, ECHO, SPACE) and inline FILTER / MORPH racks**: the approved design (docs/APPROVED_RACKS_AUDIT.md) - shared ceramic controls (vertical two / three-position switches, segmented switches, M/S-like lane keys, the stepped rate knob, the approved cell metrics, MODE 165 and PRESET 294 fixed fields with a `···` actions key, the approved graph frame); per-module graph columns (57 / 61 / 68 %), standard and dense slot families; MOVEMENT header A B ON (mini switch) collapse, PULSE TIMING switch with one RATE knob for FREE and SYNC, SHAPER stepped RATE and VOL / FILTER / BOTH; ECHO TIMING and MONO / PING / WIDE switches, one TIME knob; SPACE FREEZE switch, VINTAGE ERA segmented, BLOOM TIMING switch. FILTER (FILTER, VOICE, PAIR) and MORPH as inline racks on the shared `rack::ParameterRack`, opened from their macros, for design approval. UI only: no DSP, parameter, preset or state change | ✅ done and tested / ⚠️ FILTER and MORPH await design approval | `analog: the approved rack layout ...`, `analog: the racks' switches are their parameters ...`, `analog: FILTER and MORPH open inline ...`, `analog: FILTER's graph is the filter model's response ...`, `[screenshots-polish]`, `[screenshots-filtermorph]`; OSP `[rackcontrols]` |
| **One rack height and one set of control lines**: FILTER (FILTER, VOICE with AMP / MOD / CHARACTER, PAIR) and MORPH in the standard 214 px rack like MOVEMENT, ECHO and SPACE (selectors in the selector row, one control row); every control on the shared cell lines (caption, control centreline - a vertical switch by its track - and value), the keyboard still when pages change. UI only | ✅ done and tested / ⚠️ FILTER and MORPH await design approval | `analog: every rack and page on the same lines ...`, `[screenshots-guides]` |
| **The racks' hierarchy (FILTER, MORPH)**: pages and sub-pages told apart (primary 11 px dark + 2 px underline; secondary 10 px lighter + 1 px underline; quiet rules between pages, sub-pages and settings); FILTER shows only the settings its model uses (SLOPE with OTA; VOICING / JUNO when the OTA or the high-pass uses them); captioned groups with a hairline; VOICE MOD's graph is LFO 1's depths and fade-in from the parameters, CHARACTER a status (nothing measured to draw); PAIR's noise COLOR over its knob; MORPH's SETUP group (OUTPUT switch, LEVEL MATCH, ROOT) a rank quieter, card C's eligibility the graph's empty state, not the header. UI only | ✅ done and tested / ⚠️ FILTER and MORPH await design approval | `analog: FILTER and MORPH open inline ...` (applicable selectors, COLOR over NOISE, MORPH's quiet setup), `[screenshots-guides]` |
| **FILTER and MORPH final polish**: the approved racks' type scale throughout (graphs on ECHO's metadata / label styles and lines; one selector-caption style; secondary tabs readable, set apart by space); 28 px group gaps; AMP and MOD one section each (WHEEL the last control); CHARACTER's graph the ladder's drive curve and the chorus taps' sweep from the engine's equations (`filtergraph::driveCurve`, `JunoChorus::spec`), BIAS resting with the OTA; NOISE COLOR over its knob; MORPH's METHOD / CARRIER wider, its setup quiet, a clearer empty state and legend. UI only | ✅ done and tested / ⚠️ FILTER and MORPH await design approval | `analog: CHARACTER's drive curve ...`, `analog: FILTER and MORPH open inline ...`, `[screenshots-final]` |
| **Card C's modifiers in its well** (phase 4): LINK, REVERSE, LOOP and FOLLOW are a compact row of graphite icon keys at the top-left of card C's dark well instead of the large keys under its knobs (the freed foot is removed: the cards are 64 px shorter with their displays at the original height, and everything under them moves up, so the whole window is 64 px shorter). Only those that act on what card C plays: a SAMPLE all four; an INSTRUMENT PATCH LINK, LOOP and FOLLOW (its zones do not play backwards); OSC 3 and NOISE none. The same parameters and automation; each key takes keyboard focus (space or return toggles it); they give way to the EQ, the note map and the in-card editor; the type's selector stays at the top-right and the granular controls at the foot. ANDOR OSP keeps its keys (opt-in: `ShellProduct::cardModifiersInWell`) and the parity render is unchanged. No DSP change | ✅ done and tested / ⚠️ reviewed as screenshots at 869 / 1448 / 1810 px | `analog: card C's modifiers sit in its well, only those that apply, bound as before`; `[screenshots-three]` |
| The mod wheel moves nothing unless routed (state version 10): the classic wheel → LFO 1 depths link (LFO 1's PITCH 1 / 2 at ±0.4 st: a vibrato heard as a detune) is the WHEEL switch on the FILTER popover's VOICE page, off in a new instrument and in the factory presets; a session or preset from before turns it on, so it sounds as it did | ✅ done and tested | `[modulation][wheel]`, `analog: the mod wheel moves nothing in a new instrument ...`, the legacy renders (with WHEEL, as migrated) |
| **FX ROUTING (gates 0-5)** (docs/FX_ROUTING_IMPLEMENTATION.md, docs/FX_EFFECTS_LIBRARY.md): the routing view in the cards' place (icon beside SAVE); CLASSIC plays the original chain bit for bit (114 frozen renders); CUSTOM routes in series and parallel with undo, bypass glides and tails; nine new effects (STEREO SPREAD, COMPRESSOR CLEAN / FET 76 / MULTIBAND, EQUALIZER, FX FILTER, PHASER, FLANGER, FREQ SHIFTER, CONVOLUTION, UTILITY) with their DSP, 283 parameters (state version 12), the FX rack, quick settings, effect presets and FX chain presets | ✅ done and tested / ⚠️ reviewed as screenshots on Linux, not in a DAW; not yet voiced by ear | `[fx][legacy]`, `[fx][route]`, `[fx][effects]`, `[analog][fx]` (state, routing UI, rack, presets, convolution); `[screenshots-routing]`, `[screenshots-fx-racks]` |
| **FX RACK V2 gates 0-6** (docs/FX_RACK_V2_PROGRESS.md): hardware faceplates on every FX rack, DRIVE / MOVEMENT / ECHO / SPACE and the synthesis racks (FILTER, MORPH); **RETRO** (TAPE / VINYL / DIGITAL 80s, 90s, CODEC) with its DSP, 96 parameters (state 13), rack, graph and 13 presets | ✅ done and tested / ⚠️ screenshots on Linux, not in a DAW | `[retro]`, `[realtime][fx][retro]`, `[analog][fx][rack][ui]`, `[screenshots-fx-racks]` |
| **FX RACK V3 polish** (docs/FX_RACK_V3_POLISH.md): one full-bleed rack face, no violet (SPACE spore), horizontal ON slides, a borderless X, the rail on one centre line; FILTER's graph LIVE from the engine's voice telemetry and a silent PREVIEW; FX rack controls no longer greyed by bindings | ✅ done and tested / ⚠️ screenshots on Linux, not in a DAW | `[filterlive]`, `[screenshots-v3]`, `[screenshots-filter-sweep]` |
| **ROUTING + TAPE V4** (docs/FX_ROUTING_TAPE_V4.md): the engine mixer at 21 % with ceramic level knobs on the cards' own LEVEL parameters; legible titles and tiles; the board a projection of the route: one row, two rows in order (CONT. 2 / FROM 1), parallel stages, labelled group summaries when nothing fits, no scrolling; ECHO TAPE and MOVEMENT TAPE as a light reel deck (shared, opt-in) with a PREDICTED display; reels follow a read-only output-peak telemetry | ✅ done and tested / ⚠️ screenshots on Linux, not in a DAW; no ADVANCED graph yet (gate 9) | `[v4]`, `[screenshots-v4]` |
| **V5 PATTERNED HARDWARE** (docs/VISUAL_IDENTITY_INDEX.md): one identity per device (enamel, original vector motif, inks, glass) shared by the routing tile, the library, the quick settings and the full rack; tiles are miniature hardware with 2-3 real knobs (SliderAttachments to the racks' parameters); every rack's whole face is its enamel with its motif and four bolts; no violet | ✅ done and tested / ⚠️ screenshots on Linux, not in a DAW | `[v5]`, `[v5-audio]` (render hashes identical to before V5), `[screenshots-v5]` |
| **EQUALIZER graphic** (docs/FX_EFFECTS_LIBRARY.md): the rack is one full-width graphic in the card EQ's design - nodes, overlay fields, HP / LP, MIX / OUT - with a live spectrum (input line, output fill; telemetry taps, FFT on the message thread); racks with presets have a save key beside the browser; FREQ SHIFTER, PHASER, FLANGER, FX FILTER in one view | ✅ done and tested / ⚠️ not tried in a DAW | `[eqgraphic]`, `[savekey]`, `[screenshots-eq]` |
| **ROUTING cleanup**: no text the view does not need (titles, status, path summary, footer keys, CONT. 2 / FROM 1, PARALLEL); the mixer's rows fill the column's height (one per card); an X closes the view; the TAPE deck's display (ECHO, MOVEMENT) fills its column without a caption | ✅ done and tested / ⚠️ screenshots on Linux | `[v4]`, `[fx]`, `[screenshots-routing]`, `[screenshots-v5]`; docs/screenshots/routing-cleanup |
| **Header without MIX; one X everywhere**: the header MIX is off for now (the cards' levels and the routing view balance them; `ProductInfo::headerMix`); the preset bar takes its room, SAVE and ROUTING follow as keys of one size on one centre line, 8 px apart; the ARPEGGIATOR and the routing view close with the racks' borderless X, the same size, 25 px from the right edge | ✅ done and tested / ⚠️ screenshots on Linux | `[routing]`, `[screenshots-rack-fx]`; docs/screenshots/header |
| **Cinematic V6 gate 0 + gate 1** (docs/CINEMATIC_V6_GATE0.md, _GATE1.md): the 1.0 baseline frozen (`[v6-baseline]`: 44 factory sounds + 2 routes through the whole instrument, 92 renders); each oscillator card's own REIMAGINED (card B on ANDOR OSP's `layerC.reimagined*`), state version 14, older patches give both cards theirs; B's VELO in its well | ✅ done and tested / ⚠️ five REIMAGINED factory sounds sound different by design; not tried in a DAW | `[v6-baseline]`, `[reimagined][v6]`, `[v6][reimagined]`, `[screenshots-v6-g1]` |
| **Cinematic V6 gate 2: SUB** (docs/CINEMATIC_V6_GATE2.md): an independent 8-voice sub oscillator on the musician's own keys (before the arpeggiator), after every effect (dry by construction); WAVE, OCT, GRIT, LEVEL, RANGE, FLAT/TAPER; `arp` / `sub` stacked in the arpeggiator's housing (OSP opt-in `arpCompanion`), SUB's popover with a RANGE keybed; state version 15 | ✅ done and tested / not tried in a DAW | `[sub][v6]`, `[v6][sub]`, `[realtime][sub]`, `[screenshots-v6-g2]` |
| **Cinematic V6 gate 3: card C's second layer** (docs/CINEMATIC_V6_GATE3.md): C2 as another layer inside card C (OSC, NOISE, or its own recording / instrument patch; LEVEL, pitch, VELO), sharing card C's M / S, modifiers, EQ / DAMPEN, lane, MORPH and routing; + / 1 / 2 / × keys in card C's well rebind the well, picture, heading and knob row; state version 16 | ✅ done and tested / not tried in a DAW | `[hybrid2][v6]`, `[v6][hybrid2]`, `[realtime][hybrid2]`, `[screenshots-v6-g3]` |
| **Cinematic V6 gate 4: MAIN + SEND** (docs/CINEMATIC_V6_GATE4.md): an opt-in route (gear: Use MAIN + SEND…; never on load): MAIN the dry inserts (no ECHO / SPACE / CONVOLUTION), SEND fed by each card's SEND (taken before the voice filter) and strictly wet; SUB on MAIN, never SEND; MAIN \| SEND \| OVERVIEW in FX ROUTING's header, the mixer's sends on SEND; route schema 2; state version 17 | ✅ done and tested / not tried in a DAW | `[mainsend][v6]`, `[v6][mainsend]`, `[realtime][mainsend]`, `[screenshots-v6-g4]` |
| FX route latency (V2 gate 8, part): planned from each effect's maxLatency, constant through bypass, parallel lanes and sends aligned, the host told when it changes | ✅ done and tested / ⚠️ not tried in a DAW | `[fx][route][latency]`, `[analog][fx][latency]` |
| FX ROUTING gates 6-13: stems and buses, splitters, Advanced view, Lock FX, user impulses, DRIVE creative modes, realtime/CPU matrix, DAW validation | ❌ not done | docs/FX_ROUTING_IMPLEMENTATION.md, "Not built yet" |
| CPU (16 voices, 48 kHz, 4x, one core) | ✅ measured | CLASSIC 24.8 %, DCO 28.1 %, SUPER SAW pair 34.1 %, OTA 36.3 %, ORGANIC 40 %, ORGANIC + MORPH (live) 52–55 % (`[morph][cpu]`) |
| pluginval, DAWs | ❌ not done | docs/RELEASE.md |
| Listening tests and preset voicing by ear | ❌ not done | docs/AUDIBLE_QA.md checklist |
| Host parameter list trimmed to the used ones | ❌ not implemented (shell-wide limitation) | release blocker |
| Signing, notarization, installer, JUCE licence | ❌ not done | docs/RELEASE.md |

## Version 2 notes

- The first version's sessions and presets open as they sounded, with one exception: an LFO set
  to S&H becomes ANDOR OSP's RANDOM (a smooth random; the stepped one is not among LFO 1's shapes).
  The factory "Random Steps" uses RANDOM now.
- MOSAIC rebuilds a recording from the harmonic frames ANDOR OSP analyses when it is loaded; a
  live sound has none, so in ANDOR ANALOG the mode plays the sound unchanged and its popover says
  so ("NEEDS A RECORDING: PLAYS UNCHANGED"). Its settings are shown disabled.
- TAPE FRAME's FRAME lengths (3.6–11.4 s of tape) are longer than a voice's recording (1.5 s):
  the tape runs for the whole FRAME, its travel scaled into the recording.
- KEY DNA is ANDOR OSP's own editor (curves included) since state version 6.

## Version 4 notes

- ORGANIC is the shell's layer B. Its card is ANDOR OSP's own sample card, with LEVEL, TUNE,
  START, LOOP, FOLLOW, REVERSE, M and S. PAN stays disabled: each voice is one mono signal
  through its filter.
- MORPH needs card C to sound (a recording, OSC 3, NOISE or an instrument patch). Otherwise its popover says so and the oscillators play as they
  are.
- The oscillator pair is one card. There is no third "OSC 2" card: see the decisions below.

## Open decisions for the owner

0. Version 4:
   - three source cards (OSC 1, OSC 2, ORGANIC) instead of two (the pair, ORGANIC): that needs
     a LEVEL/PAN/M/S for each oscillator in the shared shell;
   - the HPF frequencies and the Juno-6 voicing, by ear or by measurement;
   - MORPH by ear on real recordings (nyckelharpa, cello);
   - whether a DCO with its sub (−67.8 dB aliasing at C7, against CLASSIC's −75.2) is
     acceptable.

1. Final product name and tagline ("ANDOR Analog", "/ANALOG", "A living analog instrument").
2. The generator card as built (one hero card with the oscillators' waveform) — or a designed
   ANDOR extension with oscillator controls on the card.
3. The arpeggiator ships as ANDOR OSP's (played by the shell): by ear.
4. Voicing: filter and drift constants, factory sounds (the ten new ones included) — by ear.
5. JUCE commercial licence, signing credentials, the artwork's use in this product.
