# Hostile Takeover for Windows 11

A native Windows port of **Hostile Takeover**, the open source release of the
mobile real-time strategy game *Warfare Incorporated* by Spiffcode, Inc.

The original source ([spiffcode/hostile-takeover](https://github.com/spiffcode/hostile-takeover))
builds for iOS, Android, macOS and (partly) Linux. This fork adds a Windows
10/11 version built from the game's SDL2 port, aimed at single-player play
with a mouse and keyboard.

> This is an unofficial fan port, not a Spiffcode release.

## Playing

1. Download `HostileTakeover-Windows.zip` (from the Releases page, or build it
   yourself as described below) and unzip it anywhere.
2. Double-click `HostileTakeover.exe`. Nothing is installed and no extra DLLs
   are needed.
3. If Windows shows **"Windows protected your PC"**, click **More info → Run
   anyway**. This happens for any program that isn't code-signed.
4. Choose **Play → Single Player → M0: Introduction** to start the story.

### Controls

| Input | Action |
| --- | --- |
| Left click | Select a unit or building, press buttons |
| Left click on the ground | Move the selected units there |
| Left drag | Select several units with a box |
| Right click | Open a unit's or building's menu (the phone version's "tap and hold"; holding the left button for half a second also works) |
| Arrow keys | Scroll the map |
| Click the minimap | Jump to that part of the map |
| M | In-game menu (save, load, options) |
| F11 or Alt+Enter | Toggle fullscreen |

The window can be resized or maximized; the 800×600 picture scales to fit and
keeps its shape.

### Saves

Saved games, progress, settings and a `log.txt` are stored in
`%APPDATA%\HostileTakeover`. To keep everything next to the game instead
(for example on a USB stick), create an empty file called `portable.txt`
beside `HostileTakeover.exe`.

### Known limitations

- Single player is the focus. Multiplayer, the leaderboard and add-on mission
  downloads depend on the original online servers and may not work.
- Arrow keys are the only keyboard scrolling; there is no edge-of-screen
  mouse scrolling.

## Building

The Windows version is cross-compiled with MinGW-w64. On Ubuntu/Debian, or in
WSL on Windows 11:

```sh
sudo apt install mingw-w64 cmake make curl python3 zip
cd game/sdl/win
./build.sh
```

The script downloads SDL2 and builds a static libcurl the first time, compiles
the game, and writes `game/sdl/win/dist/HostileTakeover-Windows.zip`.

See [`game/sdl/win/BUILDING.md`](game/sdl/win/BUILDING.md) for build options
and [`PORTING_NOTES.md`](PORTING_NOTES.md) for how the port was done.

## Other platforms

The original build instructions for iOS, Android, macOS, Linux, the data file
and the multiplayer server are in [`README-upstream.md`](README-upstream.md).

## License

Hostile Takeover is copyright © 2003–2014 Spiffcode, Inc. and is released under
the BSD license; see [`LICENSE.txt`](LICENSE.txt). The Windows port changes are
offered under the same license. *Hostile Takeover* and *Warfare Incorporated*
are trademarks of Spiffcode, Inc.
