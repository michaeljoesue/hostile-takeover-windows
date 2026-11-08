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
  10/11 compatibility. The game's own icon (`game/sdl/win/app.ico`, originally `game/win/app.ico`) and
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

Not verified at this stage: sound (the test environment has no audio device;
see Part 3 for how the new mixer was checked) and a run on a physical
Windows 11 PC.

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

# Part 3: positional stereo sound

## 16. The problem

A player noticed that battles elsewhere on the map sounded exactly like
battles on screen, with no hint of direction. Reading the code showed why:

- `SoundMgr::PlaySfx(sfx)` takes only the sound, never a position. Every
  gunshot and explosion on the map played at full volume, dead center.
- The SDL sound device output 8 kHz, 8-bit **mono**, the format of the
  original handhelds' speakers.
- It mixed only **4 voices** at once and *averaged* them, so each sound got
  quieter whenever another started, and a distant battle could take the
  voices that on-screen sounds needed.
- The Options screen's volume slider called `SetVolume`, but the SDL device
  ignored it (the setting was even saved in the preferences).

## 17. The new mixer (`game/sdl/sdlsounddev.cpp`)

- 16-bit stereo at 44.1 kHz. The 8 kHz ADPCM sound effects are decoded
  exactly as before and upsampled with linear interpolation, so pitch and
  length are unchanged.
- 16 voices (`kcChannelsMax`), each with its own volume and left/right
  pan (linear pan: the far speaker fades, the near one stays at full).
- Voices are added together with headroom, and a soft limiter compresses
  pile-ups instead of letting them clip harshly.
- The master volume works, so the Options slider does too.

`SoundDevice` gained `PlayAdpcmEx(ichnl, pb, cb, nVolume, nPan)`. Its default
implementation calls the old `PlayAdpcm`, so the iOS and other sound devices
are unaffected.

## 18. Positioning sounds (`game/soundmgr.cpp`)

`SoundMgr::PlaySfx(sfx, wx, wy)` positions a sound relative to the view;
the maths is in `CalcSfxPosition` so it can be tested on its own:

- **Pan** follows the horizontal distance from the center of the view: up
  to half-way to one side at the edge of the screen, and up to 80% one more
  screen-width away. Neither ear ever goes completely silent, which is
  tiring on headphones.
- **Volume** is full anywhere on screen. Outside it, it halves 12 tiles from
  the edge of the view and keeps falling, but never below 1/8, so a battle
  across the map is still a faint sound from its side.
- Stereo can't show above/below: a battle straight north of the view is
  centered, just quieter.

The 23 calls for sounds that happen somewhere on the map now pass their
object's position: weapons firing (infantry, tanks, artillery, towers,
Andy), projectile impacts, units and buildings destroyed, buildings
damaged, miners delivering, processor doors, and the Replicator building.
Interface sounds, voice announcements ("unit ready", "base under attack",
"miner under attack") and responses to your own commands stay centered and
at full volume, so they are never missed.

## 19. Testing the sound without a sound card

The test machine has no audio device, so the sound was checked by
measuring, not listening. Two test programs were run against the Linux
build (they were in `tools/audio_tests/` and were removed with the Linux
port, see section 34):

- **Mixer** (`mixer_test.cpp`): the new decoder reproduces the original
  mixer's samples exactly (8,000 of 8,000); a sound panned fully left comes
  only out of the left channel; half right gives exactly twice the level on
  the right; half volume halves the level; the master volume at 0 is
  silent and at 128 about half; one second of sound lasts one second after
  upsampling; and 16 voices at once stay within range.
- **Positioning** (`position_test.cpp`): on screen is full volume; 12 tiles
  off screen is half volume and panned further to that side; far away is
  quiet but audible; straight above is centered; nearer is always louder.

Then a real battle: challenge C4 ran for about 5 minutes with SDL's dummy
audio driver standing in for a sound card, and a test build (`-DSFX_TRACE`)
logged every positioned sound. Of 2,580 sounds, 2,270 were off screen. They
were quieter than on-screen ones (median 54% volume; the only exceptions were
two sounds less than a tile past the edge, which count as on screen) and
all were panned to their side, and
after the view was scrolled left, the fighting to the right came out mostly
on the right. The Windows `.exe` also ran under Wine with the dummy driver
through menus and into a mission.

What remains is listening to it on a real PC.

# Part 4: maps up to 120 × 120

The original missions are at most 62 × 62 tiles on a 64 × 64 canvas, and the
engine was written for that. This part raises the limit.

## 20. Where the limit comes from

World coordinates (`WCoord`) are signed 16-bit numbers with 256 units per
tile, so a map can be at most 127 tiles across before the far edge stops
fitting (127 × 256 = 32512; 128 would be 32768). Within that, the engine had
a handful of things sized for 64:

- `ktcMax = 64` in `game/ht.h`, which sizes the world ↔ pixel lookup tables
  and a few work arrays.
- `game.cpp` built the pixel → world table with a hard-coded `64 * 32`.
- `ganSquared[64]` (squares used for path-finding distances) was indexed with
  tile differences up to the map size; on a bigger map it would read past
  the end.
