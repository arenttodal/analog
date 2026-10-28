#!/bin/bash
# Installs the ANDOR Analog test build for the current user (no admin rights needed).
# Double-click in Finder, or run from Terminal. Unsigned test build: this also removes the
# download quarantine so macOS lets the plug-in load. OSP and the other ANDOR instruments are
# not touched (different plug-in codes and bundle names).
set -e
here="$(cd "$(dirname "$0")" && pwd)"
xattr -cr "$here" 2>/dev/null || true
mkdir -p ~/Library/Audio/Plug-Ins/Components ~/Library/Audio/Plug-Ins/VST3 ~/Applications
name="ANDOR Analog"
rm -rf ~/Library/Audio/Plug-Ins/Components/"$name.component" ~/Library/Audio/Plug-Ins/VST3/"$name.vst3"
cp -R "$here/$name.component" ~/Library/Audio/Plug-Ins/Components/
cp -R "$here/$name.vst3" ~/Library/Audio/Plug-Ins/VST3/
if [ -d "$here/$name.app" ]; then
    rm -rf ~/Applications/"$name.app"
    cp -R "$here/$name.app" ~/Applications/
fi
# Make Logic and other AU hosts rescan.
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo "Installed ANDOR Analog: AU and VST3 in ~/Library/Audio/Plug-Ins, the standalone app in ~/Applications."
echo "The factory presets are written to ~/Documents/ANDOR Analog/Presets/Factory the first time it opens."
echo "Restart your DAW. In Logic: Instrument > AU Instruments > OSP > ANDOR Analog."
