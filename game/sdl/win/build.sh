#!/usr/bin/env bash
# Builds the Windows version of Hostile Takeover and packages it into
# game/sdl/win/dist/HostileTakeover/ (+ a .zip).
#
# Run on Ubuntu / Debian or inside WSL on Windows 11:
#
#   sudo apt install mingw-w64 make curl python3 zip mono-devel p7zip-full
#   ./build.sh
#
# Downloads SDL2 (MinGW dev package) the first time; later runs reuse it from
# game/sdl/win/deps/.

set -euo pipefail

SDL2_VERSION=2.30.9
ARCH=i686-w64-mingw32          # 32-bit on purpose; see makefile for why

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HTROOT="$HERE/../../.."
DEPS="$HERE/deps"
mkdir -p "$DEPS"

# --- SDL2 -------------------------------------------------------------------
SDL2DIR="$DEPS/SDL2-$SDL2_VERSION/$ARCH"
if [ ! -f "$SDL2DIR/lib/libSDL2.a" ]; then
    echo "==> Downloading SDL2 $SDL2_VERSION"
    curl -fsSL -o "$DEPS/sdl2.tgz" \
        "https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VERSION/SDL2-devel-$SDL2_VERSION-mingw.tar.gz"
    tar -xzf "$DEPS/sdl2.tgz" -C "$DEPS"
fi

# --- Game -------------------------------------------------------------------
echo "==> Building HostileTakeover.exe"
make -C "$HERE" REL=1 ARCH="$ARCH" SDL2DIR="$SDL2DIR" -j"$(nproc)"

# --- Package ----------------------------------------------------------------
DIST="$HERE/dist/HostileTakeover"
rm -rf "$HERE/dist"
mkdir -p "$DIST"
cp "$HERE/release/HostileTakeover.exe" "$DIST/"
cp "$HTROOT/game/htsfx.pdb" "$DIST/"
# Release builds require the data file to be stamped with the game version
# (this is what bin/markversion.exe does in the original instructions).
python3 - "$HTROOT/game/htdata832.pdb" "$DIST/htdata832.pdb" 1.6 <<'EOF'
import sys
src, dst, ver = sys.argv[1:4]
pat = b'+++VERSION+++'
d = bytearray(open(src, 'rb').read())
if d.count(pat) == 1:
    i = d.find(pat)
    m = ver.encode()
    d[i:i + len(m)] = m
    d[i + len(m)] = 0
elif ver.encode() + b'\0' not in d:
    sys.exit("htdata832.pdb: version mark not found")
open(dst, 'wb').write(d)
EOF
cp "$HERE/README-Windows.txt" "$DIST/README.txt"
cp "$HTROOT/LICENSE.txt" "$DIST/LICENSE.txt"

# --- Map & mission editor + Author Kit ----------------------------------------
if [ "${NO_EDITOR:-0}" != "1" ]; then
    bash "$HERE/package-editor.sh" "$DIST" "$DEPS"
fi

(cd "$HERE/dist" && rm -f HostileTakeover-Windows.zip && zip -qr HostileTakeover-Windows.zip HostileTakeover)
echo "==> Done: $HERE/dist/HostileTakeover-Windows.zip"
