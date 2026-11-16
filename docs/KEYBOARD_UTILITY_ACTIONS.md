# Keyboard utility actions — AUX D, Settings and Info

_ANDOR Analog and ANDOR OSP, shared code (`external/osp/shared/andor-ui`, `andor-shell`,
`andor-learn`). Branch `feature/aux-live-input` in both repositories._

## What changed

The large **Advanced** key beneath ARP / SUB (`layout::advanced`, 105 × 31) is replaced by a
row of small square ceramic keys: `osp::plugin::KeyboardUtilityActions`.

| Instrument   | Keys (left to right)        | Layout                                             |
|--------------|-----------------------------|----------------------------------------------------|
| ANDOR Analog | **D**, **SETTINGS**, **INFO** | three 31-unit keys, 6 apart, filling the 105 area |
| ANDOR OSP    | **SETTINGS**, **INFO**        | two 31-unit keys, centred; no gap for D            |

- Each key is a `juce::Button`: keyboard focus with an outline, an accessible title and
  description, a tooltip, and hover / pressed / active states. There are no captions.
- The group itself takes no clicks; only its keys do. It is 10 units larger on every side than
  the area, so the key shadows are never clipped.
- **D** (instruments with an AUX strip only):
  - The AUX badge's "D", in a restrained purple.
  - It shows or hides the AUX strip. It changes nothing else: D's power, source, sound and
    routing stay as they are.
  - It is active while the strip shows. Tooltip: "Show AUX D" / "Hide AUX D".
- **SETTINGS**:
  - A sliders icon. It opens and closes the existing Advanced popover, which now anchors to
    this key.
  - It is pressed while the popover is open. Tooltip: "Advanced Settings".
- **INFO**:
  - A circled "i", green while on. It turns Learn's Info on and off
    (`LearnController::setLearnMode`). Tooltip: "Show Info" / "Hide Info".
  - With Info on, pointing at a control outlines it, and after a 300 ms rest its explanation
    shows. Clicks still reach the control (the Info regions never intercept the mouse).
  - Enter on a focused region still opens its explanation.

### Menus

- Removed:
  - **Show / Hide AUX** from Analog's main menu (the AUX strip's own "Hide AUX" stays).
  - **Advanced settings** from OSP's main menu.
  - **Toggle Info** from the Learn submenu.
- The Learn submenu is now: Tutorials…, a separator, Reset Tutorials… (plus Restore Previous
  Sound while an example sound is in use).

### OSP gets ANDOR Learn

OSP had no Learn controller. To give it a working INFO key it now has one:

- The catalog is `apps/plugin/Source/OspLearn.cpp`, with product id `osp`.
- It has a seven-step Quick Start, which ends on the SETTINGS key.
- Help covers OSP's own controls. Nothing in OSP's catalog mentions AUX.
- If its catalog were ever invalid, the INFO key is removed rather than left dead.

### The popovers' proportions

MOD WHEEL, PITCH BEND and ADVANCED now use the compact `MiniPanel` (the SUB / MOVEMENT
proportions): a small 13 pt title, no close key, 58-unit knob cells and a 62-unit picture.

| Popover    | Size (units)          |
|------------|-----------------------|
| PITCH BEND | 250 wide              |
| MOD WHEEL  | 330 wide              |
| ADVANCED   | 300 wide, 6 rows      |

## Learn

- **Targets**:
  - `keyboard.auxToggle` (AUX products only; its reveal shows the strip and nothing else)
  - `keyboard.settings`
  - `keyboard.info`
  - The old id `advanced` is kept as an alias of `keyboard.settings`; `aux.strip` is unchanged.
- **Actions**: `aux.visibility.toggled`, `settings.opened`, `info.toggled`. They are sent only
  from the musician's own clicks on the keys.
- **Analog Quick Start** (revision 3 → 4, now eight steps):
  - Step 7, "Show AUX D", outlines the **D key**. Text: "Click D beneath ARP and SUB to show
    AUX D below the oscillator cards. AUX D can play rhythmic loops, noise, Kontakt
    instruments or audio from another DAW track."
  - The step is complete only when the musician's click shows the strip. If the strip is
    already showing: "AUX D is already visible." with Continue.
  - Step 8 then outlines the AUX strip itself.
- **Use external audio** (`analog.aux.live`, revision 2 → 3):
  - Step 1, "Show AUX D", outlines the D key: "Click D beneath the ARP/SUB controls to show
    AUX D. The strip will appear below OSC 1, OSC 2 and OSC C."
  - Then "Select LIVE INPUT" outlines the source dropdown.
- **Recovery when D is hidden** (MORPH, LIVE INPUT):
  - The first requirement is `aux.shown`: "Show AUX D" — "Click D below ARP and SUB to show
    the AUX controls. You can then check which sound D is using."
  - It outlines D. Its fix key only shows the strip.
  - Once the strip shows, the real problem is outlined on its own control: power, M, LEVEL,
    source, or no signal.
- **Help**:
  - Analog: "Advanced Settings", "Info" and "AUX D" (the D key).
  - OSP: "Advanced Settings" and "Info", with OSP's own texts.
- **Progress**:
  - Lesson ids are unchanged, and steps resume by their stable ids.
  - A completed lesson stays completed. The revision is recorded the next time it is run.