- `TRectSmall` (a gob's on-screen tile rectangle) stored tile numbers in
  `char`s, and used 127 / -128 as "empty" markers.
- The memory manager allocates in chunks of at most 63 KB, and the world →
  pixel table for 127 tiles is 64 KB.

## 21. The engine changes

- `ktcMax` is now 127, the engine's ceiling. `ktcMapOriginal = 64` is kept
  for the places where behaviour must not change on the original maps.
- `Game::InitCoordMappingTables` builds both lookup tables as plain heap
  arrays sized from `kwcMax` / `kpcMax` (no more hard-coded 64), and
  saturates the pixel → world entries that only exist for tiles smaller than
  32 pixels instead of letting them wrap.
- `ganSquared` has 128 entries.
- `TRectSmall` uses `short`s, with `0x7fff` / `-0x7fff` as the empty markers.
- Path finding (`terrainmap.cpp`):
  - The search's node budget scales with the map
    (`max(64, map size) × 9`), so on maps up to 64 tiles it is exactly what
    it was.
  - On maps bigger than 64 tiles, scores are added in 32 bits and saturate
    at 65535 instead of wrapping (the distance term alone can reach
    126² × 2 = 31752). Smaller maps keep the original 16-bit arithmetic,
    wrap-around included, so the original missions route exactly as before.
  - `MakePath` collects the route in a buffer sized for the whole map. The
    old one was sized by the node budget, but heads are recycled, so that
    was never a real bound.
- Loading a terrain map larger than 127 tiles shows "Map too big" and stops,
  rather than corrupting memory.
- Scrolling: the map drag (stylus/finger) and minimap drag paths add view
  offsets in 32 bits and clamp before converting back to `WCoord`.
- The computer player's unit groups pick the nearest target by squared tile
  distance in a `word`, with an assert that it stays under 8192 ("for 64x64
  maps"). On a 120 × 120 map it reaches about 28000, still below the
  `0x7fff` starting value, so release builds were fine; the assert now
  checks the real bound for 127 tiles. The debug build found this one.
- The minimap first used one pixel per tile on maps bigger than 64 tiles,
  so it stayed at most 127 pixels. That was too small to read on a 120 × 120
  map, so it now always uses the normal 2 × 2 pixels per tile and, when that
  is more than 180 pixels (maps over 90 tiles), is shown scaled down to 180
  pixels (`MiniMapControl::CalcSizes` in `game/SimUI.cpp`). A 120 × 120 map
  gets a 180-pixel minimap; clicks and drags on it are scaled back. Both
  scales of mini-tiles were always in the data (`m/OutputTools.cs` writes
  2 × 2 and 1 × 1 sets), so no data change was needed.

What was checked and left alone: gob gid map (2 bytes per tile, 32 KB at
127 × 127, inside one memory chunk), the fog and terrain maps (one byte per
tile, heap), path offsets (`word`, 16129 tiles at most), range checks (done in
tiles, under 10), shots (brought inside the map before they fly), line
iterators (32-bit maths), and the gob count limits (per category, not per map
size, so a big map holds the same number of units and buildings).

## 22. Why the editor stops at 120

127 is where the numbers run out. Anything that can stray past the map edge
for a moment (a shot's start point, a view position before it's clamped,
an explosion offset) needs room beyond the last tile, and at 127 there is
under one tile of it. At 120 there are 7 tiles (1792 world units), more than
any of those offsets. The engine accepts up to 127, so raising the editor's
cap later is a one-line change (`LevelDoc.ktcPlayableMax`).

## 23. The editor

- `LevelDoc.Bounds` (the green playable rectangle) can now be up to 120 × 120;
  larger values are refused with a message. The canvas the editor draws on
  (previously always 64 × 64) grows automatically to hold the bounds plus a
  one-tile border, and never shrinks, so nothing already placed is lost.
- **Settings → Map Size…** (`MapSizeForm` in `m/GameMenu.cs`) sets the playable
  width and height, keeping the top left corner.
- Validation reports maps over 120 as an error.
- Importing a mission pack's tile map was hard-coded to 64 × 64 and now uses
  the map's size.
- `LevelView.UpdateImage` compared the canvas bitmap against the *control's*
  width rather than the level's, so it rebuilt the bitmap on every update; it
  now compares against the level size and rebuilds only when the canvas
  really changes size (after Map Size, for example).

## 24. Testing big maps

A small harness (not part of the build) used the editor's own code to turn
C4 "Tables Turned" (60 × 60) into a 120 × 120 level: the terrain repeated to
fill the new area, and copies of the player's units placed on open ground
near the far corner, in the last column on the right edge and near the
bottom edge. It also checked that the editor refuses 121 and grows the
canvas for 100 × 80. The level then went through the real editor under
Wine: Play Level in Game (F5) validated and exported it and started the
game.

In the game:
- the level loaded; the minimap was 120 pixels square and showed the fog
  lifted around the far units;
- clicking the minimap's corners, and holding the arrow keys against each
  edge, scrolled the view to every edge of the map;
- the far-corner units, ordered to the player's base in the top left, walked
  across the whole map (about 2½ minutes for infantry), and the right-edge
  units walked down the last column;
- saving and loading worked, in the same session and after restarting the
  game;
- the debug build (asserts on) ran the same level with the computer players
  active; that is how the unit-group assert above was found. (The debug
  build can't load saves made by the release build, as before; the debug
  makefile also needed `-lgdi32 -luser32` to link.)

The original missions: C4 run through the same Play Level in Game path still
has the 2-pixel-per-tile minimap, and saves and loads. Nothing about the
original-size maps changed: the path-finding node budget and score
arithmetic, and the minimap scale, only change above 64 tiles, and the
other changes are wider types holding the same values.

Not tested: multiplayer on big maps.

# Part 5: side colours

## 25. How side colours work

Unit and building art isn't stored in a side's colour. When the art is
compiled, pixels in five special source blues (`SpiffLib/tbitmap.cs`) become
"side codes" 0–4, from brightest to darkest. At run time
`TBitmap::InitClass` maps each side's codes onto five palette entries, its
*ramp* (`kiclr0BlueSide`…`kiclr4BlueSide` in `game/res.h`, which
`gaclr8bpp` in `game/misc.cpp` points at palette entries 16–20 for side 1).
The minimap dot uses the same entries, and so do the game's dialog frames
and a few other interface details, which are drawn with side 1's ramp.

## 26. Changing a side's colour

`tools/set_side_color.py <side> <hex>` sets a side's brightest entry to the
colour given and scales the other four by the original ramp's brightness
steps, in every palette in `game/htdata832.pdb` and in the source palettes
`data/preload_8bpp.pal` and `data/art824/common_8bpp.pal` (so a data rebuild
keeps it). Side 1 was briefly light blue (#99CCFF) this way on the
`feature/side1-color` branch and then put back to its original blue.

`paltool` used to count the five source blues that mark side-coloured pixels
as ordinary colours. That was harmless while they matched side 1's ramp
exactly, but after recolouring they took palette entries of their own, so
`paltool` now skips them like the transparent and shadow key colours.

## 27. A sixth side: Crimson (branch `feature/crimson-side`)

The game had the neutral side plus sides 1–4. This branch adds side 5,
**Crimson** (#DC143C), which levels can use like any other side (computer
or human).

**Game.**
- `kside5` / `kcSides = 6` (`mpshared/side.h`), `ksidmSide5`, and one
  player per side (`kcPlayersMax = kcSides`). The multiplayer
  sync-error message keeps its 5 slots (`kcPlayersMaxNet`), because that
  protocol is fixed by the old servers.
- `Level::m_asidi` (the per-side starting info) was a hard-coded `[5]`; it is
  `[kcSides]` now. This one overwrote the trigger manager next to it in
  memory and crashed the first test, so it's worth knowing if more sides
  are ever added.
- Colours: `kiclrSide5` and `kiclr0CrimsonSide`…`kiclr4CrimsonSide` in
  `game/res.h`, pointing at palette entries 67–71 (`gaclr8bpp`), a Crimson
  row in `TBitmap::InitClass`'s side table, and `gaiclrSide[5]` for the
  minimap. The white "flash" side-code table that used to be number 5 is now
  `kiColoredSideWhite` (after the last side).
- Triggers: in a trigger's side argument bits 0–4 mean neutral and sides
  1–4, but bits 5–8 already meant Enemies, Allies, All sides and Current
  side. So Crimson is bit 9 (`knCaSideSide5`), and
  `GetSideMaskFromCaSideMask` maps it to the side-5 mask explicitly.
- Saved games are version 10 (`knVerGameState`). Saves made before this
  branch can't be loaded by it, because the trigger
  and player state now has six sides.
- Levels without a `[side5]` section get the usual default for a missing
  side (a computer player with nothing on the map), so all existing missions
  load and play as before.

**Palette.** The palettes were full, so Crimson needed real entries: the
crimson ramp (220,20,60 / 186,17,51 / 114,10,31 / 87,8,24 / 61,6,17, the same
light-to-dark steps as the other sides; the build rounds palettes to 6 bits,
giving 220,20,60 / 184,16,48 / 112,8,28 / 84,8,24 / 60,4,16 in the game) went into
`data/art824/common_8bpp.pal` at entries 67–71, which were padding, and the
game data was rebuilt with the Linux recipe in `BUILDING.md` and checked in
as `game/htdata832.pdb`. In every palette entries 0–66 are unchanged and
67–71 are Crimson; the art colours after them are re-chosen by the build
(four fewer of them fit), so terrain shades differ very slightly from the
shipped data: in C4's opening view the average change was about 3 levels
out of 255 per colour channel, which is not visible.

**Exact colours on screen.** The game passes every palette through
`SetHslAdjustedPalette` (the Options brightness/colour sliders), and its
RGB → HSL → RGB round trip was off by one level for some colours even with
the sliders at zero, so Crimson showed as 219,20,60. With all adjustments at
zero the palette is now used as it is, so the main colour is exactly
220,20,60 (#DC143C) on screen; moving the sliders works as before.

**Editor.** `Side.side5` with display name "Crimson" (the unit panel's side
list, unit properties, trigger sides), `CaSide.Side5` (appended last, saved
as bit 9) shown as "Crimson (Side5)" in trigger arguments,
**Settings → Side 5 (Crimson)** for its starting credits, view and
intelligence, and crimson unit pictures. Levels saved before side 5 existed
have one trigger list and one side flag fewer; both grow when used.
The embedded `res.h` used for mission packs defines `kside5` and
`knCaSideSide5` too.

**Tested** (`game/sdl/win`, under Wine) with two levels made from C4 by the
editor's own code and played through Play Level in Game:
- C4 with the ACME side (side 2) moved to Crimson: its 37 units, 7 unit
  groups, its starting info, the "Crimson and side 1 are allies" trigger
  and the "destroy the Crimson base" victory condition (side bit 9). The
  debug build ran it with the computer players active for several minutes
  without an assert.
- C4 with the player's own side moved to Crimson: buildings, units,
  minimap dots and the build menu's pictures are crimson; saving and
  loading worked.
- Original C4 and M4 still load and play with the rebuilt data; in M4
  side 1's units are their original blue (0,116,232), pixel for pixel as
  many as before.

Not tested: multiplayer with a fifth player (the lobby and servers only
know four).

## 28. Side 2 in bright red (#FF3333)

`python3 tools/set_side_color.py 2 FF3333` turned side 2's ramp (originally
232,32,0 down to 64,8,0) into 255,51,51 / 215,43,43 / 132,26,26 /
101,20,20 / 70,14,14 in the game data's palettes and the source palettes.
(Earlier tries on this branch were #FF0000, #FF6347 and #FF6666.) Tested
with C4 changed so the player is side 2: units and buildings draw with
exactly #FF3333 and its darker shades, and none of the earlier colour
remains. The same entries colour Fox's text in the in-game messages. A data
rebuild rounds to 6 bits (252,48,48).

## 29. Allies share what they see

In the original game only the local player's own units and buildings lifted
the fog of war (`m_pplr == gpplrLocal` in `Unit.cpp`, `MobileUnit.cpp` and
`Struct.cpp`), so an ally's base and army stayed hidden under fog even
while you fought alongside them. Now:

- `IsVisionSharedWithLocal(pplr)` (`Player.cpp`) is true for the local player
  and for any side that is a *mutual* ally (each lists the other in its
  allies). One-way alliances and the neutral side don't share. Those three
  fog checks use it.
- `PlayerMgr::SetAllies` ends with `RevealSharedVision()`, which lifts the
  fog around every unit and building of the new allies. Units also lift fog
  as they move, but buildings never move and missions usually make
  alliances with a trigger just after the level loads, after everything
  was placed.
- Fog in this game never closes again once lifted, so if an alliance ends
  (M9 does this) what you have seen stays visible; new ground is no longer
  uncovered by the former ally.

Fog only affects what is drawn, and which Galaxite a human player's miners
look for in single player; computer players' miners and all multiplayer
miners already ignore fog, so the simulation is otherwise unchanged.

Tested with M9 "Limited Partnership", where you (side 1) and OMNI (side 2)
are allies from the start: before the change only your starting corner was
uncovered; after it OMNI's base and units were visible on the map and
minimap. The debug build ran the mission for 3 minutes without an assert.

# Part 6: online features removed (branch `feature/crimson-side`)

## 30. What was removed

The original servers for multiplayer, the leaderboard and the add-on mission
pack catalogue no longer run, so this branch removes the code that used
them. Single player is unchanged.

- **Multiplayer**: the lobby, rooms, game creation and game start forms, the
  login form, chat, the transport layer (`xtransport`, `SocTransport`,
  `transportmgr`) and the network messages (`mpshared/netmessage`,
  `messages`, `xpump`, `xmsg*`). The simulation no longer has a server
  command queue, lag handling, "waiting for all players" form, observer mode,
  resign or disconnect handling; `SimUIForm::OnTimer` just runs
  `gsim.Update(&gcmdq)`.
- **`gfMultiplayer` and `Game::IsMultiplayer()`**: every place that tested
  them now does what single player always did (unit costs, armour, build
  times and upgrades use the single player values; unit limits use the
  single player limits; god mode, ecoms, cut scenes and "jump to mission"
  triggers work as before).
- **Leaderboard and score upload**: the LEADERBOARD button, the web view,
  win statistics upload, the sync-error uploader and the state tracker.
- **Forums button** and the URL-opening and "ask" helpers behind it.
- **Downloading mission packs**: the ADD-ON MISSION PACKS button, the
  download form and progress box, the HTTP pack and index loaders and the
  libcurl-based HTTP service. `PackManager` (`mpshared/packmanager.*`) keeps
  only the local part: listing, mounting and deleting installed packs. The
  globals for it moved to `game/packs.h`.
- **The server code**: `server/`, `stats/` and `syncerror/`.
- **Build**: no more libcurl (`build.sh` no longer downloads or builds it),
  and `-liphlpapi` is gone. Winsock (`-lws2_32`) stays, because the game's
  event loop is woken through a loopback socket (`base/eventer.cpp`).

The main menu is now PLAY, LOAD SAVED GAME, OPTIONS and HELP. PLAY goes
straight to the mission list (Story, Challenge, Add-On); the old "Single
Player / Multiplayer" screen is gone. The in-game menu and the battlefield
no longer have a CHAT button. The help text (`data/helpiphone-multiplayer.txt`, now
`data/help.txt`) no longer describes multiplayer, login,
the leaderboard or downloading, and explains installing packs with the editor.

## 31. What was kept

- **Multiplayer maps.** The `M_*` levels stay in the game data and in the
  editor's Author Kit, so they can still be opened and edited. The game's
  mission list only shows single player missions, so they can't be played.
- **Add-on packs installed with the editor** (Game > Install Mission Pack in
  Game) still show under PLAY > Add-On, F5 still launches a level with
  `-pack <id>`, and Options > Delete Mission Pack still works.
- **File formats.** Saved games are unchanged (version 10 on this branch).
  The preferences file keeps its layout; the online account fields
  (user name, password, token, device id, ask URL) are written empty.
  Resource ids in `res.h` and the message ids in `mpshared/mpht.h` were left
  alone.
- **Other platforms** were removed afterwards; see sections 34 and 35.

## 32. Data changes

Two records of `game/htdata832.pdb` were replaced, the rest of the file is
byte for byte the same (so the side colours set earlier are untouched):

- `forms.ini`, built from `data/forms.ini.pp` with the data makefile's
  options (`cpp -P -DTILESIZE=32 -DIPHONE -DMULTIPLAYER`, then
  `inicrunch`). The multiplayer, download and login forms were deleted and
  the main menu re-laid out.
- `help.txt`, packed from the edited help file with `packpdb2`. Packing the
  old help file gives exactly the records the PDB had, so the new records
  are what a full data rebuild would produce.

## 33. Testing

- Release and debug builds compile and link with no references to the
  removed code.
- Main menu shows PLAY / LOAD SAVED GAME / OPTIONS / HELP; PLAY opens the
  mission list; story mission M1 played, objectives and ecoms shown by
  triggers, in-game menu without CHAT, Options (game speed slider), Help.
- Save game, abort mission (summary), load the save again.
- Editor F5 on the 120 × 120 test level launched the game into the level
  through `-pack`.
- The debug build ran the 120 × 120 level for almost 5 minutes of game time
  with fighting (3 enemy units destroyed, 2 own units lost) and no assert.
- Saves made before the Crimson side was added (save version 9) are
  reported as out of date, as they already were on this branch; this change
  doesn't alter the save format.

## 34. Windows only

After the online code went, the other platforms were removed too, so that
the repository only contains what the Windows build uses:

- Deleted: `game/iphone/`, `game/wi.xcodeproj/`, `game/dist.plist`,
  `game/Makefile.iphone` (iOS); `game/sdl/ios/`, `game/sdl/mac/`,
  `game/sdl/htsdl.xcodeproj/` (SDL on iOS/macOS); `game/sdl/android/`;
  `game/sdl/linux/` and `game/makefile.sdl`; `game/sdl/nacl/` and
  `game/makefile.nacl` (Chrome Native Client); the prebuilt SDL2 and libcurl
  copies for those platforms (`game/sdl/SDL2/`, `game/sdl/curl/`);
  `base/epollserver.*` (Linux, only used by the server); `bin/inicrunch_osx`;
  and `tools/audio_tests/`, which were built against the Linux port.
- The `IPHONE`, `__IPHONEOS__`, `__ANDROID__`, `__MACOSX__` and `__LINUX__`
  branches were removed from the shared code (with `unifdef`), keeping what
  the Windows build compiles.
- Kept: the shared SDL2 layer in `game/sdl/` and the Windows host in
  `game/sdl/win/`; `bin/inicrunch_linux` and the Linux part
  of the data makefile, because the Windows build is cross-compiled on
  Linux or WSL. The art sources (`.psd`) are untouched, and the data build
  still uses its `IPHONE` setting, which means 32 × 32 pixel tiles.

## 35. Palm OS and Windows Mobile

There was never an SDL build for Palm OS or Windows Mobile; those versions
predate the SDL layer. Their leftovers were removed too:

- Deleted: `game/win/` (the original Win32 and Windows Mobile/Pocket PC
  port, without SDL), the Palm build files (`game/makefile`,
  `game/makefile.dsp`, `game/sizeops.makefile`, `game/pno_*.h`),
  `game/mkwin.bat`, files only those ports compiled
  (`game/DebugHelpers.cpp`, `game/hashtablecode.cpp`,
  `game/filepdbreader.cpp`), the Palm, Sony, Tapwave, Bluetooth and
  Windows CE SDK headers in `inc/` (only `basictypes.h` and `rip.*` remain)
  and the Palm tools `bin/prc2bin.exe`, `prc2rcp.exe`, `prcview.exe`,
  `splitprc.exe` and `sizeops.exe`.
- The icon moved to `game/sdl/win/app.ico`.
- The `PIL`, `__CPU_68K`, `CPU_68K`, `PNO`, `__MWERKS__` (Palm), `CE`,
  `_WIN32_WCE`, `UNDER_CE` (Windows Mobile), `WIN` and `DEBUG_HELPERS` (old
  Win32 port) branches were removed from the shared code with `unifdef`.
  None of them were defined in the SDL Windows build, and the release
  `HostileTakeover.exe` built afterwards has the same code as before (only
  the timestamp bytes differ).

## 36. Other leftovers

A last sweep removed what didn't belong to the Windows SDL build:

- Online and other-platform leftovers: `utilities/` (Python 2 scripts for
  the mission pack server), `drmutil/` and `bin/drmutil.exe` (registration
  key generator for the Palm and Pocket PC versions), `texpack/`, the iPhone
  icon and title screen art sources, and `game/fingerhandler.cpp` (the
  iPhone touch controls; on Windows it could only be switched on with the
  "Stylus UI" box in the debug test options, which is gone too).
- Data: the old `data/help.txt` and the unused `data/helpiphone.txt` were
  deleted, and `data/helpiphone-multiplayer.txt` (the help the game uses)
  was renamed to `data/help.txt`. `data/makefile` now always builds the one
  data set the game uses (8-bit colour, 32 pixel tiles scaled from the 24
  pixel art); the 4-bit colour and 20/16 pixel tile options and
  `data/preload_4bpp.pal` were removed. `IPHONE=1 MULTIPLAYER=1` on the make
  command line are still accepted but no longer needed; a dry run
  (`make -n -B`) gives the same commands as before.
- Old notes: `game/README.txt`, the `worklist.txt` files,
  `game/calcdiagdist.js` and the CVS `.cvsignore` files.

The Windows art tools Spiffcode used (AniMax, aed, BmpProof and others) were
kept.

## 37. All missions unlocked

Story missions used to unlock one at a time: only the missions up to one
past the last completed one could be played, the rest showed LOCKED (a
hidden gesture on the BACK button, or a debug build, got around it).
`SelectMissionForm` (`game/selectmission.cpp`) no longer locks anything:
every story, challenge and add-on mission can be played from the start, and
completed missions are still marked "Complete". The lock flag, the hidden
unlock gesture and `IsSelectedMissionLocked` were removed.

## 38. Scrolling lists with the mouse

Lists (the build menus, the mission list and others; `ListControl` in
`game/misccontrols.cpp`) were written for touch: an item was selected the
moment the pen went down. With a mouse that meant every drag to scroll a
build menu first selected the item under the pointer, and in the unit build
menus pressing on the already-selected item to start scrolling ordered
another unit. When the drag ended off the list, the building menu's
description and Build button didn't follow the new highlight. Paging with
the arrows could also leave empty rows at the end, and clicking an empty row
cleared the selection and hid the Build/Order button, so it looked as if
nothing could be selected.

Now a list selects on release, and only when the mouse wasn't dragged (a
click); dragging only scrolls. Clicks on empty space leave the selection
alone, arrow paging stops at a full last page, and the building menu updates
its description and Build button whenever the selection changes.

## 39. Editor limits and renaming unit groups

The editor refused levels with more than 100 scenery items, 32 areas, or
more starting buildings and units per side than the original limits (for
example 39 buildings and 60 units for the player). On 120 × 120 maps those
limits get in the way. What each one really protected:

- **Scenery and per-side units/buildings.** The game creates everything a
  level places without checking these limits; they only stop more being
  built or scenery added. A level that started above a limit couldn't build
  anything. Now `Game::FitLimitsToLevel` (`game/game.cpp`), run after a
  level or saved game is loaded, raises each limit to what the level starts
  with plus room to build (10 buildings, 20 units); the original limits stay
  the minimum, so the built-in missions play exactly as before. The debug
  build's count checks in `GobMgr::TrackGobCounts` no longer compare against
  the limits.
- **The object list.** All units, buildings, scenery, shots and effects live
  in one list of `kcpgobMax` entries, which was 768. It is now 2048, and
  `GobMgr::IsBelowLimit` keeps the last 32 entries free so building can't
  use them up. The editor's only object check is now a total of 1600 placed
  units, buildings and scenery, leaving room for what is built during play.
- **Areas.** These were a real limit: each area is one bit of an
  `AreaMask`, a 32-bit number, and `GobMgr::LoadAreas` wrote past its
  32-entry array (in release builds) when a level had more. `AreaMask` is
  now 64 bits, so 64 areas fit, and extra areas are ignored instead of
  overwriting memory. The editor now allows 64.

Saved games are unaffected (area masks and the limits are worked out again
when a game is loaded).

Renaming unit groups (Settings > Unit Groups) only worked by clicking an
already selected name, and even then the change didn't mark the level as
changed, so it could be lost. There is now a Rename button, F2 and
double-click start the rename, empty and duplicate names are refused, and
the level is marked changed. Triggers refer to unit groups by position, not
by name, so they keep working after a rename.

Tested with a 120 × 120 level that has 321 scenery items, 55 areas and 139
of the player's units (all over the old limits): it passes the editor's
checks, loads in the release and debug builds (no asserts), and the player
can still order more units.

## 40. Renaming areas in the editor

Triggers (the area argument of conditions and actions) and unit groups (the
spawn area) store an area's *name*; only when the level is saved is it turned
into a position in the sorted list of names. Renaming an area in the property
grid therefore left every trigger that used it pointing at a name that no
longer existed ("Area X in trigger Y doesn't exist!", and an index of -2 in
the saved level). Nothing stopped two areas having the same name either, and
every area dropped from the panel was called "New Area".

- `Area.Name` (m/Area.cs) trims the new name and asks the level
  (`LevelDoc.CheckAreaName`) whether it's allowed: empty names, names
  starting with `[` (reserved for `[LastDiscovery]`) and names already used
  by another area are refused with a message and the old name is kept.
- `LevelDoc.OnAreaRenamed` then updates every `CaTypeArea` in every side's
  trigger conditions and actions, and every `UnitGroup.SpawnArea`, and marks
  the triggers, unit groups and level as changed.
- Placing areas (dropping from the panel, Paste, Ctrl-drag copy) gives them
  unused names (`LevelDoc.MakeAreaNamesUnique`: "New Area 2", "Base 3", ...).
  A plain drag-move inside the map keeps the name (`LevelView.m_fDragSource`).
- Validation reports areas that share a name, for older levels.

Tested on C_05.ld: renaming area d1 (used 3 times) moved all 3 references to
the new name, the level still validates with no area errors, and the
references survive save and reload.

Unit groups for Crimson: the Side list in Settings > Unit Groups
(m/UnitGroupsForm.cs) only offered sides 1-4, so a group couldn't be given
to Crimson (side 5). Crimson is now the fifth entry; the group is saved as
`Side=kside5`, which the game already maps to the Crimson player.

## 41. Force attack (Ctrl+right click)

Holding Ctrl while right clicking with units selected makes them attack
whatever is under the cursor, including the player's own or allied units and
buildings.

- `sdl/host.cpp` adds `kfEvtForceAttack` to mouse button events while Ctrl is
  down. `stylushandler.cpp` calls `SimUIForm::ForceAttack` when a Ctrl+right
  click is released. Ctrl+left click is an ordinary left click.
- `SimUIForm::ForceAttack` hit-tests, then calls `SetSelectionTargets` with
  `gfForceAttackCommand` set.
- `UnitGob::m_gidForceTarget`: `MobileUnitGob::SetTarget` and
  `TowerGob::SetTarget` set it to the target for a forced order and clear it
  for any other order, and skip the "ignore allies" check for it.
  `UnitGob::IsValidTarget` accepts that one target even if it's an ally, so
  the attack state keeps attacking it until it dies or a new order is given.
  A unit never targets itself. Allies hit this way don't fight back (their
  return fire still fails `IsValidTarget`).
- Not saved: after loading a game, a forced attack on an ally stops.
- Kill statistics: a unit or building destroyed by its own side or an ally
  doesn't count as an enemy kill for the attacker (the `kmidHit` handlers in
  MobileUnit.cpp and Struct.cpp check `IsAlly(sideAssailant)`). It still
  counts as lost for its owner.
- Ctrl+right click on empty ground does nothing (the engine has no
  attack-ground), nor on a Replicator. Units that can't attack ignore it.

## 42. Computer unit/building limits are per computer player

The computer could stop building completely. The single-player limits for
the computer (117 mobile units, 72 buildings) were one pool shared by every
computer-controlled side, including the neutral side. So in a level with two
computer opponents, or one with a big army or base, or neutral buildings,
one side used up the pool and the others couldn't build anything until
enough of those units died. On top of that, `CalcUnitCountDeltas` cut the
computer's building limit to its starting buildings + 5 and gave the rest to
the human player.

- `GobMgr::IsBelowLimit` (GameObjects.cpp): for a computer player it counts
  only that player's units/buildings (`Player::GetUnitInstanceCountFromMask`)
  and that player's build queues (`BuilderGob::GetQueuedCount`), so each
  computer side has its own 117 / 72. The human limits are unchanged.
- `CalcUnitCountDeltas` no longer lowers the computer's building limit (the
  human still gets the extra building room it got before).
