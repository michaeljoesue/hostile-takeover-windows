# How the Windows port was made

These notes explain how Hostile Takeover was brought to Windows 11, in the
order the work happened, including the problems hit along the way. They are
meant for anyone who wants to understand, maintain or extend the port.

## 1. Choosing a starting point

The repository contains several platform layers: the original iPhone code,
an Android build, and a shared **SDL2** layer in `game/sdl/` used by the macOS,
Android and Linux builds. SDL2 also runs on Windows, so the plan was to add a
Windows host next to `game/sdl/linux/` rather than revive the very old
Windows Mobile code.

The game itself (`game/`, `base/`, `mpshared/`, `inc/`, `yajl/`) is
platform-neutral C++. A platform only needs to supply:

- a `HostHelpers` implementation (data and save folders, logging, message
  boxes, screen size), and
- a chat controller (only used by multiplayer).

## 2. Getting a known-good baseline on Linux

Before touching Windows, the Linux port was built and run, so that any later
failure could be pinned on Windows rather than on the code base itself.
Three problems surfaced, all caused by the code being older than today's
compilers:

| Problem | Fix |
| --- | --- |
| `#include "game/Multiplayer.h"` but the file is `multiplayer.h`; this only worked on case-insensitive file systems | Corrected the include |
| Functions returning pointers did `return false;`, which modern GCC rejects | `return NULL;` (`game/sdl/sdlpackfile.cpp`, `mpshared/packinfomanager.cpp`) |
| "Incorrect game graphics version" at startup | Release builds compare the version stamped into `htdata832.pdb` against `1.6`. The checked-in data file is unstamped (`+++VERSION+++`), and stamping is done by `bin/markversion.exe`, a C# tool. A small Python equivalent now does the stamping during packaging. |

With those fixed, the Linux build reached the main menu.

## 3. Setting up a Windows toolchain

The port is **cross-compiled** with MinGW-w64, so it can be built from Linux
or from WSL on Windows without Visual Studio. Two libraries are needed:

- **SDL2**: the official `SDL2-devel-*-mingw` package, linked statically.
- **libcurl**: used by the game's HTTP service. It is built from source as a
  static library using Windows' own TLS (Schannel), so the game needs no
  OpenSSL DLLs.

Everything is linked statically, so `HostileTakeover.exe` depends only on DLLs
that ship with Windows. `game/sdl/win/build.sh` automates all of this.

## 4. The Windows host layer (`game/sdl/win/`)

A new `makefile` mirrors the Linux one. The first compile showed that only a
handful of files used Linux-only headers. The new pieces are:

- **`hosthelpers.cpp`**
  - Game data (`htdata832.pdb`, `htsfx.pdb`) is read from the folder that
    contains the exe.
  - Saves, settings and `log.txt` go to `%APPDATA%\HostileTakeover`, or to a
    `UserData` folder beside the exe if `portable.txt` exists.
  - Error messages appear as real message boxes, and links open in the
    browser.
- **`winchatcontroller.*`**: a copy of the Linux stub (multiplayer only).

## 5. Sockets, even for single player

The game's main loop sleeps inside `select()` and is woken up by
`base::Eventer`, which on Linux writes a byte into a `pipe()`. On Windows,
`select()` only works on **sockets**, not pipes, so this mechanism is needed
even when playing alone.

- **`base/wincompat.h`** pulls in Winsock and provides small helpers:
  start Winsock once, translate Winsock error codes (such as
  `WSAEWOULDBLOCK`) to the POSIX codes the code expects, close sockets with
  `closesocket`, and switch to non-blocking mode with `ioctlsocket`.
- **`base/eventer.cpp`**: on Windows the "pipe" is a non-blocking UDP socket
  bound to `127.0.0.1` and connected to itself. Sending it a byte makes
  `select()` wake up, exactly like the pipe did.
- **`base/socket*.cpp`, `selectserver.cpp`**: small `#ifdef _WIN32` changes
  for the differences above. LAN interface discovery (multiplayer only)
  returns "none" on Windows.

After this the exe linked and reached the main menu under Wine.

## 6. Two crashes when a mission started

Testing went beyond the main menu, and the game crashed as soon as a mission
loaded. Debug symbols plus `addr2line` turned the crash addresses into source
lines.

**Crash 1: `MiniMapControl::RedrawTRect`.** The function starts with
`if (this == NULL) return;` and the code base relies on calling it through a
null pointer. That is undefined behaviour in C++, so modern GCC deletes the
check. The fix is the compiler flag `-fno-delete-null-pointer-checks`, which
restores the old behaviour everywhere at once. The flag was also added to the
Linux makefile, which had the same latent bug. `-fno-strict-aliasing` and
`-fwrapv` are also set, as old code like this commonly depends on them.

**Crash 2: `GobMgr::AddGob`.** The game keeps pointers in integers of type
`pword`, which was `unsigned long`. `long` is 64 bits on 64-bit Linux and
macOS, so the 64-bit ports were fine. On **64-bit Windows**, however, `long` is
only 32 bits, so pointers were cut in half.

