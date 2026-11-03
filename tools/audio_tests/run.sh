#!/usr/bin/env bash
# Builds and runs the audio tests against the Linux build's objects.
# First build the Linux port: cd game/sdl/linux && make REL=1
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$HERE/../.."
cd "$ROOT"
OUT="${TMPDIR:-/tmp}/ht-audio-tests"
mkdir -p "$OUT"
OBJS=$(find game/sdl/linux/release -name "*.o" ! -name main.o ! -name sdlsounddev.o)
FLAGS="-w -fpermissive -fsigned-char -std=c++11 -I. -Igame -Igame/sdl -Ibase -Impshared -Iinc -I/usr/include/SDL2 -D__LINUX__ -DSDL -DMULTIPLAYER"
g++ $FLAGS "$HERE/mixer_test.cpp" $OBJS -lSDL2 -lcurl -lpthread -o "$OUT/mixer_test"
g++ $FLAGS "$HERE/position_test.cpp" $OBJS game/sdl/linux/release/sdl/sdlsounddev.o -lSDL2 -lcurl -lpthread -o "$OUT/position_test"
SDL_AUDIODRIVER=dummy "$OUT/mixer_test"
echo
"$OUT/position_test"
