#!/usr/bin/env python3
"""Changes the colour of one side (player colour) in Hostile Takeover.

Each side's units are drawn with a ramp of 5 palette entries, brightest
first. This sets the brightest ("main") entry to the colour you give and
scales the other four to keep the ramp's original light-to-dark steps, in:

  - every palette (.palbin record) in game/htdata832.pdb, the game data the
    Windows build ships, and
  - the source palettes data/preload_8bpp.pal and data/art824/common_8bpp.pal,
    so a data rebuild (the editor's Rebuild Built-in Missions) keeps it.

The art itself is untouched: side-coloured pixels are stored as "side codes"
that the game maps onto the ramp at run time.

Usage (from the repository root):
    python3 tools/set_side_color.py 1 99CCFF
"""

import struct
import sys

# Palette entries of each side's ramp (see kiclr*Side* in game/res.h and
# gaclr8bpp in game/misc.cpp). Side 0 is neutral and isn't a player colour.
RAMPS = {1: 16, 2: 21, 3: 26, 4: 11}

PDB = 'game/htdata832.pdb'
PALS = ['data/preload_8bpp.pal', 'data/art824/common_8bpp.pal']


def make_ramp(main, old):
    """Scales main by the old ramp's brightness steps."""
    top = max(old[0])
    ramp = []
    for c in old:
        f = max(c) / top
        ramp.append(tuple(min(255, int(round(v * f))) for v in main))
    ramp[0] = main
    return ramp


def pdb_records(d):
    n = struct.unpack('>H', d[76:78])[0]
    offs = [struct.unpack('>I', d[78 + 8 * i:82 + 8 * i])[0] for i in range(n)] + [len(d)]
    r0 = d[offs[0]:offs[1]]
    p = 6
    while p + 32 <= len(r0):
        name = r0[p:p + 29].split(b'\0')[0].decode('latin1')
        first = struct.unpack('>H', r0[p + 30:p + 32])[0]
        yield name, offs[first], offs[first + 1]
        p += 32


def patch_pdb(path, first, main):
    d = bytearray(open(path, 'rb').read())
    ramp = None
    count = 0
    for name, start, end in pdb_records(d):
        if not name.endswith('.palbin'):
            continue
        # 6-byte record header, 2-byte entry count, then R,G,B per entry
        base = start + 6 + 2 + 3 * first
        old = [tuple(d[base + 3 * i:base + 3 * i + 3]) for i in range(5)]
        if ramp is None:
            ramp = make_ramp(main, old)
        for i, c in enumerate(ramp):
            d[base + 3 * i:base + 3 * i + 3] = bytes(c)
        count += 1
    open(path, 'wb').write(d)
    return ramp, count


def patch_pal(path, first, ramp):
    raw = open(path, 'rb').read()
    nl = b'\r\n' if b'\r\n' in raw else b'\n'
    lines = raw.split(nl)
    assert lines[0].strip() == b'JASC-PAL', path
    for i, c in enumerate(ramp):
        lines[3 + first + i] = ('%d %d %d' % c).encode()
    open(path, 'wb').write(nl.join(lines))


def main():
    if len(sys.argv) != 3 or int(sys.argv[1]) not in RAMPS:
        sys.exit(__doc__)
    side = int(sys.argv[1])
    hexclr = sys.argv[2].lstrip('#')
    main_clr = tuple(int(hexclr[i:i + 2], 16) for i in (0, 2, 4))
    first = RAMPS[side]
    ramp, count = patch_pdb(PDB, first, main_clr)
    for pal in PALS:
        patch_pal(pal, first, ramp)
    print('side %d ramp: %s (%d palettes in %s, plus %s)' %
          (side, ramp, count, PDB, ', '.join(PALS)))


if __name__ == '__main__':
    main()
