"""A bounded reader for one Kontakt 5+ NKI (NI container): enough to list its program, groups,
zones (key / velocity ranges, roots, tuning, loops), file list and script (with its persistent
values). Analysis only (ANDOR ANALOG sample-source importer, milestone A1.0); written from the
documented format (ConvertWithMoss's readers, LGPL, used as format documentation; no code
copied). Read-only. Usage: python3 -I nki_inspect.py <file.nki> > dump.json"""
import io, json, struct, sys

def u16(b): return struct.unpack('<H', b.read(2))[0]
def u32(b): return struct.unpack('<I', b.read(4))[0]
def s32(b): return struct.unpack('<i', b.read(4))[0]
def s16(b): return struct.unpack('<h', b.read(2))[0]
def f32(b): return struct.unpack('<f', b.read(4))[0]
def u64(b): return struct.unpack('<Q', b.read(8))[0]
def utf16(b):
    n = u32(b)
    return b.read(2 * n).decode('utf-16-le', 'replace')

def fastlz(data, size):
    level = (data[0] >> 5) + 1
    out = bytearray()
    ip = 0
    ctrl = data[ip] & 31; ip += 1
    while True:
        if ctrl >= 32:
            ln = (ctrl >> 5) - 1
            ofs = (ctrl & 31) << 8
            ref = len(out) - ofs
            if ln == 6:
                if level == 1:
                    ln += data[ip]; ip += 1
                else:
                    while True:
                        c = data[ip]; ip += 1; ln += c
                        if c != 255: break
            if level == 1:
                ref -= data[ip]; ip += 1
            else:
                c = data[ip]; ip += 1; ref -= c
                if c == 255 and ofs == (31 << 8):
                    ofs = (data[ip] << 8) | data[ip + 1]; ip += 2
                    ref = len(out) - ofs - 8191
            ref -= 1
            for _ in range(ln + 3):
                out.append(out[ref]); ref += 1
            if ip >= len(data): break
            ctrl = data[ip]; ip += 1
        else:
            n = ctrl + 1
            out += data[ip:ip + n]; ip += n
            if ip >= len(data): break
            ctrl = data[ip]; ip += 1
    if len(out) != size:
        raise ValueError(f'fastlz size {len(out)} != {size}')
    return bytes(out)

CHUNK_NAMES = {1: 'Terminator', 101: 'AuthoringApplication', 106: 'Authorization', 108: 'Soundinfo', 109: 'PresetChunk',
               115: 'SubTree', 116: 'Encryption', 118: 'ContainerRoot', 121: 'ControllerAssignments', 120: 'AutomationParameters'}

def read_item(b, found, depth=0):
    size = u64(b)
    blk = io.BytesIO(b.read(size - 8))
    assert u32(blk) == 1
    assert blk.read(4) == b'hsin', 'no hsin'
    u32(blk); u32(blk); blk.read(16)
    read_data_chunk(blk, found, depth)
    u32(blk)  # version
    for _ in range(u32(blk)):
        u32(blk); blk.read(4); u32(blk)
        read_item(blk, found, depth + 1)

def read_data_chunk(b, found, depth):
    size = u64(b)
    blk = io.BytesIO(b.read(size - 8))
    dom = blk.read(4); typ = u32(blk); ver = u32(blk)
    found.setdefault('chunks', []).append((depth, CHUNK_NAMES.get(typ, typ)))
    if typ != 1:
        read_data_chunk(blk, found, depth)
    rest = blk.read()
    d = io.BytesIO(rest)
    if typ == 109:  # preset chunk
        u32(d); found['dictType'] = u32(d); n = u32(d); sz = u32(d); u32(d)
        found['preset'] = d.read(sz)
    elif typ == 115:  # sub tree
        u32(d)
        if d.read(1)[0] > 0:
            us = u32(d); cs = u32(d)
            sub = fastlz(d.read(cs), us)
            read_item(io.BytesIO(sub), found, depth + 1)
        else:
            read_item(d, found, depth + 1)
    elif typ == 101:
        found['authoring'] = rest[:64].hex()
    elif typ == 106:
        found['authorization'] = len(rest)

