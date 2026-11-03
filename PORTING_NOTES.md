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
- **Right click cancels in build menus** (`game/form.cpp`, `game/Builder.cpp`).
  In the unit build menus (Human Resource Center, Vehicle Transport
  Station), right-clicking a unit's icon or the Order button removes one
  queued unit of that type, and never queues a new one. Controls don't see
  the raw event, so `Form::EventProc` records the flags of the pen event
  being processed in `gffPenEventCurrent`, and the build form checks it for
  `kfEvtRightButton`.
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
- Build menu: three units queued, right-click on the icon and on Order each
  removed one, and right-clicking a unit with nothing queued changed nothing

Not verified: sound (the test environment had no audio device; SDL uses
standard Windows audio) and a run on a physical Windows 11 PC.

## 9. Packaging

`game/sdl/win/build.sh`:

1. downloads and builds the dependencies,
2. builds a stripped release exe (about 4.5 MB),
3. copies the data files, stamping `htdata832.pdb` with version 1.6,
4. adds a player README and the license, and
5. runs `package-editor.sh` to add the editor and Author Kit (section 14),
6. zips the result as `HostileTakeover-Windows.zip`.

# Part 2: the map & mission editor

## 10. What there was to work with

`m/` is the source of **M**, the editor Spiffcode used for every mission. It
is a C# Windows Forms program for .NET Framework 2.0–3.5. Prebuilt copies of
it and of the other data tools were already in `bin/`. The game's built-in
missions come from `data/`: levels (`*.ld`, saved by the editor), art,
sounds and text, turned into `htdata832.pdb` by `data/makefile`, which runs
a dozen small C# and C++ tools.

Three things stood between that and a Windows 11 user:

1. Windows 11 ships .NET Framework 4.8, not the 2.0–3.5 runtimes the old
   binaries need.
2. The editor's **Export Mission Pack** writes a `.pdb` file, but the game
   only knew how to get packs from the old download server.
3. The data build needs `make`, a Unix shell and the C preprocessor, none of
   which come with Windows.

## 11. Building the editor and tools for .NET 4

`tools/build_dotnet_tools.py` compiles the editor and every data tool from
source with Mono's C# compiler (`mcs -sdk:4.5`) into .NET Framework 4
programs, which run on stock Windows 10/11 and on Mono. The projects mix
Visual Studio 2003 and MSBuild formats, so the script reads the file lists
out of each `.csproj` itself, compiles `.resx` form resources with `resgen`,
and names embedded resources the way Visual Studio would.

The sources were written on Windows, so file names in the projects don't
always match the files' case (`globals.cs` vs `Globals.cs`). Those files
were renamed to match the projects so the build works on case-sensitive file
systems too.

## 12. Connecting the editor to the game (`m/GameMenu.cs`)

A new **Game** menu, added from one line in `Form1.cs`:

- **Play Level in Game (F5)** exports the active level as a private
  "Editor Test" mission pack, installs it, and starts the game with
  `-pack 7e570001`.
- **Install Mission Pack in Game** asks for a title and an author, then
  exports all open levels and installs them as a pack.
- **Remove Mission Pack from Game** lists installed packs by title and
  deletes the one picked. The game's own remove button only exists on the
  online download screen, which needs Spiffcode's server.
- **Open Built-in Mission**, **Rebuild Built-in Missions** and
  **Restore Original Missions** work on the Author Kit (section 14).
- Levels given on the command line, or dropped onto `M.exe`, are opened at
  startup.

The game already lists packs found in its `MissionPacks` folder, so
installing one means writing two files where the download code would have:

- `MissionPacks\<id>-<md5 of the pack>`: the exported pack itself, with the
  name `PackManager` expects;
- `MissionPackInfos\<same name>`: `{"t":"<title>","a":"<author>"}`, read by
  `PackInfoManager` for the title shown in the Add-On list.

Pack ids are derived from the title, so reinstalling a pack with the same
title replaces it. The game now shows just the title when a pack has no
author (it printed "by (null)").

On the game side, `game/sdl/main.cpp` now passes the command line through
(the SDL port dropped it), and `GameMain` gained `-pack <id>`, which starts
the first mission of that pack directly.

The "Editor Test" pack (`PACKID_EDITORTEST` in `mpshared/mpht.h`) is
skipped when the game lists add-on missions, so trying a level with F5
doesn't add it to the Add-On list. The editor deletes it when it closes.

### Validation

Before exporting, the editor validates each level. Running that check on
every built-in mission showed that almost all of them "fail": they have
units, buildings or Galaxite standing on terrain the editor considers
blocked. The terrain and tile maps the editor computes are byte-identical to
the ones in the shipped data, so the check is right; Spiffcode simply never
ran it on these missions, and the game doesn't mind. Four missions (challenges
C2 and C5, story missions M4 and M14) also have one "real" error each, such as scenery partly off the map.

So the Game menu sorts the results. Placement problems are reported as
warnings with a "Continue?" prompt. Other errors get a stronger warning
whose default answer is No, but can still be overridden, because those four
missions play fine as shipped.

