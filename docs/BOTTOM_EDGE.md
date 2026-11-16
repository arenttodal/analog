# Compact bottom edge — ANDOR OSP and ANDOR Analog

_16 Nov 2026, branch `feature/aux-live-input` in both repositories. Shared code:
`external/osp/shared/andor-ui`, used by both instruments._

## What was there

- Both editors lay out one canvas of 1448 × **1086** reference px (`design::height`).
  - ANDOR Analog trims its cards by 64, so its canvas is 1022.
  - The window's height always follows the instrument's (`frame::instrumentHeight`,
    `ScreenFit`). Width is limited to 869–1810, and there is no aspect ratio.
- The bottom was shared and top-anchored:

  | Element                                       | Reference y |
  |-----------------------------------------------|-------------|
  | Keyboard (white and black keys)               | 913–1014    |
  | PITCH / MOD wheels                            | 913–1001    |
  | PITCH / MOD captions (MOD's socket centred)   | 1001–1019   |
  | ARP card                                      | 913–977     |
  | D / SETTINGS / INFO keys                      | 983–1014    |
  | Footer status line ("Ready", "No sound loaded", notes held, messages) | 1038–1062 |
  | Housing foot                                  | 1072        |
  | Surround (with the housing's shadow) below the foot | 14 px  |

- There is no separate apron or trim layer. The housing (the ceramic nine-slice, or the
  painted body) is the ivory under the keys. That treatment is kept and nothing is added.

## What changed

- `design::height` is now **1050** and `layout::housing` ends at **1036**.
  - That puts the housing foot 22 px under the keys and 17 px under the caption box. The caption
    glyphs end about 1015, so they get about 20 px, in line with the housing's side insets
    (17 px to the PITCH wheel).
  - The 14 px surround and the shadow below the foot are unchanged.
- Nothing above the keys' foot moved or was scaled:
  - The keys, wheels, ARP, the action keys and every control keep their coordinates and size.
  - The window is shorter; the content is not compressed.
- The footer status line is removed, with no reserved space left behind.
  - What it carried that matters now shows once in the modulation overlay's existing brief
    notice (`ModulationOverlay::showStatusNotice`), centred above the keyboard row: a file that
    could not load, the processor's messages (opened, saved, a file left out) and OSP's sound
    warning (natural pitch on a long recording).
  - "Ready", "No sound loaded" and the note count are gone; the cards already show their own
    loading.
  - A diagnostic engine's name remains in the main menu ("Engine: …").
- Anchored popovers now stay above the instrument's foot too (`frame::placePopover` clamps the
  bottom as well as the top). Analog's SHAPER DNA panel, opened from the MOVEMENT rack, would
  otherwise have run 21 px past the shorter window.

| Editor                     | Before          | After           |
|----------------------------|-----------------|-----------------|
| OSP canvas (1448 wide)     | 1448 × 1086     | 1448 × **1050** |
| OSP default window         | 1086 × 815      | 1086 × 788      |
| Analog canvas (1448 wide)  | 1448 × 1022     | 1448 × **986**  |
| Smallest width (869)       | OSP 651, Analog 613 | OSP 631, Analog 592 |
| Largest width (1810)       | OSP 1357, Analog 1278 | OSP 1313, Analog 1232 |

The arpeggiator's editor, an open rack and the AUX strip still add their own height on top.

## Bottom spacing (reference px, after)

| From                                 | To the housing's foot | To the canvas' foot |
|--------------------------------------|-----------------------|---------------------|
| Keys' foot (1014)                    | 22                    | 36                  |
| PITCH / MOD caption box (1019)       | 17                    | 31                  |
| PITCH / MOD caption glyphs (~1015)   | ~21                   | ~35                 |
| D / SETTINGS / INFO keys (1014)      | 22                    | 36                  |

## Files

**OSP** (`external/osp`):
- `shared/andor-ui/Source/Design.h`: height, housing and comments; `layout::status` removed.
- `shared/andor-ui/Source/InstrumentFrame.h/.cpp`: `Parts::status` and `setUpStatusLabel`
  removed; `placePopover` clamps the bottom.
- `shared/andor-ui/Source/ModulationPanel.h/.cpp`: `showStatusNotice`.
- `shared/andor-shell/Source/ShellEditor.h/.cpp`: status label → notices.
- `apps/plugin/Source/PluginEditor.h/.cpp`: status label → notices.
- Tests:
  - `tests/support/VisualStates.h`: renders at `design::height`; the `status-line` mask is gone.
  - `tests/shell/ShellTests.cpp`: a bottom-edge test for every product.
  - `tests/plugin/PluginProcessorTests.cpp`: an OSP bottom-edge test and a screenshot test.

**Analog**:
- `product/tests/AnalogLearnTests.cpp`: a screenshot test.
- This document, `docs/STATUS.md` and `docs/screenshots/bottom/`.

## Screenshots (`docs/screenshots/bottom/`)

All screenshots are real editors rendered headless, at identical scale (1 px = 1 reference px at
1448).
- `*-bottom-before-*.png` and `*-bottom-after-*.png`: full GUI at 869, 1448 and 1810 wide (the
  smallest, the reference and the largest width).
- `*-bottom-compare.png`: the bottom section, before above and after below.
- `*-corner-pitch-mod-before-after.png` and `*-corner-arp-before-after.png`: the lower corners,
  3× enlarged.

## Verification and limits

- Automated, Linux headless: see `docs/STATUS.md`.
- Not verified here (OWNER TEST): hosts' plugin windows (AU / VST3 in a DAW) and the Mac
  standalone. The size logic is the same code in every format; a host that kept an old, taller
  window shows the surround under the instrument until it re-asks the editor's size.