# Kontakt preset chunks
GROUP, PAR_SCRIPT, PROGRAM, GROUP_LIST, ZONE_LIST, FILENAME_LIST, FILENAME_LIST_EX, LOOP_ARRAY = 0x04, 0x06, 0x28, 0x33, 0x34, 0x3D, 0x4B, 0x39
STRUCTURED = {0x03, 0x29, 0x28, 0x06, 0x17, 0x32, 0x3A, 0x45, 0x47, 0x4E}

class PC:
    def __init__(self): self.id = -1; self.pub = b''; self.priv = b''; self.ver = -1; self.kids = []
    def __repr__(self): return f'PC({self.id:#x}, v{self.ver:#x}, pub{len(self.pub)}, kids{len(self.kids)})'

def read_pc(b):
    c = PC(); c.id = u16(b); sz = u32(b); o = io.BytesIO(b.read(sz))
    if c.id == GROUP_LIST:
        read_array(o, c, False); [setattr(k, 'id', GROUP) for k in c.kids]
    elif c.id == ZONE_LIST:
        read_array(o, c, True)
    elif c.id in STRUCTURED:
        read_struct(o, c, sz)
    elif c.id in (0x3B, 0x3C):
        n = 16 if c.id == 0x3B else 32
        o.read(1); c.ver = u16(o)
        for _ in range(n):
            if o.read(1)[0] > 0:
                k = PC(); k.id = u16(o); k.pub = o.read(u32(o)); c.kids.append(k)
    else:
        c.pub = o.read()
    return c

def read_array(o, c, ref):
    for _ in range(u32(o)):
        k = PC(); k.id = u32(o) if ref else -1
        read_struct(o, k, 0); c.kids.append(k)

def read_struct(o, c, sz):
    if o.read(1)[0] == 0:
        c.pub = o.read(sz - 1) if sz > 0 else b''
        return
    c.ver = u16(o)
    c.priv = o.read(u32(o)); c.pub = o.read(u32(o))
    kids = io.BytesIO(o.read(u32(o)))
    while kids.tell() < len(kids.getbuffer()):
        c.kids.append(read_pc(kids))

def find_all(chunks, cid):
    out = []
    for c in chunks:
        if c.id == cid: out.append(c)
        else: out += find_all(c.kids, cid)
    return out

def parse_group(c):
    b = io.BytesIO(c.pub)
    g = {'name': utf16(b), 'volume': f32(b), 'pan': f32(b), 'tune': f32(b)}
    g['keyTracking'] = b.read(1)[0] > 0; g['reverse'] = b.read(1)[0] > 0
    g['releaseTrigger'] = b.read(1)[0] > 0; b.read(1); s32(b)
    g['midiChannel'] = s16(b); g['voiceGroup'] = s32(b); s32(b)
    g['muted'] = b.read(1)[0] > 0; g['soloed'] = b.read(1)[0] > 0; g['interpQuality'] = s32(b)
    g['version'] = hex(c.ver)
    return g

def parse_zone(c):
    b = io.BytesIO(c.pub); v = c.ver
    z = {'group': c.id, 'version': hex(v), 'sampleStart': u32(b), 'sampleEnd': u32(b)}
    u32(b)
    z['velLo'] = u16(b); z['velHi'] = u16(b); z['keyLo'] = u16(b); z['keyHi'] = u16(b)
    z['fadeVelLo'] = u16(b); z['fadeVelHi'] = u16(b); z['fadeKeyLo'] = u16(b); z['fadeKeyHi'] = u16(b)
    z['root'] = u16(b); z['volume'] = f32(b); z['pan'] = f32(b); z['tune'] = f32(b)
    if v >= 0x9A:
        b.read(2); u32(b)
        if b.tell() >= len(c.pub): return z
    z['file'] = u32(b); z['bits'] = u32(b); z['rate'] = u32(b); z['channels'] = b.read(1)[0]; z['frames'] = u32(b)
    u32(b)
    if v <= 0x93: u32(b)
    z['rootNote2'] = u32(b); z['tuning'] = f32(b); b.read(1); z['sampleSize'] = u32(b)
    z['loops'] = []
    for k in c.kids:
        if k.id != LOOP_ARRAY or len(k.pub) < 2: continue
        lb = io.BytesIO(k.pub); enabled = u16(lb)
        for i in range(8):
            if enabled & (1 << i):
                if u16(lb) != 0x60: raise ValueError('loop marker')
                loop = {'mode': s32(lb), 'start': u32(lb), 'length': u32(lb), 'count': u32(lb),
                        'alternating': lb.read(1)[0] > 0, 'tuning': f32(lb), 'xfade': u32(lb)}
                if lb.tell() < len(k.pub): lb.read(1)
                z['loops'].append(loop)
    return z

