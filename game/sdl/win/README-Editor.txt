M - THE HOSTILE TAKEOVER MAP & MISSION EDITOR
============================================

M is the editor the game's own missions were made with. Use it to make new
maps and missions, or to change the built-in story and challenge missions.

Start it by double-clicking M.exe in this folder, or drag a level (.ld file)
onto M.exe to open it. It needs the .NET Framework 4, which Windows 10 and 11
already include.


THE GAME MENU
-------------
M has a Game menu that connects it to Hostile Takeover:

  Play Level in Game (F5)
      Starts the game straight into the level you're editing, so you can try
      it out. Single player levels only. Close the game before using it again.
      The level doesn't appear in the game's Add-On list; use Install Mission
      Pack in Game for that.

  Install Mission Pack in Game...
      Puts all the levels you have open into a mission pack with the title
      and author you choose. In the game it appears under Play > Add-On as
      "<title> by <author>". Installing again with the same title replaces
      it. The game only plays single player levels; multiplayer levels in a
      pack are kept but not listed.

  Remove Mission Pack from Game...
      Lists the mission packs installed in the game and removes the one you
      pick. Your level files are not affected.

  Open Built-in Mission...
      Opens the game's own missions: story (S_*), challenge (C_*) and
      multiplayer (M_*). They live in AuthorKit\data. The multiplayer maps
      can be opened and edited, but the game doesn't play them.

  Rebuild Built-in Missions
      After changing built-in missions, this rebuilds the game's data so the
      game uses your versions. The first build takes a few minutes. The
      original data is kept as htdata832.original.pdb.

  Restore Original Missions
      Puts the original built-in missions back.

  Game Location...
      Only needed if the Editor folder is moved away from the game.


BIGGER MAPS
-----------
The game's own missions are at most 64 x 64 tiles. This version of the game
supports maps up to 120 x 120. Open a level and use Settings > Map Size to set
the size of the playable area (the green rectangle). The top left corner stays
put; growing a map adds empty space to the right and bottom, which you can
then paint. You can also type the size into the level's Bounds property.

On maps bigger than 64 tiles the in-game minimap shows one pixel per tile,
so it stays about the size players are used to.


THE CRIMSON SIDE
----------------
This version has a fifth side, Crimson, as well as sides 1-4. Choose
"Crimson" in the side list above the unit pictures to place its units and
buildings, use Settings > Side 5 (Crimson) to set its starting credits,
view and whether a person or the computer plays it, and tick "Crimson" in
a trigger's sides or side arguments like any other side.

Saved games from before Crimson was added can't be loaded.


CHECKS BEFORE PLAYING
---------------------
Before a level goes into the game, M checks it and lists anything odd in its
Output window:

  Placement warnings   Something stands on terrain M thinks is blocked.
                       Most of the game's own missions have some of these
                       and play fine, so you can continue.
  Errors               For example scenery partly off the map, more than
                       64 areas, more than 64 switches, more than 512
                       triggers for one side or more than 5000 units,
                       buildings and scenery in total. These can
                       make a level misbehave, so fix them if you can. (A
                       few built-in missions have one and still work, so M
                       lets you continue anyway.)

There's no separate limit on scenery or on each side's starting buildings
and units any more: the game raises its build limits to fit what a level
starts with (and leaves room to build more).


GETTING STARTED
---------------
1. Start M.exe and use File > New to make a level, or Game > Open Built-in
   Mission to look at how the real missions are made.
2. Pick a terrain, paint tiles and drop units and buildings from the panels
   on the left. Select something to change its properties on the right.
   Settings > Level shows the level's own properties, including
   GalaxiteRegrowth: Off (the default, like the original missions), Slow
   (mined Galaxite grows back one size every 60 seconds) or Normal (every
   30 seconds). It only grows back to the amount the level starts with.
3. Missions are driven by triggers (conditions and actions), edited with
   Settings > Triggers. Unit groups (Ctrl+G; rename a group with the Rename
   button, F2 or a double-click), switches, counters and mission text
   are in the Settings menu too. Rename an area by selecting it and
   editing Name on the right; triggers and unit groups that use it are
   updated, and copies of an area get their own name. The built-in
   missions are the best
   examples.
4. Press F5 to play it.

A guide to mission authoring written by the original developers:
http://sites.warfareincorporated.com/mission-authoring/
(It may only be available through web.archive.org.)


FILES
-----
  M.exe, m.dll         the editor
  AuthorKit\data       sources of the built-in missions (*.ld are levels),
                       their art and text, and build-data.bat
  AuthorKit\tools      the tools build-data.bat uses
  AuthorKit\game       headers the build reads; the rebuilt data goes here

The editor was written for an older version of Windows and some parts of it
look dated. Save your work often.