- `FitLimitsToLevel` fits the computer limits to the biggest computer side,
  and the room left above the starting count is now 60 units and 20
  buildings (was 20 / 10).
- The overall object guard (`m_cpgobActive >= m_cpgobMax - 32`, kcpgobMax
  2048) still applies.

Effect on the built-in missions: a computer side that keeps ordering units
or rebuilding buildings can now have up to the full limit on its own, so
missions with two computer sides (e.g. C_04, S_09) or neutral buildings can
field more enemy units than before.

Tested with C_04 plus 120 extra units for computer side 1 (141 at the start;
side 2 has 18). With logging, after about 4 minutes side 1 had 163 units and
side 2 had 40, both still allowed to build: about 200 computer units in all,
past the 179 the old shared limit would have allowed.


## 43. Bigger limits on big maps

Maps bigger than 64 x 64 tiles (the size of the original editor's maps; every
built-in mission is 64 x 64 or smaller) get limits scaled by their area, up to
4x. `Game::FitLimitsToLevel` works out
`percent = map tiles * 100 / (64 * 64)`, clamped to 100..400, and scales the
unit and building limits (human and per computer player), the shot
(support) limit and the scorch-mark limit. The "starting count + headroom"
rule from section 42 still applies on top, whichever is bigger.

| Limit | 64 x 64 or smaller | 120 x 120 (351%) |
|---|---|---|
| Your units | 88 | 308 |
| Your buildings | 55 (+ unused computer room) | 193 (+ unused computer room) |
| Units per computer side | 117 | 410 |
| Buildings per computer side | 72 | 252 |
| Shots in flight | 50 | 175 |
| Scorch marks | 30 | 105 |

