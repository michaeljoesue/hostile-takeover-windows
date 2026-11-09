# Hostile Takeover for Windows 11

A native Windows port of **Hostile Takeover**, the open source release of the
mobile real-time strategy game *Warfare Incorporated* by Spiffcode, Inc.

The original source ([spiffcode/hostile-takeover](https://github.com/spiffcode/hostile-takeover))
builds for iOS, Android, macOS and (partly) Linux. This fork adds a Windows
10/11 version built from the game's SDL2 port, aimed at single-player play
with a mouse and keyboard, and brings the game's map & mission editor, **M**,
along with it.

> This is an unofficial fan port, not a Spiffcode release.

## Playing

1. Download `HostileTakeover-Windows.zip` (from the Releases page, or build it
   yourself as described below) and unzip it anywhere.
2. Double-click `HostileTakeover.exe`. Nothing is installed and no extra DLLs
   are needed.
3. If Windows shows **"Windows protected your PC"**, click **More info → Run
   anyway**. This happens for any program that isn't code-signed.
4. Choose **Play → M0: Introduction** to start the story.

### Controls

| Input | Action |
| --- | --- |
| Left click | Select a unit or building, press buttons |
| Left drag | Select several units with a box |
| Left click on empty ground | Deselect everything |
| Right click on the ground | Move the selected units there |
| Right click on an enemy | Attack it with the selected units |
| Right click on your own unit or building | Open its menu (the phone version's "tap and hold"; holding the left button for half a second also works) |
| Ctrl + right click on anything | Force attack: the selected units attack it, even your own units and buildings |
| Ctrl + Alt + right click on the ground | Attack-move (Red Alert style): move there, fighting enemy units and defence towers met on the way (not buildings without weapons), then carry on |
| Ctrl + Alt + right click on your own or an allied unit or building | Guard / protect it (Red Alert style): the selected units stay near it, follow it when it moves and fight enemy units and towers that come near it |
| Right click in a build menu | Cancel one queued unit (on the unit's icon or the Order button); never queues a new one |
| Right click or Escape while placing a building | Cancel placing it (same as its X button) |
| Arrow keys | Scroll the map (smoothly, while held) |
| Mouse at the edge of the window | Scroll the map that way (not over the minimap) |
| Middle mouse button drag | Grab and move the map with the mouse |
| F | Show / hide the frames-per-second counter |
| Click the minimap | Jump to that part of the map |
| M or Escape | In-game menu (save, load, options); Escape also closes menus and dialogs |

These are modern-RTS style controls. The in-game tutorial was written for
phones, so when it says "tap on grass to move", right click instead.

The game runs in a window of fixed size picked for your monitor: it fills the
screen above the taskbar, and every game pixel is drawn as an exact square
block of screen pixels, so the picture is sharp (no blurry stretching). On a
1920×1080 screen the game is about 952×496 shown at 2×; 2560×1440 → about
1272×676 at 2×; 3840×2160 → about 952×516 at 4×; 1366×768 → about 1350×690 at
1×. The window can't be resized or made fullscreen, and there's no setting to
change it.

Sound is in stereo: battles to the left of the screen come from the left
speaker, and fighting off screen is quieter the farther away it is. Options (main menu,
or M in a game) has a Volume slider.

### Saves

Saved games, progress, settings and a `log.txt` are stored in
`%APPDATA%\HostileTakeover`. To keep everything next to the game instead
(for example on a USB stick), create an empty file called `portable.txt`
beside `HostileTakeover.exe`.

### Known limitations

- Single player only. On branch `feature/crimson-side` the online features
  (multiplayer, chat, the leaderboard and score upload, the Forums button and
  downloading add-on mission packs) have been removed, since the original
  servers are gone. Add-on mission packs are installed with the editor (see
  below). The multiplayer maps (`M_*`) are still in the game data and the
  editor, but the game doesn't play them.
- Each factory / barracks can queue up to 99 units, any mix of types
  (it was 10 in all).

## Map & mission editor

The zip includes **M**, the editor Spiffcode used to make the game's missions,
in the `Editor` folder. Start `Editor\M.exe`; it runs on the .NET Framework 4
that Windows 10 and 11 already include. Its **Game** menu connects it to the
game:

| Command | What it does |
| --- | --- |
| Play Level in Game (F5) | Starts the game straight into the level you're editing |
| Install Mission Pack in Game | Installs the open levels as a mission pack; it shows up under Play → Add-On (single player levels only) |
| Remove Mission Pack from Game | Lists the installed packs and removes the one you pick |
| Open Built-in Mission | Opens the game's own story (`S_*`), challenge (`C_*`) and multiplayer (`M_*`) missions |
| Rebuild Built-in Missions | Rebuilds the game data from `Editor\AuthorKit` so your changes to built-in missions take effect (a few minutes the first time) |
| Restore Original Missions | Puts the shipped missions back |

**Bigger maps.** The original missions are at most 64 × 64 tiles. This
version of the game supports maps up to 120 × 120 (about 3.5 times the area of 64 × 64):
open a level and use **Settings → Map Size** to set the size of the
playable area. The minimap switches to one pixel per tile on maps bigger
than 64 tiles so it stays the familiar size.

**Crimson side** (branch `feature/crimson-side`). Besides sides 1–4
there is a fifth side, Crimson (#DC143C). Pick "Crimson" in the editor's
unit panel to place its units, set it up under **Settings → Side 5
(Crimson)**, and choose it in triggers like any other side.

**Allies share vision** (branch `feature/crimson-side`). Your allies' units
and buildings lift the fog of war for you, as yours do (the original game
only counted your own).

**New unit group actions.** Computer unit groups can now **Protect Group**
(stay with a unit of another group, follow it and fight enemies near it,
then protect the group's next unit when it dies), **Protect In Area** (the
same for the nearest own or allied unit or building of the chosen types in
an area, e.g. a base building) and **Attack In Area** (attack the chosen
types and sides inside an area until none are left). Levels that use them
need this version of the game.

In every unit group action with a time (Attack, Guard, Guard Vicinity and the
three above), **0 seconds means no time limit**: the group keeps doing it
and never moves on to its next action (before, 0 ended Attack and Guard at
once). Wait is the exception: Wait 0 seconds still means don't wait.

You can also drag a `.ld` level file onto `M.exe` to open it. Levels you only
try out with Play Level in Game don't appear in the Add-On list; only
installed packs do.

The editor checks a level before exporting it. Many of the game's own missions
have "placement" warnings (a building on terrain the editor considers blocked)
and a few have small errors, yet play fine; the editor lists them and lets you
continue. See `Editor\README.txt` for more.

## Building

The Windows version is cross-compiled with MinGW-w64. On Ubuntu/Debian, or in
WSL on Windows 11:

```sh
sudo apt install mingw-w64 make curl python3 zip mono-devel p7zip-full
cd game/sdl/win
./build.sh
```

The script downloads SDL2 and fetches `make`/`sh` from
w64devkit the first time, compiles the game, the editor and the data tools,
assembles the Author Kit, and writes `game/sdl/win/dist/HostileTakeover-Windows.zip`.
Set `NO_EDITOR=1` to build only the game.

See [`game/sdl/win/BUILDING.md`](game/sdl/win/BUILDING.md) for build options
and [`PORTING_NOTES.md`](PORTING_NOTES.md) for how the port was done.

## Other platforms

On branch `feature/crimson-side` this repository builds for Windows only. The
iOS, Android, macOS, Linux and Native Client builds, the old Palm OS and
Windows Mobile code, the multiplayer server
(`server/`, `stats/`, `syncerror/`) and the game's online code have been
removed. The original upstream instructions are kept, for reference, in
[`README-upstream.md`](README-upstream.md); the source of the other ports is
still on `main` and in the upstream repository.

## License

Hostile Takeover is copyright © 2003–2014 Spiffcode, Inc. and is released under
the BSD license; see [`LICENSE.txt`](LICENSE.txt). The Windows port changes are
offered under the same license. *Hostile Takeover* and *Warfare Incorporated*
are trademarks of Spiffcode, Inc.
