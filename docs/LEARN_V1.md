# ANDOR Learn in ANDOR Analog (V1)

Guided lessons and a contextual Learn mode, built on the shared `shared/andor-learn` module in `external/osp`. Its authoring guide is `external/osp/shared/andor-learn/README.md`.

## What the musician sees

- **Two items in the top-right menu**, after Undo / Redo:
  - **Learn ANDOR…** opens the topic browser.
  - **Learn mode** turns inspection on and off.
  - A **Learn** submenu holds **Reset learning progress**. It never resets the sound; that is the existing *Reset settings*.
- **The invitation.** It appears once, about half a second after the editor first opens on a profile with no Learn file: *Find your sound. Learn the instrument as you play.*
  - It offers **Not now**, **Browse lessons** and **Quick Start**.
  - Any answer is remembered; another plugin instance does not show it again.
  - It never opens a lesson or changes the sound on its own.
- **Lessons.** A small ceramic note beside the real control, which is ringed in the accent colour.
  - It shows the step number, a title, one or two sentences, and Back, Show me, Skip or Next.
  - A completed task shows a check and Next. Only the musician moves on.
  - The cross (or Escape) closes the lesson and leaves the sound exactly as they set it.
- **Learn mode.** A quiet note at the foot says *Click an area to learn · Esc to exit*.
  - Hovering a main area outlines it. A click explains it, with **Start lesson** (where one exists), **Open controls** and **Close**.
  - Escape closes the explanation, then Learn mode.

Nothing is added to the instrument while none of this is shown. The window, cards, racks and rows keep their sizes.

## Lessons

| ID | Title | Steps | Needs |
|---|---|---|---|
| `analog.quickstart` | Quick Start | 4 explanations: Sounds, Three cards, Six macros, Arpeggiator | — |
| `analog.arp.basics` | Make a moving pattern | 8 (see below) | the per-card arpeggiator |
| `analog.morph.basics` | Blend with MORPH | open the MORPH rack, the two sources, move MORPH, the rack | — |
| `analog.fx.routing` | See the effect chain | open FX ROUTING, the mixer, open a tile's quick settings, the chain | — |

C2, SUB, MAIN / SEND and independent REIMAGINED are not in Analog 1.x and have no lesson.

The ARP lesson:

| # | Target | Task | Completed by |
|---|---|---|---|
| 1 | the ARP card | open its controls | a click on the card (not its light) and the panel open. *Already open · Continue* if it was. Show me opens it but completes nothing. |
| 2 | the ARP light | switch it on | a click on the light and `arp.enabled` = 1, or *Already on · Continue* |
| 3 | PATTERN | another pattern | a pattern chosen in the list, and `arp.pattern` changed |
| 4 | RATE | another rate | RATE turned (drag, wheel, double-click, accessibility), and `arp.rate` changed |
| 5 | PLAYS A B C | A off, B on | a card toggled, then A = 0 and B = 1 (`analog.arp.cardA/B`) |
| 6 | LINK | unlink | LINK clicked and `analog.arp.link` = 0, or *Already unlinked* |
| 7 | EDIT B, its pattern and rate | give B its own rhythm | a pattern or rate change on B, verified on B's own parameter (`analog.arp.cardB.pattern` / `.rate`), never the global one. The step selects EDIT B (a view setting); on EDIT A it pauses and says how to get back. |
| 8 | the panel | — | Finish: saved as complete for revision 1 |

## Trustworthy completion

- **Only the musician's gestures report events.** The ARP card, its light, the panel's rows, keys, chips and tabs report from the click handlers that write the parameter. RATE reports from the knob's user change; the attachment's own updates never notify.
- **Touches.** The MORPH knob and the routing board report touches through a mouse listener.
- **What never reports:**
  - automation and presets (session recall included);
  - undo;
  - programmatic calls such as `choosePattern`, `toggleCard` or `showQuick`;
  - the lesson's own Show me.
- **The postcondition.** After an event, the step checks the parameter (or the UI state) against its value before the gesture. While no gesture is under way the baseline follows the parameter, so a change by automation is never credited to a later touch.
- **One event, one step.** An event carries the step's generation, so it cannot complete a later step.

## Where progress lives

| Platform | Folder |
|---|---|
| macOS | `~/Library/Application Support/ANDOR/Learn/analog.json` |
| Windows | `%APPDATA%\ANDOR\Learn\analog.json` |
| Linux | `~/.config/ANDOR/Learn/analog.json` |
| Tests | `ANDOR_LEARN_DIR` overrides the folder |

Example:

```json
{
  "schema": 1,
  "product": "analog",
  "firstInviteSeen": true,
  "learnModeEnabled": false,
  "lessons": {
    "analog.arp.basics": { "revision": 1, "status": "complete", "completedAt": "2026-11-11T10:12:03.101Z" },
    "analog.fx.routing": { "revision": 1, "status": "in-progress", "lastStep": "tile" }
  }
}
```

- **Never in a project.** It is never in a DAW project, preset or the plugin state, and never in `Interface.xml`.
- **Private.** No samples, MIDI, file names, project names or host identity; nothing is sent anywhere.
- **Learn mode is not restored.** `learnModeEnabled` records the last choice, but V1 never turns Learn mode back on when the editor opens (a product choice).

## Testing it in a DAW

1. **Fresh profile.** Delete the Learn file above, or choose *Learn ▸ Reset learning progress*, to see the invitation again on the next editor open.
2. **Open Analog** (Standalone, AU or VST3). The invitation appears top right after about half a second. Check:
   - **Not now**: close and reopen the editor, and it does not come back;
   - with a second instance open, only one shows it.
3. **ARP lesson.** Menu ▸ *Learn ANDOR…* ▸ *Make a moving pattern*. Play a chord at any time; the lesson never plays notes. In each step:
   - **Mouse or trackpad:** do it with the mouse or the trackpad.
   - **Automation:** with automation reading on `arp.pattern` or `arp.rate`, let the host move it; the step must not complete.
   - **Preset:** recall a preset mid-step; nothing completes.
   - **Window:** resize the window and collapse / expand the ARP panel; the ring follows the control.
4. **Learn mode.** Menu ▸ *Learn mode*. Hover and click the preset bar, cards, macros, modulation, keyboard, ARP, Advanced and the routing icon. Escape twice to leave.
5. **Closing.** Close the editor mid-lesson, reopen it, and check that no lesson reappears and the browser shows *In progress*.