The shot limit matters in big fights: when it's reached, units hold fire.

To make room, `kcpgobMax` (all objects in a level) went from 2048 to 6144.
Gids are 15-bit indexes, so this is still well within range. The scratch
buffer is now at least `kcpgobMax * 8` bytes, since it holds per-object
arrays (the sprite sort, the nearby-enemy search).

Checked on the 120 x 120 test map: the limits above are what the game uses,
and the map plays normally.

## 44. Volume slider in Options

The Windows build only ever shows the GAME OPTIONS form
(`DoModalGameOptionsForm` skips the old options menu with its SOUND
button), so there was no way to change the volume, even though the sound
manager, the SDL sound device and the saved preferences all support it.

- `data/forms.ini.pp` `[kidfInGameOptions]`: a Volume label, slider and
  percentage (new ids `kidcInGameVolume`, `kidcInGameVolumeLabel`,
  `kidcInGameVolumeString` in res.h), with the other controls moved up a
  little to fit.
- `InGameOptionsForm` (GameOptions.cpp): the slider goes 0-100% in 5%
  steps (sound manager volume 0-255). Moving it changes the volume at once
  and plays a click so you can hear the level; Cancel puts back the old
  volume, Default sets 100%, OK saves it with the other preferences
  (`Preferences.nVolume`, already saved and loaded at start-up). Sound
  that was turned off with the old Mute setting shows as 0% and is turned
  back on when the slider moves. With no sound device the slider is hidden.
