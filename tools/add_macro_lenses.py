#!/usr/bin/env python3
"""
ANDOR ANALOG's macro lenses: the six rendered glass lights under the CORE macros, in ANALOG's
own palette (its MORPH / DRIVE / FILTER / MOVEMENT / ECHO / SPACE arcs), added to its walnut
skin pack. The pack's pieces replace ANDOR OSP's of the same name in this product only
(osp::plugin::design::Materials::pack), so ANDOR OSP keeps its own palette.

Why: ANDOR OSP re-rendered its CORE lenses in a new palette (OSP c010a8f4, "CORE macros: the
harmonised palette, with re-rendered beads"). ANALOG keeps its own macro colours, so the shared
lenses no longer matched its arcs. The lenses ANALOG shipped with are those of ANDOR OSP's
ceramic skin before that change (any revision up to b522a06): they are copied, pieces and
manifest entries, unchanged.

Usage: tools/add_macro_lenses.py <ceramic-skin.zip with ANALOG's palette> <walnut-skin.zip>
       (the walnut pack is rewritten in place; its other pieces are kept as they are)

  git -C external/osp show b522a06e:shared/andor-ui/Resources/skin/ceramic-skin.zip > /tmp/c.zip
  tools/add_macro_lenses.py /tmp/c.zip product/Resources/walnut-skin.zip
"""

import io
import json
import sys
import zipfile

LENSES = [f"led-{name}-{state}" for name in ("life", "drive", "character", "motion", "space", "echo") for state in ("lit", "unlit")]


def main(source_path, walnut_path):
    with zipfile.ZipFile(source_path) as source:
        source_manifest = json.loads(source.read("manifest.json"))
        pieces = {p["name"]: p for p in source_manifest["pieces"]}
        missing = [n for n in LENSES if n not in pieces]
        if missing:
            sys.exit(f"not in {source_path}: {', '.join(missing)}")
        files = {level["file"]: source.read(level["file"]) for n in LENSES for level in pieces[n]["levels"]}

    with zipfile.ZipFile(walnut_path) as walnut:
        manifest = json.loads(walnut.read("manifest.json"))
        kept = {name: walnut.read(name) for name in walnut.namelist() if name != "manifest.json"}

    manifest["pieces"] = [p for p in manifest["pieces"] if p["name"] not in LENSES] + [pieces[n] for n in LENSES]
    for name in list(kept):
        if any(name == level["file"] for n in LENSES for level in pieces[n]["levels"]):
            del kept[name]

    out = io.BytesIO()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        # Fixed timestamps: the same pack every run.
        def add(name, data):
            info = zipfile.ZipInfo(name, date_time=(2026, 11, 20, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data)

        add("manifest.json", json.dumps(manifest, indent=1).encode() + b"\n")
        for name in sorted(kept):
            add(name, kept[name])
        for name in sorted(files):
            add(name, files[name])
    with open(walnut_path, "wb") as f:
        f.write(out.getvalue())
    print(f"{walnut_path}: {len(LENSES)} lens pieces ({len(files)} images) from {source_path}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
