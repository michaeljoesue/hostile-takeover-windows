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

3. Choose PLAY > SINGLE PLAYER > M0: Introduction to start the story.


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
  Arrow keys            Scroll the map
  Click the minimap     Jump to that part of the map (bottom right)
  M                     Open the in-game menu (save / load / options)
  F11  or  Alt+Enter    Toggle fullscreen

The in-game tutorial was written for phones: when it says "tap on grass
to move", right click instead.

The window can be resized or maximized; the picture scales to fit and
keeps its shape.


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
  also want to remove your saves).
- Online features (multiplayer, leaderboard, downloading add-on mission
  packs) depend on the original game servers and may not work.


LICENSE
-------
Hostile Takeover is copyright 2004-2014 Spiffcode, Inc. and released
under the BSD license (see LICENSE.txt). "Hostile Takeover" and
"Warfare Incorporated" are trademarks of Spiffcode, Inc. This build is
not an official Spiffcode release.
