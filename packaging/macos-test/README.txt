ANDOR Analog test build for macOS (universal: Apple Silicon and Intel)

1. Unzip this folder.
2. Double-click install.command. If macOS refuses to open it, right-click it, choose
   Open, then Open again. Or run in Terminal:  bash install.command
3. Restart your DAW.
   - Logic: Instrument slot > AU Instruments > OSP > ANDOR Analog
   - Ableton Live / Reaper: rescan plug-ins, then use the VST3 or AU "ANDOR Analog"
   - Or open ~/Applications/ANDOR Analog.app (standalone, choose your audio device in Options)

The 34 factory sounds appear in the preset list (Presets/Factory) after the first launch.

This build is not signed or notarised (that needs a Developer ID). install.command
removes the download quarantine so macOS lets it load.

Uninstall: delete "ANDOR Analog.component" from ~/Library/Audio/Plug-Ins/Components,
"ANDOR Analog.vst3" from ~/Library/Audio/Plug-Ins/VST3, "ANDOR Analog.app" from
~/Applications, and (if you like) ~/Documents/ANDOR Analog.
