#!/usr/bin/env bash
# ANDOR Analog: the macOS retail installer (one .pkg: AU, VST3, standalone app), signed and
# notarised when credentials are present, otherwise an UNSIGNED but otherwise identical,
# signing-ready package (for the private beta and to prove the pipeline).
#
#   packaging/macos/build-installer.sh <artefacts dir> <version> <out dir> <notices file>
#
#   <artefacts dir>  build-plugin/osp/external-products/product/ANDOR_Analog_artefacts/Release
#
# Signing (all three, or none):
#   ANALOG_APP_IDENTITY        "Developer ID Application: <Name> (<TEAMID>)"
#   ANALOG_INSTALLER_IDENTITY  "Developer ID Installer: <Name> (<TEAMID>)"
#   ANALOG_NOTARY_PROFILE      a keychain profile (xcrun notarytool store-credentials), or
#   ANALOG_NOTARY_KEY / ANALOG_NOTARY_KEY_ID / ANALOG_NOTARY_ISSUER   an App Store Connect API key
# The script never prints secrets. Identifiers never change: com.osp.andoranalog.*.
set -euo pipefail

art="${1:?artefacts dir}"; version="${2:?version}"; out="${3:?out dir}"; notices="${4:?notices file}"
here="$(cd "$(dirname "$0")" && pwd)"
name="ANDOR Analog"
for b in "$art/AU/$name.component" "$art/VST3/$name.vst3" "$art/Standalone/$name.app"; do
    [ -d "$b" ] || { echo "missing $b" >&2; exit 2; }
done
[ -f "$notices" ] || { echo "missing $notices" >&2; exit 2; }

signed=0
if [ -n "${ANALOG_APP_IDENTITY:-}" ] && [ -n "${ANALOG_INSTALLER_IDENTITY:-}" ]; then signed=1; fi

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/au" "$stage/vst3" "$stage/app" "$stage/docs/ANDOR Analog" "$stage/resources" "$out"
cp -R "$art/AU/$name.component" "$stage/au/"
cp -R "$art/VST3/$name.vst3" "$stage/vst3/"
cp -R "$art/Standalone/$name.app" "$stage/app/"
cp "$notices" "$stage/docs/ANDOR Analog/THIRD_PARTY_NOTICES.txt"
cp "$here/../common/EULA-DRAFT.txt" "$stage/docs/ANDOR Analog/EULA.txt"
cp "$here/../common/EULA-DRAFT.txt" "$stage/resources/license.txt"
xattr -cr "$stage" 2>/dev/null || true

# Code signing: Developer ID with the hardened runtime and a secure timestamp. The standalone
# needs no entitlements (no microphone, no network). Without an identity: an ad-hoc signature,
# so the bundles are well formed (Apple silicon refuses unsigned code) but not trusted.
for b in "$stage/au/$name.component" "$stage/vst3/$name.vst3" "$stage/app/$name.app"; do
    if [ "$signed" = 1 ]; then
        codesign --force --deep --options runtime --timestamp --sign "$ANALOG_APP_IDENTITY" "$b"
    else
        codesign --force --deep --sign - "$b"
    fi
    codesign --verify --deep --strict "$b"
done

pkgbuild --root "$stage/au"   --install-location "/Library/Audio/Plug-Ins/Components" --identifier com.osp.andoranalog.au   --version "$version" "$stage/au.pkg"
pkgbuild --root "$stage/vst3" --install-location "/Library/Audio/Plug-Ins/VST3"       --identifier com.osp.andoranalog.vst3 --version "$version" "$stage/vst3.pkg"
pkgbuild --root "$stage/app"  --install-location "/Applications"                       --identifier com.osp.andoranalog.app  --version "$version" "$stage/app.pkg"
pkgbuild --root "$stage/docs" --install-location "/Library/Application Support"         --identifier com.osp.andoranalog.docs --version "$version" "$stage/docs.pkg"

cat > "$stage/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>ANDOR Analog $version</title>
    <license file="license.txt"/>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <os-version min="11.0"/>
    <choices-outline>
        <line choice="au"/><line choice="vst3"/><line choice="app"/><line choice="docs"/>
    </choices-outline>
    <choice id="au" title="Audio Unit (Logic Pro, GarageBand, Ableton Live)"><pkg-ref id="com.osp.andoranalog.au"/></choice>
    <choice id="vst3" title="VST3 (Ableton Live, Cubase, REAPER, Bitwig, Studio One)"><pkg-ref id="com.osp.andoranalog.vst3"/></choice>
    <choice id="app" title="Standalone application"><pkg-ref id="com.osp.andoranalog.app"/></choice>
    <choice id="docs" title="Licence and notices" visible="false"><pkg-ref id="com.osp.andoranalog.docs"/></choice>
    <pkg-ref id="com.osp.andoranalog.au">au.pkg</pkg-ref>
    <pkg-ref id="com.osp.andoranalog.vst3">vst3.pkg</pkg-ref>
    <pkg-ref id="com.osp.andoranalog.app">app.pkg</pkg-ref>
    <pkg-ref id="com.osp.andoranalog.docs">docs.pkg</pkg-ref>
</installer-gui-script>
XML

suffix=""; [ "$signed" = 1 ] || suffix="-UNSIGNED"
pkg="$out/ANDOR-Analog-$version-macOS$suffix.pkg"
if [ "$signed" = 1 ]; then
    productbuild --distribution "$stage/distribution.xml" --resources "$stage/resources" --package-path "$stage" \
                 --sign "$ANALOG_INSTALLER_IDENTITY" --timestamp "$pkg"
else
    productbuild --distribution "$stage/distribution.xml" --resources "$stage/resources" --package-path "$stage" "$pkg"
fi

if [ "$signed" = 1 ]; then
    if [ -n "${ANALOG_NOTARY_PROFILE:-}" ]; then
        xcrun notarytool submit "$pkg" --keychain-profile "$ANALOG_NOTARY_PROFILE" --wait
    elif [ -n "${ANALOG_NOTARY_KEY:-}" ]; then
        key="$stage/notary.p8"; printf '%s' "$ANALOG_NOTARY_KEY" > "$key"
        xcrun notarytool submit "$pkg" --key "$key" --key-id "$ANALOG_NOTARY_KEY_ID" --issuer "$ANALOG_NOTARY_ISSUER" --wait
    else
        echo "signed but NOT notarised (no notary credentials)" >&2
    fi
    xcrun stapler staple "$pkg" || echo "stapling failed (not notarised?)" >&2
    spctl --assess --type install -vv "$pkg" || true
fi
pkgutil --check-signature "$pkg" || true
( cd "$out" && shasum -a 256 "$(basename "$pkg")" > "$(basename "$pkg").sha256" )
echo "installer: $pkg (signed=$signed)"