def parse_script(pub):
    # A script slot: a marker, the source (length-prefixed text), then the script's persistent
    # variables as saved ("$name value" strings).
    b = io.BytesIO(pub)
    b.read(2)
    source = b.read(u32(b)).decode('latin1')
    out = {'sourceLength': len(source), 'source': source, 'persistent': {}}
    if b.tell() + 19 <= len(pub):
        b.read(15)
        for _ in range(u32(b)):
            name, _, value = b.read(u32(b)).decode('latin1').partition(' ')
            out['persistent'][name] = value
    return out

def read_file(b):
    parts = []
    for _ in range(u32(b)):
        t = b.read(1)[0]
        if t == 0: d = b.read(2).decode('ascii').strip(); parts.append('/' if not d else d + ':/')
        elif t == 1: d = utf16(b); parts.append('/' if not d else d + ':/')
        elif t == 2: parts.append(utf16(b) + '/')
        elif t == 3: parts.append('../')
        elif t in (4, 8, 9): parts.append(utf16(b))
        elif t == 6: pass
        else: raise ValueError(f'segment {t}')
    return ''.join(parts)

def parse_files(c):
    b = io.BytesIO(c.pub)
    if c.id == FILENAME_LIST_EX:
        ver = u16(b)
        if ver == 3:
            n = s32(b); b.read(8); files = []
            for i in range(n - 1):
                files.append(read_file(b))
                if b.tell() < len(c.pub): b.read(4); b.read(8); b.read(20)
            return {'version': 3, 'samples': files}
    special = [read_file(b) for _ in range(s32(b))]
    samples = [read_file(b) for _ in range(s32(b))] if b.tell() < len(c.pub) else []
    return {'special': special, 'samples': samples}

def main(path):
    data = open(path, 'rb').read()
    found = {}
    read_item(io.BytesIO(data), found)
    preset = found.get('preset')
    report = {'containerChunks': found.get('chunks'), 'authorization': found.get('authorization')}
    if preset is None:
        print(json.dumps(report, indent=1)); return
    tops = []
    b = io.BytesIO(preset)
    while b.tell() < len(preset):
        tops.append(read_pc(b))
    report['topChunks'] = [repr(c) for c in tops]
    progs = find_all(tops, PROGRAM)
    for p in progs:
        pb = io.BytesIO(p.pub)
        report['programName'] = utf16(pb)
        gl = [k for k in p.kids if k.id == GROUP_LIST]
        report['groups'] = [parse_group(g) for g in (gl[0].kids if gl else [])]
        zl = find_all(p.kids, ZONE_LIST)
        report['zones'] = [parse_zone(z) for zlist in zl for z in zlist.kids]
        scripts = [k for k in p.kids if k.id == PAR_SCRIPT]
        report['scriptChunks'] = [repr(s) for s in scripts]
        report['scriptSlots'] = [len(s.pub) for s in scripts]
        if scripts:
            report['script'] = parse_script(scripts[0].pub)
        report['programChildren'] = [repr(k) for k in p.kids]
    fl = [c for c in tops if c.id in (FILENAME_LIST, FILENAME_LIST_EX)]
    if fl: report['files'] = parse_files(fl[0])
    print(json.dumps(report, indent=1, default=str))

if __name__ == '__main__':
    main(sys.argv[1])
