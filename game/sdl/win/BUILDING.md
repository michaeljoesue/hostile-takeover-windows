# Windows port (SDL2, MinGW-w64)

Builds a native, self-contained `HostileTakeover.exe` for Windows 10/11 from
the existing SDL port. Single player only: multiplayer, chat, the
leaderboard/score upload and mission pack downloads have been removed (see
"Online features removed" in `PORTING_NOTES.md`).

## Build

On Ubuntu/Debian, or in WSL on Windows 11:

```sh
sudo apt install mingw-w64 make curl python3 zip mono-devel p7zip-full
cd game/sdl/win
./build.sh
```

Output: `game/sdl/win/dist/HostileTakeover-Windows.zip` (exe, data files,
README, and the `Editor` folder with the map editor and Author Kit). The
script downloads SDL2 2.30.9 (MinGW dev package) and fetches w64devkit 2.10.0 (for `make`/`sh`) the first
time, into `game/sdl/win/deps/`.

`NO_EDITOR=1 ./build.sh` builds only the game. `package-editor.sh <game
folder> deps` re-packages just the editor and Author Kit.

On Windows, `Editor\AuthorKit\data\build-data.bat` rebuilds the game data
with the packaged tools. On Linux, build the tools first and point the
original makefile at them (the prebuilt tools in `bin/` lack the
transparency fix described in `PORTING_NOTES.md`):

```sh
python3 tools/build_dotnet_tools.py /tmp/httools
cp bin/inicrunch_linux /tmp/httools/
cd data && make TOOLDIR=/tmp/httools
mono /tmp/httools/markversion.exe 1 ../game/htdata832.pdb "+++VERSION+++" 1.6
```

The last step stamps the version that release builds of the game check.
Note that this overwrites the checked-in `game/htdata832.pdb`.

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
  modern GCC removes.
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
  click moves, attacks or opens the unit menu; see `game/stylushandler.cpp`); the game runs
  fixed borderless fullscreen; its resolution is the desktop's divided by
  the largest whole number that keeps it at least 720x540
  (`win/hosthelpers.cpp`), scaled up with nearest-neighbour integer scaling
  (`SDL_RenderSetIntegerScale`) and per-monitor DPI awareness; arrow keys
  scroll the map.
* **Manifest** (`app.manifest`). UTF-8 active code page so `fopen()` works
  with non-ASCII user folder names, plus Windows 10/11 compatibility.
* **Release data file.** Release builds check the version stamped into
  `htdata832.pdb`; `build.sh` stamps "1.6" (same as `bin/markversion.exe`).
* Small fixes needed by any modern GCC: `return false` from functions
  returning pointers; `game/Multiplayer.h` include on case-sensitive file
  systems.
