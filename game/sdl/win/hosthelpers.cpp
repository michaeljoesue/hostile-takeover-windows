// Windows host helpers for the SDL port of Hostile Takeover.
//
// Layout on disk:
//   <folder containing HostileTakeover.exe>\htdata832.pdb, htsfx.pdb   (game data)
//   %APPDATA%\HostileTakeover\                                         (saves, prefs, log)

#include "game/sdl/hosthelpers.h"
#include "base/thread.h"
#include "base/wincompat.h"
#include <shellapi.h>
#include <direct.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <SDL.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

namespace wi {

char gszHomeDir[PATH_MAX];              // %APPDATA%\HostileTakeover
char gszMainDataDir[PATH_MAX];          // data files directory (next to the exe)
char gszTempDir[PATH_MAX];              // temp file directory
char gszMissionPacksDir[PATH_MAX];      // installed mission packs
char gszMissionPackInfosDir[PATH_MAX];
char gszSaveGamesDir[PATH_MAX];         // saved games
char gszPrefsFilename[PATH_MAX];        // game prefs
char gszCompletesDir[PATH_MAX];         // for "mission completed" tracking
char gszLogFilename[PATH_MAX];

static void StripTrailingSlash(char *psz) {
    int cch = (int)strlen(psz);
    while (cch > 0 && (psz[cch - 1] == '\\' || psz[cch - 1] == '/')) {
        psz[--cch] = 0;
    }
}

static bool FileExists(const char *pszDir, const char *pszFn) {
    char sz[PATH_MAX];
    snprintf(sz, sizeof(sz), "%s/%s", pszDir, pszFn);
    FILE *pf = fopen(sz, "rb");
    if (pf == NULL) {
        return false;
    }
    fclose(pf);
    return true;
}

bool HostHelpers::Init() {
    // Game data lives next to the executable. If it isn't there, fall back
    // to the current directory (handy when running from the build folder).
    char *pszBase = SDL_GetBasePath();
    if (pszBase != NULL) {
        strncpy(gszMainDataDir, pszBase, sizeof(gszMainDataDir) - 1);
        SDL_free(pszBase);
    } else {
        strcpy(gszMainDataDir, ".");
    }
    StripTrailingSlash(gszMainDataDir);
    if (!FileExists(gszMainDataDir, "htdata832.pdb") &&
            FileExists(".", "htdata832.pdb")) {
        strcpy(gszMainDataDir, ".");
    }

    // Per-user writable data goes in %APPDATA%\HostileTakeover. If the
    // folder next to the exe contains a file named "portable.txt", keep
    // everything next to the exe instead (handy for USB sticks).
    if (FileExists(gszMainDataDir, "portable.txt")) {
        snprintf(gszHomeDir, sizeof(gszHomeDir), "%s/UserData", gszMainDataDir);
    } else {
        const char *pszAppData = getenv("APPDATA");
        if (pszAppData != NULL && *pszAppData != 0) {
            snprintf(gszHomeDir, sizeof(gszHomeDir), "%s/HostileTakeover", pszAppData);
        } else {
            snprintf(gszHomeDir, sizeof(gszHomeDir), "%s/UserData", gszMainDataDir);
        }
    }

    snprintf(gszTempDir, sizeof(gszTempDir), "%s/tmp", gszHomeDir);
    snprintf(gszMissionPacksDir, sizeof(gszMissionPacksDir), "%s/MissionPacks", gszHomeDir);
    snprintf(gszMissionPackInfosDir, sizeof(gszMissionPackInfosDir), "%s/MissionPackInfos", gszHomeDir);
    snprintf(gszSaveGamesDir, sizeof(gszSaveGamesDir), "%s/SaveGames", gszHomeDir);
    snprintf(gszCompletesDir, sizeof(gszCompletesDir), "%s/Completes/", gszHomeDir);
    snprintf(gszPrefsFilename, sizeof(gszPrefsFilename), "%s/prefs.bin", gszHomeDir);
    snprintf(gszLogFilename, sizeof(gszLogFilename), "%s/log.txt", gszHomeDir);

    // Make the directories
    _mkdir(gszHomeDir);
    _mkdir(gszMissionPacksDir);
    _mkdir(gszMissionPackInfosDir);
    _mkdir(gszSaveGamesDir);
    _mkdir(gszCompletesDir);
    _mkdir(gszTempDir);

    // Start a fresh log each run
    FILE *pf = fopen(gszLogFilename, "w");
    if (pf != NULL) {
        fclose(pf);
    }

    base::WinsockInit();

    return true;
}

void HostHelpers::Cleanup() {
}

const char *HostHelpers::GetMainDataDir() {
    return gszMainDataDir;
}

const char *HostHelpers::GetTempDir() {
    return gszTempDir;
}

const char *HostHelpers::GetMissionPacksDir() {
    return gszMissionPacksDir;
}

const char *HostHelpers::GetMissionPackInfosDir() {
    return gszMissionPackInfosDir;
}

const char *HostHelpers::GetSaveGamesDir() {
    return gszSaveGamesDir;
}

const char *HostHelpers::GetCompletesDir() {
    return gszCompletesDir;
}

const char *HostHelpers::GetPrefsFilename() {
    return gszPrefsFilename;
}

void HostHelpers::Log(const char *pszFormat, va_list va)
{
    char sz[1024];
    vsnprintf(sz, sizeof(sz), pszFormat, va);

    OutputDebugStringA(sz);
    OutputDebugStringA("\n");
#ifdef DEBUG
    printf("%s\n", sz);
    fflush(stdout);
#endif
    if (gszLogFilename[0] != 0) {
        FILE *pf = fopen(gszLogFilename, "a");
        if (pf != NULL) {
            fprintf(pf, "%s\n", sz);
            fclose(pf);
        }
    }
}

void HostHelpers::Log(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    Log(pszFormat, va);
    va_end(va);
}

void HostHelpers::MessageBox(const char *pszFormat, va_list va)
{
    char sz[1024];
    va_list vaCopy;
    va_copy(vaCopy, va);
    vsnprintf(sz, sizeof(sz), pszFormat, vaCopy);
    va_end(vaCopy);
    Log("%s", sz);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Hostile Takeover",
            sz, NULL);
}

