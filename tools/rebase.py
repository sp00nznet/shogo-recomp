#!/usr/bin/env python3
"""Map every module Shogo runs to a fixed, non-overlapping VA.

Client.exe is the LithTech engine. Everything else it loads was linked at
0x10000000: the renderers (d3d.ren, soft.ren), the server (server.dll), and the game
code that ships inside SHOGOP3.REZ (the 2.2 patch's CSHELL.DLL, CRES.DLL,
SRES.DLL, OBJECT.LTO). The real loader relocates all but one at run time; a
static recompilation cannot, because the lifted C has every absolute address
baked in. So they are relocated here, once, before disassembly, and the host
maps each copy at exactly this base (MODULES in src/runtime/host.c).

    py -3 tools/rebase.py            # game/ -> work/rebased/
"""
import os
import sys
import pefile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import rez  # noqa: E402

REZ = 'SHOGOP3.REZ'        # the 2.2 game code; Shogo.exe passes it last
# module -> base. Keep in step with src/runtime/host.c.
MODULES = {
    'Client.exe':  0x00400000,   # linked base: unchanged, no relocations
    'server.dll':  0x10000000,   # linked base: unchanged
    'd3d.ren':     0x10100000,
    'CSHELL.DLL':  0x10200000,
    'CRES.DLL':    0x10300000,
    'OBJECT.LTO':  0x10400000,
    'SRES.DLL':    0x10600000,
    'soft.ren':    0x10700000,   # the software renderer, LithTech's fallback
}
# Byte patches applied to the relocated images, before lifting: (module, VA,
# the original bytes, the new ones). Each is checked against the original.
PATCHES = [
    # DirectInput: the keyboard and mouse ask for DISCL_FOREGROUND (5: with
    # DISCL_EXCLUSIVE, 6: without). A foreground device reads nothing while
    # the window is not in front, which is every run the test harness makes
    # (--key), so both become DISCL_BACKGROUND | DISCL_NONEXCLUSIVE (0xA).
    ('Client.exe', 0x0042B232, bytes([0x6A, 0x05]), bytes([0x6A, 0x0A])),
    ('Client.exe', 0x0042B236, bytes([0x6A, 0x06]), bytes([0x6A, 0x0A])),
]
IN_REZ = {'CSHELL.DLL', 'CRES.DLL', 'SRES.DLL', 'OBJECT.LTO'}


def rebase(raw, base):
    """The file's bytes with every HIGHLOW fixup applied for `base` (by hand:
    pefile's relocate_image() also rewrites the import thunks it parsed)."""
    pe = pefile.PE(data=raw)
    data = bytearray(pe.__data__)
    delta = (base - pe.OPTIONAL_HEADER.ImageBase) & 0xFFFFFFFF
    if delta:
        for block in getattr(pe, 'DIRECTORY_ENTRY_BASERELOC', ()):
            for e in block.entries:
                if e.type == 3:                      # IMAGE_REL_BASED_HIGHLOW
                    off = pe.get_offset_from_rva(e.rva)
                    v = int.from_bytes(data[off:off + 4], 'little')
                    data[off:off + 4] = ((v + delta) & 0xFFFFFFFF).to_bytes(4, 'little')
                elif e.type != 0:
                    raise ValueError('relocation type %d' % e.type)
        off = pe.OPTIONAL_HEADER.get_file_offset() + 28   # ImageBase
        data[off:off + 4] = base.to_bytes(4, 'little')
    return bytes(data), pe.OPTIONAL_HEADER.SizeOfImage


def main():
    game = os.path.join(ROOT, 'game')
    out = os.path.join(ROOT, 'work', 'rebased')
    os.makedirs(out, exist_ok=True)
    spans = []
    for name, base in MODULES.items():
        raw = rez.read(os.path.join(game, REZ), name) if name in IN_REZ else \
            open(os.path.join(game, name), 'rb').read()
        data, size = rebase(raw, base)
        data = bytearray(data)
        pe = pefile.PE(data=bytes(data), fast_load=True)
        for mod, va, old, new in PATCHES:
            if mod == name:
                off = pe.get_offset_from_rva(va - base)
                if data[off:off + len(old)] != old:
                    sys.exit(f'{name}: patch at {va:#x} does not match')
                data[off:off + len(new)] = new
        spans.append((base, base + size, name))
        with open(os.path.join(out, name), 'wb') as f:
            f.write(data)
        print(f'{name:12} {base:#010x}-{base + size:#010x}')
    spans.sort()
    for (a0, a1, n), (b0, _, m) in zip(spans, spans[1:]):
        if a1 > b0:
            sys.exit(f'{n} overlaps {m}')


if __name__ == '__main__':
    sys.exit(main())
