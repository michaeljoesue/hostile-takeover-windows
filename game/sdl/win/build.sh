#!/usr/bin/env bash
# Builds the Windows version of Hostile Takeover and packages it into
# game/sdl/win/dist/HostileTakeover/ (+ a .zip).
#
# Run on Ubuntu / Debian or inside WSL on Windows 11:
#
#   sudo apt install mingw-w64 cmake make curl python3 zip
#   ./build.sh
#
# Downloads SDL2 (MinGW dev package) and builds a static libcurl the first
# time; later runs reuse them from game/sdl/win/deps/.

set -euo pipefail

SDL2_VERSION=2.30.9
CURL_VERSION=8.10.1
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

# --- libcurl (static, Schannel TLS, no extra DLLs) --------------------------
CURLDIR="$DEPS/curl-$ARCH"
if [ ! -f "$CURLDIR/lib/libcurl.a" ]; then
    echo "==> Building libcurl $CURL_VERSION"
    curl -fsSL -o "$DEPS/curl.tgz" \
        "https://github.com/curl/curl/releases/download/curl-${CURL_VERSION//./_}/curl-$CURL_VERSION.tar.gz"
    tar -xzf "$DEPS/curl.tgz" -C "$DEPS"
    cat > "$DEPS/toolchain.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER $ARCH-gcc-posix)
set(CMAKE_CXX_COMPILER $ARCH-g++-posix)
set(CMAKE_RC_COMPILER $ARCH-windres)
set(CMAKE_FIND_ROOT_PATH /usr/$ARCH)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF
    cmake -S "$DEPS/curl-$CURL_VERSION" -B "$DEPS/curl-build-$ARCH" \
        -DCMAKE_TOOLCHAIN_FILE="$DEPS/toolchain.cmake" -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF \
        -DCURL_USE_SCHANNEL=ON -DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBSSH2=OFF \
        -DUSE_LIBIDN2=OFF -DCURL_ZLIB=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF \
        -DUSE_NGHTTP2=OFF -DCMAKE_INSTALL_PREFIX="$CURLDIR" > "$DEPS/curl-config.log"
    cmake --build "$DEPS/curl-build-$ARCH" -j"$(nproc)" > "$DEPS/curl-build.log"
    cmake --install "$DEPS/curl-build-$ARCH" > /dev/null
fi

# --- Game -------------------------------------------------------------------
echo "==> Building HostileTakeover.exe"
make -C "$HERE" REL=1 ARCH="$ARCH" SDL2DIR="$SDL2DIR" CURLDIR="$CURLDIR" -j"$(nproc)"

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

(cd "$HERE/dist" && rm -f HostileTakeover-Windows.zip && zip -qr HostileTakeover-Windows.zip HostileTakeover)
echo "==> Done: $HERE/dist/HostileTakeover-Windows.zip"