- The forms.ini record in `game/htdata832.pdb` is rebuilt from
  forms.ini.pp: `cpp -P -Wno-trigraphs -DTILESIZE=32 -I../game
  forms.ini.pp ini.tmp`, then `inicrunch_linux ini.tmp forms.ini` (the
  unchanged file rebuilds byte for byte), then replaced in the pdb.

Tested with SDL's dummy audio driver (the test machine has no sound card):
the slider shows 100% by default; setting 40% and OK is still 40% after
restarting the game; moving it to 5% and pressing Cancel goes back to 40%.

## 45. Other limits raised: objects on screen, switches, triggers

- **Objects drawn on screen** (`kcpgobVisibleMax`, ht.h): 200 -> 1024. Each
  frame `Simulation::FindVisibleGobs` collects the objects in view into an
  array of this size; anything past the end wasn't drawn and couldn't be
  clicked (`HitTestGob` uses the same list), though it was still in the game.
  300 infantry placed on one screen of C_05: the old build stopped drawing
  about halfway down the screen (units, the HQ and scenery missing), the new
  one draws all of it.
- **Switches** (`kcSwitchMax`): 16 -> 64. The switch states are in the save
  game, so `TriggerMgr` state is now version 3 (writes 64); version 2 save
  games (16 switches) still load (`kcSwitchMaxOld`). Checked: a game saved by
  the previous build loads and continues in the new one, and a new save loads.
