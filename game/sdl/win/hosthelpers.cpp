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

// The game runs in a fixed-size window (it can't be resized or maximized,
// and there's no fullscreen) whose size is picked once for the monitor so
// the picture is as large and as sharp as possible: the window fills the
// screen's work area (the desktop minus the taskbar and the window's own
// title bar and borders), and each game pixel is drawn as an exact n x n
// block of screen pixels (no blurry stretching). n is the largest whole
// number that still leaves the game at least kcxGameMin x kcyGameMin, e.g.
// (100% display scaling, taskbar at the bottom)
//   1920x1080 -> about 952x496 shown at 2x
//   2560x1440 -> about 1272x676 at 2x
//   3840x2160 -> about 952x516 at 4x
//   1366x768  -> about 1350x690 at 1x
// The process is per-monitor DPI aware so Windows display scaling (125%,
// 150%...) doesn't stretch (blur) the window either.

#define kcxGameMin 640
#define kcyGameMin 480

static int s_cxGame, s_cyGame, s_nGameScale;
static int s_xWindow = SDL_WINDOWPOS_CENTERED, s_yWindow = SDL_WINDOWPOS_CENTERED;

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

    // Room on screen: the work area (no taskbar)

    SDL_Rect rcUsable;
    if (SDL_GetDisplayUsableBounds(0, &rcUsable) != 0 || rcUsable.w <= 0 ||
            rcUsable.h <= 0) {
        SDL_DisplayMode mode;
        if (SDL_GetDesktopDisplayMode(0, &mode) != 0)
            return;
        rcUsable.w = mode.w;
        rcUsable.h = mode.h - 48;
    }

    // Minus the window's title bar and borders, measured on a hidden window
    // of the same kind

    int cyTop = 0, cxLeft = 0, cyBottom = 0, cxRight = 0;
    SDL_Window *pwndProbe = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED, 320, 240, SDL_WINDOW_HIDDEN);
    if (pwndProbe != NULL) {
        if (SDL_GetWindowBordersSize(pwndProbe, &cyTop, &cxLeft, &cyBottom,
                &cxRight) != 0) {
            cyTop = cxLeft = cyBottom = cxRight = 0;
        }
        SDL_DestroyWindow(pwndProbe);
    }
    if (cyTop <= 0) {
        // Couldn't measure: assume a title bar of about 32 points
        float ddpi = 96.0f;
        if (SDL_GetDisplayDPI(0, &ddpi, NULL, NULL) != 0 || ddpi <= 0)
            ddpi = 96.0f;
        cyTop = (int)(32 * ddpi / 96.0f + 0.5f);
        cxLeft = cxRight = cyBottom = (int)(4 * ddpi / 96.0f + 0.5f);
    }
    int cxRoom = rcUsable.w - cxLeft - cxRight;
    int cyRoom = rcUsable.h - cyTop - cyBottom;
    if (cxRoom < kcxGameMin || cyRoom < kcyGameMin) {
        // Tiny screen: just use what there is
        s_cxGame = (cxRoom > 320 ? cxRoom : 320) & ~1;
        s_cyGame = (cyRoom > 240 ? cyRoom : 240) & ~1;
        return;
    }

    int n = 1;
    while (cxRoom / (n + 1) >= kcxGameMin && cyRoom / (n + 1) >= kcyGameMin)
        n++;
    // Game sizes are kept even (some blits work in pairs of pixels)
    s_cxGame = (cxRoom / n) & ~1;
    s_cyGame = (cyRoom / n) & ~1;
    s_nGameScale = n;

    // Window client area centered in the room left for it
    s_xWindow = rcUsable.x + cxLeft + (cxRoom - s_cxGame * n) / 2;
    s_yWindow = rcUsable.y + cyTop + (cyRoom - s_cyGame * n) / 2;
}

void HostGetGameWindowPos(int *px, int *py)
{
    CalcGameSize();
    *px = s_xWindow;
    *py = s_yWindow;
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
