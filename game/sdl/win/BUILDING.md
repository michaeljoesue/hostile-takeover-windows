# Windows port (SDL2, MinGW-w64)

Builds a native, self-contained `HostileTakeover.exe` for Windows 10/11 from
the existing SDL port. Single player is the target; multiplayer code is
compiled in but untested.

## Build

On Ubuntu/Debian, or in WSL on Windows 11:

```sh
sudo apt install mingw-w64 cmake make curl python3 zip
cd game/sdl/win
./build.sh
```

Output: `game/sdl/win/dist/HostileTakeover-Windows.zip` (exe + data files +
README). The script downloads SDL2 2.30.9 (MinGW dev package) and builds a
static libcurl 8.10.1 with Schannel the first time, into `game/sdl/win/deps/`.

`make REL=1` alone rebuilds just the exe once the deps exist. Leave out
`REL=1` for a debug build (console window, `DEV_BUILD`, symbols).

## What changed and why

* **32-bit target (i686).** The code stores pointers in `long` in many
  places. `long` is pointer-sized on 32-bit Windows and on 64-bit
  Linux/macOS, but only 32 bits on 64-bit Windows, which truncated pointers
  and crashed as soon as a mission loaded. `pword` in `inc/basictypes.h` is
  now `uintptr_t`, but the remaining `long` casts make Win64 unsafe.
* **`-fno-delete-null-pointer-checks`.** Code such as
  `MiniMapControl::RedrawTRect` relies on `if (this == NULL) return;`, which
  modern GCC removes. Also added to the Linux makefile.
* **Winsock shims** (`base/wincompat.h`, `base/socket*.cpp`,
  `base/selectserver.cpp`). The game's message loop sleeps in `select()`
  and is woken by `base::Eventer`, which used a `pipe()`. Windows `select()`
  only accepts sockets, so on Windows the Eventer uses a loopback UDP socket.
* **Host layer** (`game/sdl/win/hosthelpers.cpp`). Data files are read from
  the exe's folder; saves, prefs and `log.txt` go to
  `%APPDATA%\HostileTakeover` (or `UserData\` next to the exe when a
  `portable.txt` file is present). Message boxes use SDL.
* **Desktop input/display** (`game/sdl/host.cpp`, `display.cpp`,
  `game/event.cpp`, `game/InputUI.cpp`). Mouse input is enabled for
  Windows with modern RTS controls (left click selects or deselects, right
  click moves, attacks or opens the unit menu; see `game/stylushandler.cpp`); the window is
  resizable with aspect-correct scaling (`SDL_RenderSetLogicalSize`);
  F11 / Alt+Enter toggles fullscreen; arrow keys scroll the map.
* **Manifest** (`app.manifest`). UTF-8 active code page so `fopen()` works
  with non-ASCII user folder names, plus Windows 10/11 compatibility.
* **Release data file.** Release builds check the version stamped into
  `htdata832.pdb`; `build.sh` stamps "1.6" (same as `bin/markversion.exe`).
* Small fixes needed by any modern GCC: `return false` from functions
  returning pointers; `game/Multiplayer.h` include on case-sensitive file
  systems.