- **Triggers per side** (`kcTriggersPerSide`): 128 -> 512.
  `m_mpSide2nTrigger` held each side's trigger numbers as bytes with 0xff as
  the end marker, which also limited a level to 255 triggers in total (the
  editor didn't check that). It's now words with 0xffff as the end marker, and
  the loop that runs a side's triggers stops at the end of the row (with
  exactly 128 triggers it used to run on into the next side's row).
  Not saved, so no save game change.
- **Editor checks** (m/LevelDoc.cs): switches 64, triggers per side 512, and
  the total of placed units, buildings and scenery 5000 (was 1600, set when
  the game's object list was 2048; it's 6144 now, and room is kept for units
  built, shots and scorch marks).

Checked with a C_05 copy with 60 extra switches and 301 extra triggers for
every side (317 triggers for side 1): a "Mission loaded" trigger sets switch
51 and the last trigger, number 302 of its side, shows an alert when switch 51
is on. The alert appears, and the mission's own triggers still run.

## 46. Smooth rendering (frame rate no longer tied to game updates)

What held performance back on a PC (measured on a level with 300 units:
drawing a frame took about 2 ms and a game update under 0.1 ms):

- The screen was only redrawn when the Simulation updated, 12.5 times a
  second at normal speed (`gtGameSpeed` ticks of 10 ms; 25 at 2x), so the
  game ran at 12.5-25 FPS and units jumped from spot to spot.
- Arrow-key scrolling jumped 64 pixels per key repeat.
- The SDL event wait slept in 10 ms steps.

Changes:

- **Drawing between updates.** `Simulation::Update` saves every gob's
  position (`Gob::SavePrevPosition`) and the time (`gmsLastSimUpdate`)
  before moving anything. For each frame (`gamePaintEvent` in
  game.cpp), `Simulation::BeginInterpolatedDraw` puts each gob that moved
  part way from its old to its new position, by the fraction of the update
  interval that has passed, and marks it for redraw; after `DrawFrame`,
  `EndInterpolatedDraw` puts the real positions back. Only drawing sees the
  in-between positions, so game logic, timing and save games are unchanged.
  Moves longer than 3 tiles in one update (units coming out of transports
  and the like) aren't smoothed. The normal dirty-rectangle invalidation
  handles the rest (`Gob::Invalidate` unions the last drawn rect).
- **Frames between updates.** `SmoothRenderTimer` (SimUI.cpp) runs every
  10 ms tick while a mission is up and asks for a redraw while something was
  drawn in between (`gfInterpolatedLastPaint`), so frames come up to 100 a
  second while things move and stop when nothing does.
- **VSync.** `Display::SetVSync` (sdl/display.cpp) makes presenting wait
  for the monitor's refresh, so frames are evenly paced and don't tear.
  It's off at the fastest game speed (an update every 10 ms), where waiting
  would slow the game.
- **Smooth arrow-key scrolling.** `HostGetScrollKeys` (sdl/host.cpp) reads
  the held arrow keys; the timer scrolls 1000 pixels a second times the
  Scroll Speed option, with sub-pixel remainders kept. The key events
  themselves no longer scroll.
- **Event wait** (sdl/sdleventserver.h) sleeps 1 ms at a time instead of 10.
- **F** shows / hides the existing FPS counter.

Checked under Wine: 100 FPS on C_05 while units move (was 25 at 2x speed),
the game still makes exactly 25 updates a second at 2x, a moving tank's
health bar moves about 1 pixel every 20 ms frame instead of jumping, holding
an arrow key scrolls about 20 pixels per 20 ms frame, and no drawing
leftovers. VSync pacing can't be seen here (no real display); on Windows it
caps frames at the monitor's refresh rate.

## 47. Miners (Bullpups) that stop mining

Two causes, found by logging every miner state change in test games:

1. **Storage full (by design).** A Processor only takes Galaxite while the
   player's credits fit in storage (3000 per Processor, 5000 per
   Warehouse). When they don't, the miner waits *inside* the Processor
   (`kstProcessorTakeGalaxite`) until credits are spent, the next miner
   queues at the door, and the "warehouse too full" alert plays at most every
   30 seconds. In C_04 and C_05 the computer starts with more credits than
   it can store, so its miners park in the Processor for good. This is how
   the original game works and is unchanged, except: when the credits were
   just under the limit (e.g. 2990 of 3000) the step (+20) never fit, so the
   miner waited even though storage wasn't full. Now storage is filled up to
   the limit (Processor.cpp).