- `pword` is now `uintptr_t`, which is always pointer-sized.
- The code has many other places that assume `long` can hold a pointer, so
  the Windows build targets **32-bit (i686)**, the environment the game was
  originally written for. 32-bit programs run natively on Windows 11,
  including ARM PCs.

With both fixes the missions played normally.

## 7. Making it pleasant on a desktop

The SDL layer was written for touch screens, so a few desktop comforts were
added. All of them are `#ifdef`'d to Windows or SDL and leave other platforms
unchanged.

- **Mouse input on Windows**: SDL mouse events were only enabled for
  macOS, Android and Linux.
- **Modern RTS mouse controls** (`game/sdl/host.cpp`,
  `game/stylushandler.cpp`, `game/event.cpp`). The original desktop
  ("stylus") controls come from PDAs, where a tap on the ground moves the
  selected units. On Windows they now work like a modern RTS:
  - Left click selects; left drag box-selects; a left click on empty
    ground clears the selection instead of moving.
  - Right click on the ground moves, right click on an enemy attacks, and
    right click on your own unit or building opens its menu (the touch
    version's tap-and-hold). Right-clicking a Replicator while units are
    selected sends them into it, as tapping it did before.

  The host tags pen events from the right button with a new
  `kfEvtRightButton` flag (`game/ht.h`). The battlefield's stylus handler
  ignores right-button presses for selection and acts when the button is
  released. The pen-hold timer in `game/event.cpp` skips right-button
  presses, so holding the right button never opens a menu by accident.

  An early version turned a right click into a synthesized "hold". It was
  replaced by this scheme at the player's request.

  One thing that looked like a bug while testing: in mission M1 the first
  move order seemed to be ignored. It was actually the tutorial: issuing the
  order triggers a tutorial message that pauses the game until you press OK.
- **Resizable window and fullscreen** (`game/sdl/display.cpp`): the window
  starts at the largest whole multiple of 800×600 that fits the screen and
  can be resized freely. `SDL_RenderSetLogicalSize` scales the picture,
  letterboxes it, and converts mouse coordinates back to game coordinates.
  Clicks in the black bars are clamped to the edge. F11 and Alt+Enter toggle
  borderless fullscreen.
- **Arrow keys scroll the map** (`game/InputUI.cpp`). The first attempt put
  this in the battlefield form, but key presses actually go to the in-game
  bottom bar (`InputUIForm`), which already handled `M` for the menu.
- **Windows manifest** (`game/sdl/win/app.manifest`): sets the process code
  page to UTF-8, so file paths work when the Windows user name contains
  non-English characters (for example Japanese). It also declares Windows
  10/11 compatibility. The game's own icon from `game/win/app.ico` and
  version information are embedded too.

## 8. Testing

Testing used the actual Windows `.exe` running under Wine on a virtual
display, driven by scripted mouse clicks and keystrokes and checked with
screenshots. The following were verified:

- Startup, main menu and mission list
- The story intro (M0) and mission M1, including the tutorial prompts
- Selecting and moving units, the right-click menu, transforming the
  Dominion into an HQ, and building a Power Generator (credits deducted)
- The RTS controls: left click on the ground deselects without moving, right
  click moves single units and box-selected groups, and right click on your
  own unit opens its menu
- Saving and loading a game, and progress persisting across restarts
- Window resizing with correct click positions, fullscreen, and arrow-key
  scrolling
- A clean first run from the unzipped package
- Online features failing gracefully ("Could not resolve hostname") instead of
  crashing

Not verified: sound (the test environment had no audio device; SDL uses
standard Windows audio) and a run on a physical Windows 11 PC.

## 9. Packaging

`game/sdl/win/build.sh`:

1. downloads and builds the dependencies,
2. builds a stripped release exe (about 4.5 MB),
3. copies the data files, stamping `htdata832.pdb` with version 1.6,
4. adds a player README and the license, and
5. zips the result as `HostileTakeover-Windows.zip`.

## File map

| File | Purpose |
| --- | --- |
| `game/sdl/win/makefile` | Windows build (i686 MinGW, static linking) |
| `game/sdl/win/build.sh` | Fetch deps, build, package |
| `game/sdl/win/hosthelpers.cpp` | Windows host: folders, logging, message boxes |
| `game/sdl/win/app.manifest`, `resource.rc` | UTF-8 code page, icon, version info |
| `game/sdl/win/README-Windows.txt` | Player README shipped in the zip |
| `game/sdl/win/BUILDING.md` | Build reference |
| `base/wincompat.h` | Winsock compatibility helpers |
| `base/eventer.cpp` | Loopback-socket wakeup on Windows |
| `game/sdl/host.cpp` | Mouse on Windows, right-button tagging, fullscreen keys, letterbox clamping |
| `game/stylushandler.cpp` | Modern RTS left/right click behaviour on the battlefield |
| `game/sdl/display.cpp` | Resizable scaled window, fullscreen toggle |
| `game/InputUI.cpp` | Arrow-key map scrolling |
| `inc/basictypes.h` | `pword` is now `uintptr_t` |
