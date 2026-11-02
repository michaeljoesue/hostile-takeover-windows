// Windows host helpers for the SDL port of Hostile Takeover.
//
// Layout on disk:
//   <folder containing HostileTakeover.exe>\htdata832.pdb, htsfx.pdb   (game data)
//   %APPDATA%\HostileTakeover\                                         (saves, prefs, log)

#include "game/sdl/win/winchatcontroller.h"
#include "game/sdl/hosthelpers.h"
#include "game/sdl/sdlhttpservice.h"
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
char gszMissionPacksDir[PATH_MAX];      // downloaded mission packs
char gszMissionPackInfosDir[PATH_MAX];
char gszSaveGamesDir[PATH_MAX];         // saved games
char gszPrefsFilename[PATH_MAX];        // game prefs
char gszCompletesDir[PATH_MAX];         // for "mission completed" tracking
char gszLogFilename[PATH_MAX];
HttpService *gphttp;
IChatController *gpchat;

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

    extern HttpService *gphttp;
    gphttp = (HttpService *)new SdlHttpService();

    gpchat = NULL;

    return true;
}

void HostHelpers::Cleanup() {
    delete gphttp;
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

void HostHelpers::OpenUrl(const char *pszUrl) {
    SDL_OpenURL(pszUrl);
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

void HostHelpers::GetSurfaceProperties(SurfaceProperties *pprops)
{
    pprops->cxWidth = 800;
    pprops->cyHeight = 600;
    pprops->cbxPitch = 1;
    pprops->cbyPitch = pprops->cxWidth;
    pprops->density = 1.0f;
    pprops->ffFormat = wi::kfDirect8;
}

bool HostHelpers::IsExiting()
{
    return false;
}

const char *HostHelpers::GetUdid()
{
    // A stable per-machine id derived from the computer name and the
    // system volume serial number.
    static char gszUdid[24];
    if (gszUdid[0] == 0) {
        DWORD serial = 0;
        GetVolumeInformationA("C:\\", NULL, 0, &serial, NULL, NULL, NULL, 0);
        char szName[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD cch = sizeof(szName);
        if (!GetComputerNameA(szName, &cch)) {
            szName[0] = 0;
        }
        dword hash = 2166136261u;
        for (const char *pch = szName; *pch != 0; pch++) {
            hash = (hash ^ (byte)*pch) * 16777619u;
        }
        snprintf(gszUdid, sizeof(gszUdid), "%08x%08x", (unsigned)hash,
                (unsigned)serial);
    }
    return gszUdid;
}

void HostHelpers::InitiateAsk(const char *title, int max, const char *def,
        int keyboard, bool secure)
{
    // Only used by online multiplayer / custom mission pack URLs.
    Log("HostHelpers::InitiateAsk not implemented");
}

void HostHelpers::GetAskString(char *psz, int cb)
{
    if (cb > 0) {
        *psz = 0;
    }
}

IChatController *HostHelpers::GetChatController()
{
    if (gpchat == NULL) {
        gpchat = new wi::WinChatController();
    }
    return gpchat;
}

void HostHelpers::InitiateWebView(const char *title, const char *url) {
    SDL_OpenURL(url);
}

const char *HostHelpers::GetPlatformString() {
    return "Windows";
}

} // namespace wi
