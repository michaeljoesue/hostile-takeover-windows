#!/usr/bin/env python3
"""Gives Andy's (and Fox's) sprites team colours, like the other infantry.

Team colours are drawn from five special "side colour" shades in the art
(0,116,232 ... 0,32,64), which the game swaps for the owner's colour. Andy's
art only had a few dark ones (gun tip, knees), so his side was hard to see,
and Fox used the same art. This script:

  - repaints the light grey armour in data/art824/andy/*.png with the side
    colour shades (helmet, visor and gun stay as they were), and
  - writes Fox's own copy, data/art824/foxunit/foxunit_*.png (+ foxunit.amx,
    a copy of andy.amx pointing at it), with an amber visor instead of cyan
    so he can be told apart from Andy. FoxGob loads foxunit.anir.

Run once from the repository root on the original art (git history has it):
    python3 tools/hero_side_colors.py
Then crunch andy.amx and foxunit.amx with acrunch (see PORTING_NOTES.md).
"""
import glob
import os
from PIL import Image

SIDE = [(0, 116, 232), (0, 96, 196), (0, 64, 120), (0, 48, 92), (0, 32, 64)]
ARMOUR = {(216, 216, 216): 0, (168, 168, 168): 0, (144, 144, 144): 1,
          (132, 128, 128): 1, (120, 120, 120): 2, (112, 112, 112): 2}
FOX_VISOR = {(104, 252, 252): (252, 224, 100), (0, 192, 196): (224, 160, 12),
             (0, 116, 120): (192, 104, 0)}
ART = 'data/art824'


def remap(im, mapping):
    px = im.load()
    for y in range(im.size[1]):
        for x in range(im.size[0]):
            if px[x, y] in mapping:
                px[x, y] = mapping[px[x, y]]
    return im


def main():
    armour = {c: SIDE[i] for c, i in ARMOUR.items()}
    os.makedirs(os.path.join(ART, 'foxunit'), exist_ok=True)
    for f in sorted(glob.glob(os.path.join(ART, 'andy', 'andy_*.png'))):
        im = remap(Image.open(f).convert('RGB'), armour)
        im.save(f)
        fox = remap(im.copy(), FOX_VISOR)
        fox.save(os.path.join(ART, 'foxunit', os.path.basename(f).replace('andy_', 'foxunit_')))
    amx = open(os.path.join(ART, 'andy.amx'), 'rb').read()
    open(os.path.join(ART, 'foxunit.amx'), 'wb').write(amx.replace(b'andy\\andy_', b'foxunit\\foxunit_'))


if __name__ == '__main__':
    main()
