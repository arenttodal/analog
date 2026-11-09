# ANDOR Analog private beta — tester guide

Thank you for testing ANDOR Analog before release. This build is a **release candidate**: it
should work. Please try to break it, and tell us what you find.

## What you get

| Package | Install |
|---|---|
| `ANDOR-Analog-<version>-macOS-UNSIGNED.pkg` (or the signed `.pkg` once available) | The beta package is not yet notarised: **right-click › Open**, then Open. If macOS still refuses, open *System Settings › Privacy & Security* and click *Open Anyway*. |
| `ANDOR-Analog-<version>-Windows-Setup.exe` | If SmartScreen warns about an unknown publisher: *More info › Run anyway*. |
| `ANDOR-Analog-macOS-test.zip` / `-Windows-test.zip` | The same plug-ins without an installer, for a per-user install with `install.command` / `Install.bat`. |

The user guide is `MANUAL.md` (a PDF in later builds).

## What to test, in your own DAW

1. Scan and load ANDOR Analog on an instrument track. Play the factory presets.
2. Automate two or three knobs (FILTER, MORPH, a SPACE setting). Play the project back.
3. Save the project, quit the DAW, reopen: same sound? Same automation?
4. Bounce/export offline and in real time: do the two match what you heard?
5. Change the sample rate (44.1 / 48 / 96 kHz) and the buffer size during playback.
6. Drop a recording on card C (SAMPLE), play it, save, move the original file, reopen the project.
7. Try MORPH, the arpeggiator synced to your tempo, and the routing view (CUSTOM chains, RETRO,
   CONVOLUTION: does your DAW stay in sync?).
8. Resize the window, from the smallest to the largest.
9. Open several instances; watch your CPU meter.
10. Anything you would do in a real session.

## How to report

One report per problem, to [BETA FEEDBACK ADDRESS]:

```
Title:       one line
Build:       ANDOR Analog <version> (from the installer's file name)
System:      macOS 15.1 / Windows 11 23H2, CPU (M2, i7-...), RAM
DAW:         name and version, plug-in format (AU / VST3 / standalone)
Steps:       1. ... 2. ... 3. ...
Expected:    what should have happened
Happened:    what did happen (attach a screenshot, audio clip or the project if you can)
How often:   always / sometimes / once
```

**Crashes:**
- **macOS:** attach the crash report from *Console › Crash Reports* (the file names the host).
- **Windows:** attach the Event Viewer's *Application Error* entry.

## Please note

- Beta builds expire with the beta. You will be offered the release version.
- Do not share the installers.
- Your projects and presets are yours. Factory presets live in
  `Documents/ANDOR Analog/Presets/Factory`; save your own one level up.
