#!/usr/bin/env python3
"""LithTech 1.0 REZ archives ("RezMgr Version 1"): list them, or pull files out.

The game code (CSHELL.DLL, CRES.DLL, SRES.DLL, OBJECT.LTO) ships inside the
.rez files, not beside Client.exe; the engine copies them to temp files and
LoadLibrary's those.

    py -3 tools/rez.py game/SHOGOP3.REZ              # list
    py -3 tools/rez.py game/SHOGOP3.REZ out dll,lto  # extract by extension
"""
import os
import struct
import sys


def entries(path):
    """(name with extension, offset, size) for every resource, depth first."""
    with open(path, 'rb') as f:
        f.seek(0x7F)                                   # after the three text lines + ^Z
        _ver, pos, size = struct.unpack('<III', f.read(12))
        yield from _walk(f, pos, size, [])


def _walk(f, pos, size, path):
    f.seek(pos)
    d, i = f.read(size), 0
    while i < len(d):
        kind, p, sz, _t = struct.unpack_from('<IIII', d, i)
        i += 16
        if kind == 1:                                  # a directory
            j = d.index(b'\0', i)
            name, i = d[i:j].decode('latin1'), j + 1
            yield from _walk(f, p, sz, path + [name])
            f.seek(0)
            continue
        _id, typ, nkeys = struct.unpack_from('<III', d, i)
        i += 12
        j = d.index(b'\0', i)
        name, i = d[i:j].decode('latin1'), j + 1
        i = d.index(b'\0', i) + 1 + 4 * nkeys          # description, keys
        ext = struct.pack('<I', typ).rstrip(b'\0').decode('latin1')[::-1]
        yield '/'.join(path + [name + '.' + ext]), p, sz


def read(path, name):
    for n, p, sz in entries(path):
        if n.lower() == name.lower():
            with open(path, 'rb') as f:
                f.seek(p)
                return f.read(sz)
    raise KeyError('%s: no %s' % (path, name))


def main():
    want = sys.argv[3].lower().split(',') if len(sys.argv) > 3 else None
    for n, p, sz in entries(sys.argv[1]):
        if want and n.rsplit('.', 1)[-1].lower() not in want:
            continue
        print(n, sz)
        if len(sys.argv) > 2:
            out = os.path.join(sys.argv[2], n)
            os.makedirs(os.path.dirname(out) or '.', exist_ok=True)
            with open(sys.argv[1], 'rb') as f:
                f.seek(p)
                open(out, 'wb').write(f.read(sz))


if __name__ == '__main__':
    main()
