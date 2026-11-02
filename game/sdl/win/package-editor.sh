#!/usr/bin/env bash
# Adds the M map & mission editor and the Author Kit to a packaged game folder.
#
#   package-editor.sh <game folder> <deps folder>
#
# Produces, inside the game folder:
#
#   Editor\M.exe, m.dll, ...        the editor (.NET Framework 4, which
#                                   Windows 10/11 include)
#   Editor\AuthorKit\data           level, art, sound and text sources of the
#                                   built-in missions, plus build-data.bat
#   Editor\AuthorKit\game, mpshared headers the data build reads
#   Editor\AuthorKit\tools          everything build-data.bat needs: make and
#                                   sh (w64devkit busybox), the .NET data
#                                   tools, ppini and inicrunch
#
# Needs: mono-devel (mcs, resgen), mingw-w64, 7z (p7zip-full), curl.

set -euo pipefail

GAME="$(cd "$1" && pwd)"
DEPS="$(mkdir -p "$2" && cd "$2" && pwd)"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HTROOT="$(cd "$HERE/../../.." && pwd)"

W64DK_VERSION=2.10.0
EDITOR="$GAME/Editor"
KIT="$EDITOR/AuthorKit"
TOOLS="$KIT/tools"

rm -rf "$EDITOR"
mkdir -p "$TOOLS" "$KIT/game" "$KIT/mpshared"

# --- .NET tools and the editor -------------------------------------------
echo "==> Building the editor and data tools (.NET Framework 4)"
python3 "$HTROOT/tools/build_dotnet_tools.py" "$TOOLS" \
    acrunch bcr2 packpdb2 palbin paltool shadowmap stringtable ppini \
    markversion mcl mgui > /dev/null

# The editor itself: mgui.exe is a small launcher for m.dll
cp "$TOOLS/mgui.exe" "$EDITOR/M.exe"
cp "$TOOLS/m.dll" "$EDITOR/"
[ -f "$TOOLS/SharpZipLib.dll" ] && cp "$TOOLS/SharpZipLib.dll" "$EDITOR/"
[ -f "$HTROOT/bin/m.chm" ] && cp "$HTROOT/bin/m.chm" "$EDITOR/"
rm -f "$TOOLS/mgui.exe"

# --- inicrunch (C++) -------------------------------------------------------
echo "==> Building inicrunch.exe"
i686-w64-mingw32-g++-posix -DWINDOWS -O2 -s -static -Wno-multichar -Wno-write-strings -w \
    "$HTROOT/inicrunch/main.cpp" "$HTROOT/inicrunch/ini.cpp" \
    -o "$TOOLS/inicrunch.exe"

# --- make / sh from w64devkit ---------------------------------------------
W64DK="$DEPS/w64devkit-$W64DK_VERSION"
if [ ! -f "$W64DK/w64devkit/bin/busybox.exe" ]; then
    echo "==> Downloading w64devkit $W64DK_VERSION (for make and sh)"
    mkdir -p "$W64DK"
    curl -fsSL -o "$W64DK/w64devkit.7z.exe" \
        "https://github.com/skeeto/w64devkit/releases/download/v$W64DK_VERSION/w64devkit-x64-$W64DK_VERSION.7z.exe"
    7z x -y -o"$W64DK" "$W64DK/w64devkit.7z.exe" > /dev/null
fi
for f in busybox.exe make.exe sh.exe; do
    cp "$W64DK/w64devkit/bin/$f" "$TOOLS/"
done
cp "$W64DK/w64devkit/COPYING.MinGW-w64-runtime.txt" "$TOOLS/w64devkit-COPYING.txt" 2>/dev/null || true

# --- sources of the built-in missions --------------------------------------
echo "==> Copying the Author Kit sources"
# Only files under version control: no leftovers from earlier builds
(cd "$HTROOT" && git ls-files -z data) | (cd "$HTROOT" && xargs -0 cp --parents -t "$KIT")
cp "$HTROOT/data/build-data.bat" "$KIT/data/"
cp "$HTROOT/game/res.h" "$HTROOT/game/wistrings.h" "$KIT/game/"
cp "$HTROOT/mpshared/side.h" "$KIT/mpshared/"
cp "$HERE/README-Editor.txt" "$EDITOR/README.txt"

echo "==> Editor packaged in $EDITOR"
