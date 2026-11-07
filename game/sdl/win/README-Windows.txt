HOSTILE TAKEOVER (Warfare Incorporated) - Windows 11 edition
=============================================================

An unofficial Windows build of the open source Hostile Takeover
(https://github.com/spiffcode/hostile-takeover), made from the game's
SDL port. Single player only.


HOW TO PLAY
-----------
1. Unzip the whole "HostileTakeover" folder somewhere, e.g. your Desktop
   or Documents. Keep all the files together in the same folder.
2. Double-click HostileTakeover.exe.

   The first time, Windows may show "Windows protected your PC"
   (SmartScreen). This appears for any program that isn't signed by a
   company. Click "More info", then "Run anyway".

3. Choose PLAY > M0: Introduction to start the story.


CONTROLS
--------
  Left click            Select a unit / building, press buttons
  Left drag             Draw a box to select several units
  Left click on ground  Deselect everything
  Right click on ground Move the selected units there
  Right click on enemy  Attack it with the selected units
  Right click on your own unit / building
                        Open its menu (same as "tap and hold" on the
                        phone version; holding the left button for half
                        a second also works)
  Ctrl + right click on anything
                        Force attack: the selected units attack it even
                        if it's your own unit or building
  Right click in a build menu
                        Cancel one queued unit (click the unit's icon
                        or the Order button). It never orders a new one.
  Arrow keys            Scroll the map
  Click the minimap     Jump to that part of the map (bottom right)
  M                     Open the in-game menu (save / load / options)
  F11  or  Alt+Enter    Toggle fullscreen

The in-game tutorial was written for phones: when it says "tap on grass
to move", right click instead.

The window can be resized or maximized; the picture scales to fit and
keeps its shape.

Your allies' units and buildings lift the fog of war for you too, so you
can see your allies' bases and armies.

Sound is in stereo: fighting to the left of the screen comes from the
left speaker (or headphone), fighting to the right from the right, and
battles off screen sound quieter the farther away they are. Fights
straight above or below the screen sound centered. To change the volume,
use the Volume slider in Options (main menu, or M in a game). 0% is
silent; the setting is remembered.


MAKING YOUR OWN MISSIONS
------------------------
The Editor folder contains M, the map & mission editor the game's missions
were made with. Start Editor\M.exe. Its Game menu can start the game
straight into the level you're editing (F5), install your levels as an
add-on mission pack, and rebuild the game's built-in missions after you
change them. Maps can be up to 120 x 120 tiles (the game's own are at
most 64 x 64); see Settings > Map Size in the editor. See Editor\README.txt.


WHERE YOUR SAVES ARE
--------------------
Saved games, progress and settings are stored in:

    %APPDATA%\HostileTakeover

(paste that into the File Explorer address bar to open it).
A log file, log.txt, is written there too, which helps if something
goes wrong.

Want everything to stay inside the game folder instead (e.g. to play
from a USB stick)? Create an empty file called  portable.txt  next to
HostileTakeover.exe; saves will then go in a "UserData" subfolder.


NOTES
-----
- This is a 32-bit program on purpose: the game's code dates from 32-bit
  handhelds and is most reliable that way. 32-bit programs run normally
  on Windows 11 (including ARM PCs).
- No installation, no extra DLLs, nothing written to the registry.
  To uninstall, delete the folder (and %APPDATA%\HostileTakeover if you
  also want to remove your saves and installed mission packs).
- Single player only. The online features (multiplayer, chat,
  leaderboard, forums link, downloading add-on mission packs) have been
  removed because the original game servers no longer exist. Install
  add-on mission packs with the editor in the Editor folder (Game >
  Install Mission Pack in Game); they appear under PLAY > Add-On.


LICENSE
-------
Hostile Takeover is copyright 2004-2014 Spiffcode, Inc. and released
under the BSD license (see LICENSE.txt). "Hostile Takeover" and
"Warfare Incorporated" are trademarks of Spiffcode, Inc. This build is
not an official Spiffcode release.
