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

  Install Mission Pack in Game...
      Puts all the levels you have open into a mission pack with the title
      and author you choose. In the game it appears under Play > Single
      Player (or Multiplayer) > Add-On as "<title> by <author>". Installing
      again with the same title replaces it.

  Open Built-in Mission...
      Opens the game's own missions: story (S_*), challenge (C_*) and
      multiplayer (M_*). They live in AuthorKit\data.

  Rebuild Built-in Missions
      After changing built-in missions, this rebuilds the game's data so the
      game uses your versions. The first build takes a few minutes. The
      original data is kept as htdata832.original.pdb.

  Restore Original Missions
      Puts the original built-in missions back.

  Game Location...
      Only needed if the Editor folder is moved away from the game.


CHECKS BEFORE PLAYING
---------------------
Before a level goes into the game, M checks it and lists anything odd in its
Output window:

  Placement warnings   Something stands on terrain M thinks is blocked.
                       Most of the game's own missions have some of these
                       and play fine, so you can continue.
  Errors               For example scenery partly off the map or too many
                       buildings for one side. These can make a level
                       misbehave, so fix them if you can. (A few built-in
                       missions have one and still work, so M lets you
                       continue anyway.)


GETTING STARTED
---------------
1. Start M.exe and use File > New to make a level, or Game > Open Built-in
   Mission to look at how the real missions are made.
2. Pick a terrain, paint tiles and drop units and buildings from the panels
   on the left. Select something to change its properties on the right.
3. Missions are driven by triggers (conditions and actions), edited with
   Settings > Triggers. Unit groups, switches, counters and mission text
   are in the Settings menu too. The built-in missions are the best
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
