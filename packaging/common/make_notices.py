#!/usr/bin/env python3
"""Writes THIRD_PARTY_NOTICES.txt for an ANDOR installer from the licence files of the exact
sources the build compiled (JUCE and its bundled libraries, the VST3 SDK, Signalsmith Stretch
and Linear, the Inter and Outfit fonts, SQLite).

    make_notices.py --build <cmake build dir> --osp <ANDOR OSP checkout> --out THIRD_PARTY_NOTICES.txt

Fails (exit 1) if any licence text is missing, so a package never ships without its notices.
"""
import argparse
import sys
from pathlib import Path


def find_dep(build: Path, name: str, fallback: str | None) -> Path:
    for p in (build / "_deps" / f"{name}-src", Path(fallback) if fallback else None):
        if p and p.is_dir():
            return p
    sys.exit(f"make_notices: {name} sources not found under {build}/_deps (pass --{name.split('-')[-1]})")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", required=True)
    ap.add_argument("--osp", required=True, help="ANDOR OSP checkout (fonts)")
    ap.add_argument("--juce")
    ap.add_argument("--stretch")
    ap.add_argument("--linear")
    ap.add_argument("--product", default="ANDOR Analog")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    build = Path(a.build)
    juce = find_dep(build, "juce", a.juce)
    stretch = find_dep(build, "signalsmith-stretch", a.stretch)
    linear = find_dep(build, "signalsmith-linear", a.linear)
    fonts = Path(a.osp) / "shared" / "andor-ui" / "Resources" / "fonts"
    m = juce / "modules"
    # (title, licence file) for everything compiled into the plug-ins and the app.
    sections = [
        ("JUCE 8 (framework; used under the JUCE 8 commercial licence held by the publisher)", juce / "LICENSE.md"),
        ("Steinberg VST3 SDK (VST is a registered trademark of Steinberg Media Technologies GmbH)",
         m / "juce_audio_processors" / "format_types" / "VST3_SDK" / "LICENSE.txt"),
        ("Apple AudioUnitSDK (macOS Audio Unit)", m / "juce_audio_plugin_client" / "AU" / "AudioUnitSDK" / "LICENSE.txt"),
        ("FLAC", m / "juce_audio_formats" / "codecs" / "flac" / "Flac Licence.txt"),
        ("zlib", m / "juce_core" / "zip" / "zlib" / "README"),
        ("libpng", m / "juce_graphics" / "image_formats" / "pnglib" / "LICENSE"),
        ("Independent JPEG Group's jpeglib", m / "juce_graphics" / "image_formats" / "jpglib" / "README"),
        ("HarfBuzz", m / "juce_graphics" / "fonts" / "harfbuzz" / "COPYING"),
        ("SheenBidi", m / "juce_graphics" / "unicode" / "sheenbidi" / "LICENSE"),
        ("Signalsmith Stretch", stretch / "LICENSE.txt"),
        ("Signalsmith Linear", linear / "LICENSE.txt"),
        ("Inter typeface (SIL Open Font License 1.1)", fonts / "Inter-OFL.txt"),
        ("Outfit typeface (SIL Open Font License 1.1)", fonts / "Outfit-OFL.txt"),
    ]
    missing = [str(p) for _, p in sections if not p.is_file()]
    if missing:
        print("make_notices: missing licence files:\n  " + "\n  ".join(missing), file=sys.stderr)
        return 1
    out = [f"{a.product}: third-party software notices", "=" * 72, "",
           f"{a.product} includes the following third-party software and fonts. Their copyright",
           "notices and licence terms are reproduced below, as those licences require.",
           "", "SQLite: the SQLite library is in the public domain (https://sqlite.org/copyright.html).", ""]
    for title, path in sections:
        out += ["", "-" * 72, title, "-" * 72, "", path.read_text(encoding="utf-8", errors="replace").strip(), ""]
    Path(a.out).write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"wrote {a.out} ({len(sections)} notices)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
