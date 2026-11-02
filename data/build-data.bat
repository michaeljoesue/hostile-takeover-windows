@echo off
rem Rebuilds the game data file (all built-in missions, art, sounds and text)
rem on Windows. The result is ..\game\htdata832.pdb.
rem
rem Needs the Windows toolset in ..\tools (make, sh, the .NET data tools,
rem ppini and inicrunch), which ships in the Windows editor package or is
rem built by game\sdl\win\build.sh.

setlocal
set "HERE=%~dp0"
set "TOOLS=%HERE%..\tools"
if not exist "%TOOLS%\make.exe" (
    echo Can't find the build tools in "%TOOLS%".
    exit /b 1
)
set "PATH=%TOOLS%;%PATH%"
cd /d "%HERE%"

make IPHONE=1 MULTIPLAYER=1 TOOLDIR=../tools all
if errorlevel 1 (
    echo.
    echo Building the game data failed.
    exit /b 1
)

rem Release builds of the game only load data stamped with their version.
"%TOOLS%\markversion.exe" 1 ..\game\htdata832.pdb "+++VERSION+++" 1.6
if errorlevel 1 exit /b 1

echo.
echo Game data built: %HERE%..\game\htdata832.pdb