## 13. Rebuilding the game data on Windows

`data/build-data.bat` runs the original `data/makefile` with tools that ship
in `Editor\AuthorKit\tools`:

- **make and sh** from w64devkit 2.10.0 (busybox-based, about 1 MB in total).
- **ppini** (`ppini/`), a new C# tool standing in for the C preprocessor.
  The build only uses `cpp` to replace constant names from `game/res.h` in
  INI files, and shipping GCC's preprocessor would add about 40 MB. ppini
  implements the subset that's used (`#include`, object-like macros,
  `#ifdef` guards, `-D`, `-I`, `-imacros`) and reproduces `cpp -P`'s
  spacing. Its output for an exported level was compared with GCC's and is
  byte-identical.
- **inicrunch.exe**, cross-compiled. Its Windows code path had a missing `}`
  and needs `-DWINDOWS`.
- The .NET tools from section 11.

The makefile gained a Windows section, and calls tools through `$(TOOLDIR)`
instead of hard-coded `../bin` paths.

Two tool fixes were needed along the way:

- `AniMax/XBitmap.cs` (used by `acrunch`) made sprite backgrounds
  transparent by drawing over a magenta-filled bitmap. Mono's graphics
  library turned those pixels white, putting white boxes around every unit
  when the data was built off Windows. It now copies the pixels directly,
  which gives the same result everywhere.
- `packpdb2` now passes the database type the updated writer requires.

### How close is the rebuilt data to the original?

The rebuild, run under Wine with only the packaged Windows tools, takes
about four minutes and produces 465 records, like the shipped file. 377 are
byte-identical. The rest are:

- the terrain and cutscene palettes, plus the pictures colour-reduced
  against them. The generated palettes pick colours in a slightly different
  order, so the bytes differ, but on screen the battlefield is visually
  identical: under 1% of pixels differ, by a sliver of colour.
- whitespace in `forms.ini` (one double space inside a coordinate list),
  which the game ignores.
- `c_01.lvl`, which comes from the level source as checked in (its terrain
  and tile maps are identical).
- `version.txt`, which is expected.

The build is also repeatable: a run on Linux/Mono and a run under Wine gave
identical data.

## 14. The Author Kit

`game/sdl/win/package-editor.sh` (called by `build.sh`) adds to the game
folder:

```
Editor\M.exe, m.dll          the editor (M.exe is a small launcher)
Editor\AuthorKit\data        tracked files from data/, plus build-data.bat
Editor\AuthorKit\game        res.h and wistrings.h (the build writes its
                             htdata832.pdb here)
Editor\AuthorKit\mpshared    side.h (included by res.h)
Editor\AuthorKit\tools       make, sh, busybox, the .NET tools, ppini,
                             inicrunch
```

Only files under version control are copied, so leftovers from a build in
the source tree never end up in the zip. Unpacked, the editor adds about
34 MB; the zip grows from 4 MB to 16 MB.

**Rebuild Built-in Missions** saves any open built-in levels, runs
`build-data.bat` in a window that shows its output, then copies the result
into the game. The first time, it keeps the shipped data as
`htdata832.original.pdb`; **Restore Original Missions** puts that back.

## 15. Testing the editor

All under Wine, with the packaged files only:

- The editor starts, opens built-in levels from the command line, and
  draws desert and grassy terrain, units and areas.
- F5 on challenge C1 and C4 started the game straight into those missions.
- Install Mission Pack in Game with C5: the pack appeared in the Add-On list
  as "C5: Get Ready by Michael" and played.
- After F5, the Add-On list stayed empty although the test pack was on disk.
  Remove Mission Pack from Game deleted the C5 pack's files, and closing
  the editor deleted the test pack.
- Story mission M1 was renamed, then rebuilt from the editor. The game's
  mission list showed the new name, and campaign progress was kept. Restore
  Original Missions put back data identical to the shipped file.
- Every built-in level was run through validation with a small test
  program, which is how the warning/error split above was chosen.

Wine draws the editor's dialogs and popup menus late on the virtual display
used for testing, so they were driven by keyboard. That is a testing quirk,
not something seen on Windows. As with the game, a run on a physical
Windows 11 PC is still to come.

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
| `game/form.cpp`, `game/Builder.cpp` | Right click cancels queued units in build menus |
| `game/game.cpp`, `game/sdl/main.cpp` | Command line passed through; `-pack <id>` |
| `game/selectmission.cpp`, `game/creategameform.cpp` | Packs without an author show just their title |
| `game/missionlist.cpp`, `game/httpindexloader.cpp` | The editor's test pack isn't listed with add-on missions |
| `m/GameMenu.cs` | The editor's Game menu |
| `tools/build_dotnet_tools.py` | Builds the editor and data tools for .NET 4 |
| `ppini/` | C preprocessor subset for the data build |
| `data/build-data.bat`, `data/makefile` | Data build on Windows |
| `game/sdl/win/package-editor.sh` | Packages the editor and Author Kit |
| `game/sdl/win/README-Editor.txt` | Editor README shipped in the zip |