2. **Stuck after a blocked path (bug).** Several miner states end in
   `kstGuard` with nothing to start them again: approaching Galaxite or
   stepping aside when the path stays blocked (`knMoveStuck`), reaching a
   Processor that was destroyed meanwhile with none left, and waiting at a
   busy Processor whose "come in now" notification never arrives (e.g. the
   door was blocked by another unit rather than a miner). Such a miner sat
   idle forever. Now `MinerGob` keeps `m_fKeepMining` (set by mine and
   deliver orders, cleared by any other order from the player or a trigger
   and when no Galaxite can be found at all) and overrides `kstGuard`:
   while idle and supposed to be mining, every ~3 seconds (40 updates) it
   delivers if it's full or was trying to deliver, otherwise goes back to
   mining near its last spot. A miner inside a Processor is hidden and
   isn't touched. Miner save state is version 3 (adds the flag); version 2
   save games load, with the flag set for miners that have mined before.

## 48. Other "stops for good" problems: path search budget, AI builds after loading

Looking for the same pattern as the miners (something gives up and nothing
starts it again):

- **Path search ran out of budget.** `TerrainMap::FindPath` stopped after
  map width * 9 path heads (a 2003 handheld budget) and returned a path to
  the closest point found so far; the unit walked there and stopped short of
  where it was sent (`knMoveStuck`, then Guard). Measured with a temporary
  test that pathed between 400 random reachable tile pairs per map: 7 of 400
  paths on C_02 (49 x 55) and 2 of 400 on the 120 x 120 test map came up
  short. `CNodesLimit` is now one path head per map tile: all 1200 test paths
  reached their destination; the slowest took 14 ms on the 120 x 120 map,
  most well under 1 ms.
- **Computer stopped sending a unit group after loading a save game.** The
  BuildMgr's list of units the computer has ordered isn't saved ("UNDONE:
  load/save" in BuildMgr.cpp), so a unit group that was still waiting for
  units when the game was saved waited forever after loading, e.g. a
  repeating attack wave never came again. `UnitGroupMgr::LoadState` now asks
  for those units again (`UnitGroup::RequestMissingUnits`). Tested on C_04:
  saved after 45 seconds, loaded, and one side 2 group re-ordered its 11
  missing units. A unit that was already in a factory queue when the game
  was saved is still finished, so the group may get one extra unit.
- Looked at and left alone: ordinary move orders that are blocked end in
  Guard where the unit is (visible to the player, as in the original); group
  Move actions treat a blocked unit as arrived, so the group carries on;
  attackers that get blocked retarget the target's current position; units
  waiting at a Replicator re-check every update.

## 49. Save game review

Went through what is and isn't saved (all `SaveState` / `LoadState`
code, the raw-written structures and the stream handling).

- **Fixed: a save game could vanish.** `HostOpenSaveGameStream`
  (sdl/savegame.cpp) renamed the file to `htsavetemp` while loading and
  renamed it back afterwards, but only if every read succeeded; if the game
  crashed or a read came up short during loading, the save game was gone
  from the list. It's now read where it is (and deleted afterwards only
  for the "resume where you left off" save). Checked: loading a save game
  leaves it in the list; leaving a mission with Exit and starting the game
  again resumes the mission and removes the resume save.
- **Fixed: the Overmind** (the optional computer AI that a level can choose
  per side; none of the built-in missions use it) isn't saved, so after
  loading it starts again and sends its miners to mine. It now only does
  that for idle, active miners: a miner inside a Processor being emptied is
  hidden and inactive and must not be sent off. Found by reading the code,
  not seen in a game.
- **Fixed earlier:** computer unit groups waiting for units (section 48),
  switch count (section 45, older save games still load), miner retry flag
  (section 47, older save games still load).
- **Checked, compatible:** save games made by builds of this port since
  multiplayer was removed load (the only format changes since then are
  version-checked). Older save games are refused with "out of date" by the
  existing version check rather than misread.
- **Not saved, left alone:** shots in flight (bullets and rockets vanish, so
  that damage is lost); smoke and other effects; the Overmind's attack
  timers; force attacks on allies (section 41); the computer's ordered-units
  list itself (rebuilt, section 48).
- A save game is tied to the exact version of its mission; editing a custom
  mission in the editor and then loading an older save of it shows "This
  saved game is based on an older version of this mission!" (by design).

## 50. Galaxite regrowth (per-level setting)

A level can make mined Galaxite grow back, slowly, to its starting amount.

- **Editor:** `LevelDoc.GalaxiteRegrowth` (Off / Slow / Normal) in
  Settings > Level. Saved in the .ld and, when not Off, written to the level
  file as `[General] GalaxiteRegrowth=1` (Slow) or `2` (Normal). Levels
  without it (all the built-in missions) are Off and unchanged.
- **Game:** `Level::LoadGalaxiteRegrowth` reads the setting and each tile's
  starting Galaxite (the level's `Galaxite` lines) from the level file. It
  runs in `LoadLevelConstants`, so also when a saved game is loaded; nothing
  new is saved (the current amounts are in the saved FogMap, timing comes
  from the saved update count). `Level::UpdateGalaxiteRegrowth`, called from
  `Simulation::Update`, grows every tile below its starting amount by one
  size every 60 s (Slow) or 30 s (Normal) of game time, the map split into
  16 interleaved slices so patches don't all grow at the same moment. Tiles
  with a building on them are skipped.
- `FogMap::RegrowGalaxite`: amounts 1-5, 6-10 and 11-15 are three patch looks
  with five sizes each; a mined-out tile (0) comes back as the smallest size
  of its original look, then grows one size at a time up to the start amount.
- **Miners:** with regrowth on, a miner that finds no Galaxite keeps trying
  every ~3 seconds (the retry from section 47) instead of giving up, and the
  "miner needs Galaxite" alert is shown once until it mines again
  (`MinerGob::OnNoGalaxite`).

Tested on a copy of C_04 set to Normal: tiles the computer's miners mined
grew back one size about every 29 seconds (one step per 368 updates), never
above their starting amount; the setting survived saving and reopening the
level in the editor.

Rebalanced afterwards: Slow 200 s and Normal 100 s per size
(`kcsecGalaxiteRegrowSlow` / `Normal` in ht.h), and patches only grow back
to `knGalaxiteRegrowPercent` (60%) of their starting size, rounded up: a
size 5 patch to 3, 4 to 3, 3 to 2, 2 to 2, 1 to 1. A full patch (about 500
credits) mined out on Slow is back to size 3 (about 300 credits) after
10 minutes (5 minutes on Normal).

Changed to spreading: a mined-out tile only comes back when one of its 8
neighbors has Galaxite (`Level::UpdateGalaxiteRegrowth`), so fields fill
back in from what's left of them, only over tiles that had Galaxite at the
start, and stop at the regrowth size. Tiles that still have some Galaxite
grow as before. A completely mined-out field has nothing to spread from and
stays empty. Tested on the C_04 copy with the Normal time shortened to 10 s
for the test: mined-out tiles next to remaining Galaxite came back and the
fields filled in again.

## 51. Placing a structure near the right side / edge of the map got stuck

Symptom: dragging a building's placement indicator toward the right of the
screen (most easily at the right or bottom-right edge of the map) left it
stuck: it couldn't be grabbed again and its OK / X buttons didn't respond,
so the only way out was to wait or open the menu.

Causes and fixes (`game/Headquarters.cpp`, `PlaceStructureForm`):

- The form is loaded from `forms.ini` at `(0 0 320 320)`, i.e. 640 x 640
  here, a size made for square handheld screens. On 800 x 600 everything
  at x >= 640 was outside the form, so `FormMgr::CookPenEvent` gave those
  clicks to `SimUIForm` instead. `SetOwner` now sizes the form to the sim
  DIB (the whole playfield), as `SimUIForm::Init` does for itself.
- The minimap always covers the bottom right of the screen, and at the
  map's bottom-right corner the view can't scroll that area out from under
  it. `UpdatePlacementIndicator` now keeps the indicator plus its button
  margin out of the minimap's screen rect, moving it up or left (the
  shorter move). Building under the minimap needs the map scrolled first.
