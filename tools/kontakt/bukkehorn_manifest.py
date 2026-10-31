"""Builds the Bukkehorn 4.5 import manifest (docs/sample-source/bukkehorn-4.5-manifest.json) from
an nki_inspect.py dump: the 48 groups in the script's lane order (A 0-11, C 12-23, B 24-35,
D 36-47), each zone's sample, key / velocity range, root, tuning, level and loops, the saved
script state (each lane's selected group and the lane controls) and which samples were
supplied. Analysis only (milestone A1.0). Usage:
    python3 -I bukkehorn_manifest.py dump.json [supplied-dir] > manifest.json"""
import json, math, os, sys

LANES = [('A', 0), ('C', 12), ('B', 24), ('D', 36)]   # $A_START, $C_START, $B_START, $D_START (KSP)


def lane_of(index):
    for name, start in LANES:
        if start <= index < start + 12:
            return name, index - start
    return None, None


def plain(name):
    return ''.join(c for c in name.lower() if c.isalnum())


def cents(ratio):
    return round(1200.0 * math.log2(ratio), 2) if ratio > 0 else None


def main(dump_path, supplied_dir=None):
    r = json.load(open(dump_path))
    saved = r['script']['persistent']
    # The samples supplied so far (an upload's name may carry a prefix and _ for spaces).
    supplied = set(os.listdir(supplied_dir)) if supplied_dir else set()
    files = r['files']['samples']
    out = {
        'schemaVersion': 1,
        'source': {'nki': os.path.basename(r.get('nkiPath', 'Evenant Engine v2 - Finding Rhythm - Bukkehorn 4.5.nki')),
                   'program': r['programName'], 'programVolumeLinear': r.get('programVolume'), 'resource': r['files']['special'][0],
                   'sampleFolder': files[0].split('/')[0] if files else None},
        'savedState': {k.lstrip('$%'): v for k, v in saved.items()},
        'lanes': {name: {'firstGroup': start, 'selectedSlot': int(saved.get('$ENGINE_%s_SELECT' % name, 0)),
                         'selectedGroup': start + int(saved.get('$ENGINE_%s_SELECT' % name, 0))} for name, start in LANES},
        'groups': [],
    }
    for gi, g in enumerate(r['groups']):
        lane, slot = lane_of(gi)
        entry = {'index': gi, 'lane': lane, 'slot': slot, 'name': g['name'],
                 'volumeLinear': round(g['volume'], 6), 'volumeDb': round(20 * math.log10(g['volume']), 2) if g['volume'] > 0 else None,
                 'pan': round(g['pan'], 6), 'tuneRatio': g['tune'], 'zones': []}
        for z in (z for z in r['zones'] if z['group'] == gi):
            base = files[z['file']].split('/')[-1]
            entry['zones'].append({
                'file': base, 'fileIndex': z['file'], 'keyLo': z['keyLo'], 'keyHi': z['keyHi'], 'root': z['root'],
                'velLo': z['velLo'], 'velHi': z['velHi'], 'tuneCents': cents(z['tune']), 'volumeLinear': round(z['volume'], 6),
                'pan': round(z['pan'], 6), 'sampleStart': z['sampleStart'], 'sampleEnd': z['sampleEnd'], 'frames': z.get('frames'),
                'rate': z.get('rate'), 'channels': z.get('channels'), 'loops': z.get('loops', []),
                'supplied': any(plain(base) in plain(s) for s in supplied)})
        out['groups'].append(entry)
    json.dump(out, sys.stdout, indent=1)


if __name__ == '__main__':
    main(*sys.argv[1:])
