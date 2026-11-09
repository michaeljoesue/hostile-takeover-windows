#ifndef __HOSTHELPERS_H__
#define __HOSTHELPERS_H__

#include <stdarg.h>
#include "game/ht.h"


namespace wi {

// Whole number the game picture is scaled up by on screen (Windows)
int HostGetGameScale();
// Where to put the game window's client area (Windows)
void HostGetGameWindowPos(int *px, int *py);

// Display sizes to choose from (Windows). The first is the automatic best
// fit; the others are fixed game resolutions that fit on the screen.
struct GameMode {
    int cx, cy;             // game resolution
    int nScale;             // shown at this whole-number scale
    int xWindow, yWindow;   // window client area position
    bool fAuto;
};
#define kcGameModesMax 16
int HostGetGameModes(GameMode *agm, int cgmMax);
void HostSetGameMode(const GameMode *pgm);

struct SurfaceProperties {
    int cxWidth;
    int cyHeight;
    int cbxPitch;
    int cbyPitch;
    float density;
    unsigned short ffFormat;
};

const unsigned short kfDirect565 = 0x0001;
const unsigned short kfDirect888 = 0x0002;
const unsigned short kfDirect8 = 0x0004;

class FormMgr;

class HostHelpers
{
public:
    static void Log(const char *pszFormat, ...);
    static void Log(const char *pszFormat, va_list va);
    static void MessageBox(const char *pszFormat, va_list va);
    static void Break();
    static void GetSurfaceProperties(SurfaceProperties *pprops);
    static void FrameStart();
    static void FrameComplete(int cfrmm, UpdateMap **apupd, Rect *arc,
            bool fScrolled);
    static void ResetScrollOffset();
    static void SetFormMgrs(FormMgr *pfrmmSimUI, FormMgr *pfrmmInput);
    static DibBitmap *CreateFrontDib(int cx, int cy, int nDegreeOrientation);
    static void SetPalette(Palette *ppal);
    static bool IsExiting();
    static void GameThreadStart(void *pv);
    static void DisplayInitComplete();

    // TODO(darrinm): unused?
    static int main(int argc, char **argv);

    static bool Init();
    static void Cleanup();
    static const char *GetMainDataDir();
    static const char *GetMissionPacksDir();
    static const char *GetMissionPackInfosDir();
    static const char *GetTempDir();
    static const char *GetCompletesDir();
    static const char *GetSaveGamesDir();
    static const char *GetPrefsFilename();

};

} // namespace wi

#endif // __HOSTHELPERS_H__