- Desktop extras: right click while placing cancels it
  (`PlaceStructureForm::OnPenEvent`), and Escape (now mapped to `vchrBack`
  in `sdl/host.cpp`) cancels placement in `InputUIForm::EventProc`, or
  otherwise opens the in-game menu / closes the top dialog.

## 52. Sharp, fixed window size

Before, the game always drew 800x600 and the resizable window stretched it
with linear filtering, and the exe wasn't DPI aware, so Windows display
scaling (125%, 150%) stretched it again: the picture was blurry on most
monitors.

Now (`game/sdl/win/hosthelpers.cpp` `CalcGameSize`, `game/sdl/display.cpp`):

- Room for the game = the monitor's work area (`SDL_GetDisplayUsableBounds`,
  i.e. without the taskbar) minus the window's title bar and borders,
  measured with `SDL_GetWindowBordersSize` on a hidden probe window.
- The game resolution is that room divided by the largest whole number n
  that keeps it at least 640x480 (1920x1080 -> about 952x496 x2,
  2560x1440 -> about 1272x676 x2, 3840x2160 -> about 952x516 x4,
  1366x768 x1). The game already handles any screen size (the Android
  build used the phone's), so forms, the minimap and the playfield lay
  themselves out for it. Its tallest form is 320 px, so 480 is plenty.
- The window is a normal window, not resizable (so no maximize), placed so
  its client area is centered in the work area, scaled with `nearest`
  filtering and `SDL_RenderSetIntegerScale`, so each game pixel is an exact
  n x n block.
- `SDL_HINT_WINDOWS_DPI_AWARENESS=permonitorv2` is set before SDL video
  starts, so sizes are in real pixels and Windows doesn't bitmap-stretch
  the window.
- F11 / Alt+Enter (fullscreen toggle) are swallowed and
  `Display::ToggleFullscreen` does nothing: by request there's no way to
  change the window setup.

## 53. Mouse lag (moving, clicking, box select)

Symptom: moving the mouse made the game lag; clicks, selection and box
select reacted late, more so the more the mouse moved.

Cause: every `SDL_MOUSEMOTION` became a `penMoveEvent`, even with no button
down (a desktop mouse reports 125-1000 moves a second). `EventMgr::GetEvent`
sets `kfRedrawBeforeInput` on each pen move, so a full redraw (waiting for
vsync) happened before the next input was read: about 60 moves handled a
second, the rest piling up in SDL's queue ahead of clicks and key presses.

Fix (`game/sdl/host.cpp`, `ProcessSdlEvent`):

- Moves with no left / right button held are dropped; the game (made for
  touch screens) has no use for hover moves.
- With a button held, moves already queued right behind are skipped so only
  the newest position is handled (only while the next queued event is a
  move, so the order with clicks and keys is kept).

Measured under Wine with bursts of 1500 synthetic moves: release of a box
select was handled 526 ms / 116 ms after it happened before, 67 / 27 ms
after; clicks after hovering 11-13 ms before, 0 after. A real high-rate
mouse with real vsync lagged much more than that before.

## 54. Mouse map scrolling, bigger build queues

Scrolling (`game/SimUI.cpp` `SmoothRenderTimer`, `game/sdl/display.cpp`
`Display::GetMouseScrollState`): the 10 ms render timer polls the mouse
(`SDL_GetMouseState`, mapped to game pixels with
`SDL_RenderWindowToLogical`; only while the game window has the input
focus, the mouse is over it and the game, not a menu, has the focus).

- Mouse within a few pixels (3 game pixels, at least 6 window pixels) of a
  window edge scrolls that way like the arrow keys, same speed setting.
  Not while over the minimap, which sits in the bottom right corner.
- Middle button held: the map follows the mouse (view = view at press -
  mouse movement since). Middle button events and moves never reach the
  game's pen input (`ProcessSdlEvent` ignores other buttons, and drops
  moves with no left / right button), so this can't select or order.

Build queue (`game/ht.h`, `game/Builder.cpp`): `kcBuildQueueMax` 10 -> 99
(the queue holds all unit types together, so a few of each filled it).
Saves keep working both ways: `BuildQueue::LoadState` reads the saved
length, clears the rest and gives loaded queues (other than the HQ's
1-long one) the new size. Also fixed on the way:

- `BuilderGob::Build` ignores an order when the queue is full (orders are
  queued commands; several could arrive after the UI checked), instead of
  `Enqueue` writing past the array.
- `BuildQueue::RemoveUnit` returns when the unit isn't queued (the comment
  says that can happen) instead of `memmove`-ing to index -1, and no
  longer reads one byte past the array.

`BuilderGob`'s size changed: clean rebuild (`rm -rf release`).

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
| `game/selectmission.cpp` | Packs without an author show just their title |
| `game/missionlist.cpp` | The editor's test pack isn't listed with add-on missions |
| `m/GameMenu.cs` | The editor's Game menu |
| `tools/build_dotnet_tools.py` | Builds the editor and data tools for .NET 4 |
| `ppini/` | C preprocessor subset for the data build |
| `data/build-data.bat`, `data/makefile` | Data build on Windows |
| `game/sdl/win/package-editor.sh` | Packages the editor and Author Kit |
| `game/sdl/win/README-Editor.txt` | Editor README shipped in the zip |
| `game/sdl/sdlsounddev.cpp` | Stereo 44.1 kHz mixer, 16 voices, master volume |
| `game/sounddevice.h` | `PlayAdpcmEx`: per-sound volume and pan |
| `game/soundmgr.cpp` | `PlaySfx(sfx, wx, wy)` and `CalcSfxPosition` |
| `game/ht.h`, `game/game.cpp` | Map size limit (127 in the engine), coordinate tables |
| `game/terrainmap.cpp` | Path finding on big maps; "Map too big" check |
| `game/SimUI.cpp` | Minimap at 1 pixel per tile on big maps |
| `m/LevelDoc.cs` | Map size up to 120 × 120; canvas grows with it |
| `tools/set_side_color.py` | Sets a side's colour in the game data and source palettes |
| `paltool/PalTool.cs` | Skips the side key colours when building palettes |
| `mpshared/side.h`, `mpshared/mpht.h` | Side 5 (Crimson): side count, masks, players |
| `data/art824/common_8bpp.pal` | Crimson ramp at palette entries 67–71 |
| `game/Player.cpp` (`IsVisionSharedWithLocal`, `RevealSharedVision`) | Allies share vision |
| `game/packs.h`, `mpshared/packmanager.*` | Local mission packs only (online code removed) |
| `data/forms.ini.pp`, `data/help.txt` | Main menu without online buttons; help text |