void HostHelpers::Break()
{
    Log("BREAK!!");
}

// The game picture's size is picked once from the monitor and fixed (no
// resizing, no fullscreen toggle): the game always fills the whole screen
// and each game pixel is drawn as an exact n x n block of screen pixels, so
// the picture is sharp (no blurry stretching). n is the largest whole number
// that still leaves the game at least kcyGameMin pixels tall (about the old
// 800x600), e.g.
//   1920x1080 -> 960x540 x2    2560x1440 -> 1280x720 x2
//   3840x2160 -> 960x540 x4    1366x768  -> 1366x768 x1
// The process is per-monitor DPI aware so Windows display scaling (125%,
// 150%...) doesn't stretch (blur) the window either.

#define kcxGameMin 720
#define kcyGameMin 540

static int s_cxGame, s_cyGame, s_nGameScale;

static void CalcGameSize()
{
    if (s_cxGame != 0)
        return;
    s_cxGame = 800;
    s_cyGame = 600;
    s_nGameScale = 1;

    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    if (!SDL_WasInit(SDL_INIT_VIDEO) && SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
        return;
    SDL_DisplayMode mode;
    if (SDL_GetDesktopDisplayMode(0, &mode) != 0 || mode.w <= 0 || mode.h <= 0)
        return;
    int n = 1;
    while (mode.w / (n + 1) >= kcxGameMin && mode.h / (n + 1) >= kcyGameMin)
        n++;
    // Game widths are kept even (some blits work in pairs of pixels)
    s_cxGame = (mode.w / n) & ~1;
    s_cyGame = (mode.h / n) & ~1;
    s_nGameScale = n;
}

int HostGetGameScale()
{
    CalcGameSize();
    return s_nGameScale;
}

void HostHelpers::GetSurfaceProperties(SurfaceProperties *pprops)
{
    CalcGameSize();
    pprops->cxWidth = s_cxGame;
    pprops->cyHeight = s_cyGame;
    pprops->cbxPitch = 1;
    pprops->cbyPitch = pprops->cxWidth;
    pprops->density = 1.0f;
    pprops->ffFormat = wi::kfDirect8;
}

bool HostHelpers::IsExiting()
{
    return false;
}

} // namespace wi
