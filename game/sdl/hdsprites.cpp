// HD sprites: 32-bit RGBA pictures drawn over the 8-bit game image.
//
// The game still draws everything into its 8-bit frame. When an animation
// frame has an HD picture (data/hd/manifest.txt), AnimationData::DrawFrame
// calls HdDrawFrame instead of drawing the 8-bit sprite: nothing is drawn,
// the picture is recorded together with a snapshot of the 8-bit pixels under
// it. After the frame is finished, the display scales the 8-bit image up and
// draws the recorded pictures on top, in the order the game drew them, with
// their soft shadows. A pixel the game drew over later (a unit in front, a
// selection bracket, a dialog) no longer matches the snapshot, so the HD
// picture is left out there and whatever was drawn on top stays on top.
//
// F9 switches between HD and the classic sprites.

#include "../ht.h"
#include "game/sdl/hosthelpers.h"
#include "game/sdl/hdsprites.h"
#include <limits.h>
#include <math.h>
#include <map>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO_FAILURE_STRINGS
#include "game/sdl/stb_image.h"

namespace wi {

// One picture file (colour + optional shadow and team-colour mask), loaded
// on first use. Textures are made per side (team colour) on first use.

struct HdPic {
    std::string strPath;
    bool fLoaded;
    bool fOk;
    int cx, cy;
    byte *pbRgba;           // straight alpha
    byte *pbTeam;           // 0..255 or NULL
    byte *pbShadow;         // 0..255 or NULL
    SDL_Texture *aptex[kcSides + 1];    // per side, [kcSides] = white flash
    SDL_Texture *ptexShadow;
};

struct HdEntry {
    std::string strPic;     // may hold {S} or {T36}
    int dx, dy;             // top-left relative to the draw point, HD pixels
};

// A timed path (Galaxite smoke puffs): n pictures played over game updates
// u0..u1 of the classic animation, each with its own position, so all the
// pictures play smoothly between game updates instead of one per update.
// Manifest: "track<TAB>name<TAB>u0<TAB>u1<TAB>picture pattern (%02d)<TAB>n"
// and "trackpt<TAB>name<TAB>i<TAB>-<TAB>dx<TAB>dy"; a frame entry whose
// picture is "@name" draws the track.

struct HdTrack {
    int u0, u1;
    std::string strPattern;
    int cPics;
    std::vector<int> adx, ady;
};
static std::map<std::string, HdTrack> s_mapTracks;

// HD special points ("spec<TAB>anir<TAB>strip<TAB>frame<TAB>x<TAB>y"): where
// the HD picture's gun muzzle is (towers), used for where shots start. They
// apply whenever HD pictures are installed, whether shown or not (F9), so
// the game plays the same with either.

static std::map<std::string, std::pair<int, int> > s_mapSpec;

struct HdAnir {
    std::string strName;
    std::map<std::string, std::vector<HdEntry> > mapFrames;    // "strip|frame"
};

// A recorded picture, in HD pixels relative to the game image origin

struct HdItem {
    HdPic *ppic;
    int side;               // 0..kcSides-1, kcSides = flash white
    int xHd, yHd;           // top-left
};

struct HdCommand {
    std::vector<HdItem> aitm;
    Rect rc;                // game pixels, clipped to the bitmap
    std::vector<byte> abSnap;
};

static bool s_fInited;
static bool s_fEnabled = true;
static bool s_fHaveData;
static std::vector<HdAnir> s_aanir;
static std::map<std::string, HdPic *> s_mapPics;
static std::vector<HdCommand> s_acmd;

static int HdPixelsPerGamePixel() {
    // HD pictures have 4 pixels per pixel of the 24-pixel-tile art
    int cxTile = gcxTile > 0 ? gcxTile : 24;
    return 96 / cxTile;
}

static void HdInit() {
    if (s_fInited)
        return;
    s_fInited = true;

    char szPath[PATH_MAX];
    snprintf(szPath, sizeof(szPath), "%s/hd/manifest.txt", HostHelpers::GetMainDataDir());
    FILE *pf = fopen(szPath, "rb");
    if (pf == NULL) {
        HostOutputDebugString("HD sprites: no %s", szPath);
        return;
    }
    char szLine[512];
    int cEntries = 0;
    while (fgets(szLine, sizeof(szLine), pf) != NULL) {
        if (szLine[0] == '#' || szLine[0] == '\n' || szLine[0] == '\r')
            continue;
        char *apsz[6];
        int c = 0;
        char *psz = szLine;
        while (c < 6) {
            apsz[c++] = psz;
            char *pszTab = strchr(psz, '\t');
            if (pszTab == NULL)
                break;
            *pszTab = 0;
            psz = pszTab + 1;
        }
        if (c < 6)
            continue;
        std::string strAnir = apsz[0];
        if (strAnir == "spec") {
            std::string strKey = std::string(apsz[1]) + "|" + apsz[2] + "|" + apsz[3];
            for (size_t j = 0; j < strKey.size(); j++)
                strKey[j] = tolower(strKey[j]);
            s_mapSpec[strKey] = std::pair<int, int>(atoi(apsz[4]), atoi(apsz[5]));
            continue;
        }
        if (strAnir == "track") {
            HdTrack *ptrk = &s_mapTracks[apsz[1]];
            ptrk->u0 = atoi(apsz[2]);
            ptrk->u1 = atoi(apsz[3]);
            ptrk->strPattern = apsz[4];
            ptrk->cPics = atoi(apsz[5]);
            continue;
        }
        if (strAnir == "trackpt") {
            HdTrack *ptrk = &s_mapTracks[apsz[1]];
            int iPt = atoi(apsz[2]);
            if (iPt >= 0 && iPt < 1000) {
                if ((int)ptrk->adx.size() <= iPt) {
                    ptrk->adx.resize(iPt + 1);
                    ptrk->ady.resize(iPt + 1);
                }
                ptrk->adx[iPt] = atoi(apsz[4]);
                ptrk->ady[iPt] = atoi(apsz[5]);
            }
            continue;
        }
        int i;
        for (i = 0; i < (int)s_aanir.size(); i++) {
            if (s_aanir[i].strName == strAnir)
                break;
        }
        if (i == (int)s_aanir.size()) {
            s_aanir.push_back(HdAnir());
            s_aanir.back().strName = strAnir;
        }
        HdEntry ent;
        ent.strPic = apsz[3];
        ent.dx = atoi(apsz[4]);
        ent.dy = atoi(apsz[5]);
        std::string strKey = std::string(apsz[1]) + "|" + apsz[2];
        s_aanir[i].mapFrames[strKey].push_back(ent);
        cEntries++;
    }
    fclose(pf);
    s_fHaveData = cEntries > 0;
    HostOutputDebugString("HD sprites: %d pictures for %d animations", cEntries, (int)s_aanir.size());
}

bool HdGetSpecialPoint(int ihdAnir, const char *pszStrip, int nFrame, Point *ppt) {
    HdInit();
    if (!s_fHaveData || ihdAnir < 0 || ihdAnir >= (int)s_aanir.size() || s_mapSpec.empty())
        return false;
    char szFrame[16];
    snprintf(szFrame, sizeof(szFrame), "%d", nFrame);
    std::string strKey = s_aanir[ihdAnir].strName + "|" + pszStrip + "|" + szFrame;
    for (size_t j = 0; j < strKey.size(); j++)
        strKey[j] = tolower(strKey[j]);
    std::map<std::string, std::pair<int, int> >::iterator it = s_mapSpec.find(strKey);
    if (it == s_mapSpec.end())
        return false;
    ppt->x = it->second.first;
    ppt->y = it->second.second;
    return true;
}

bool HdIsActive() {
    HdInit();
    return s_fHaveData && s_fEnabled;
}

void HdToggle() {
    HdInit();
    if (!s_fHaveData)
        return;
    s_fEnabled = !s_fEnabled;
    s_acmd.clear();
    // Everything has to draw again (classic sprites back into the 8-bit frame)
    if (gpupdSim != NULL)
        gpupdSim->InvalidateRect();
    if (gpmfrmm != NULL)
        gpmfrmm->InvalidateRect(NULL);
}

int HdFindAnir(const char *pszAniName) {
    HdInit();
    for (int i = 0; i < (int)s_aanir.size(); i++) {
        if (stricmp(s_aanir[i].strName.c_str(), pszAniName) == 0)
            return i;
    }
    return -1;
}

// Set while recording a frame that has pictures changing between game
// updates (fan, dish), so the map keeps being redrawn in between

static bool s_fTimed;

void HdBeginSimFrame() {
    s_acmd.clear();
    s_fTimed = false;
}

bool HdWantsRedraw() {
    return HdIsActive() && s_fTimed;
}

static byte *LoadGrey(const char *psz, int cx, int cy) {
    int cxT, cyT, cn;
    byte *pb = stbi_load(psz, &cxT, &cyT, &cn, 1);
    if (pb == NULL)
        return NULL;
    if (cxT != cx || cyT != cy) {
        stbi_image_free(pb);
        return NULL;
    }
    return pb;
}

static HdPic *GetPic(const std::string &strPic) {
    std::map<std::string, HdPic *>::iterator it = s_mapPics.find(strPic);
    if (it != s_mapPics.end())
        return it->second;
    HdPic *ppic = new HdPic;
    memset(ppic->aptex, 0, sizeof(ppic->aptex));
    ppic->ptexShadow = NULL;
    ppic->strPath = strPic;
    ppic->fLoaded = true;
    ppic->fOk = false;
    ppic->pbRgba = ppic->pbTeam = ppic->pbShadow = NULL;
    ppic->cx = ppic->cy = 0;
    s_mapPics[strPic] = ppic;

    char szPath[PATH_MAX];
    snprintf(szPath, sizeof(szPath), "%s/hd/%s.png", HostHelpers::GetMainDataDir(), strPic.c_str());
    int cn;
    ppic->pbRgba = stbi_load(szPath, &ppic->cx, &ppic->cy, &cn, 4);
    if (ppic->pbRgba == NULL) {
        HostOutputDebugString("HD sprites: can't load %s", szPath);
        return ppic;
    }
    snprintf(szPath, sizeof(szPath), "%s/hd/%s_team.png", HostHelpers::GetMainDataDir(), strPic.c_str());
    ppic->pbTeam = LoadGrey(szPath, ppic->cx, ppic->cy);
    snprintf(szPath, sizeof(szPath), "%s/hd/%s_shadow.png", HostHelpers::GetMainDataDir(), strPic.c_str());
    ppic->pbShadow = LoadGrey(szPath, ppic->cx, ppic->cy);
    ppic->fOk = true;
    return ppic;
}

// How far the game is between its last update and the next one, 0..256.
// Updates come every gtGameSpeed ticks: 40 ms at the default speed, 80 ms
// at normal, and so on, so the real interval is used (as for the moving
// units in Simulation::BeginInterpolatedDraw), not a fixed 80 ms. It stops
// at 256 while the game is paused or waiting.

extern long gmsLastSimUpdate;
extern int gtGameSpeed;

static int UpdateAlpha256() {
    long msInterval = (long)gtGameSpeed * 10;
    if (msInterval <= 0)
        return 0;
    long msSince = HostGetMillisecondCount() - gmsLastSimUpdate;
    if (msSince < 0)
        msSince = 0;
    if (msSince >= msInterval)
        return 256;
    return (int)(msSince * 256 / msInterval);
}

// Game time in ms: 80 per game update (the art's time base) plus the part
// of the current update that has passed

static long GameMs() {
    return (long)gsim.GetUpdateCount() * 80 + UpdateAlpha256() * 80 / 256;
}

// {L<n>x<ms>}: an n-picture loop at a steady <ms> per picture (Surveillance
// Center dish {L12x30}, R&D Center dish {L52x40}), in game time at the
// default game speed (40 ms updates; a slower game speed slows it down like
// the classic animation). It only runs while the game keeps advancing the
// classic animation (each Animation, passed as the key, says when it was
// last advanced), so the dish stops with low power, and it stops when the
// game is paused. Each building keeps its own place in the loop.

struct HdLoopTrack {
    int nFrame;
    long tFrameChanged;     // real ms when the classic frame last changed
    long msGameLast;        // game time at the last draw
    long msPhase;           // how far this loop has played, ms
};
static std::map<void *, HdLoopTrack> s_mapLoop;

static int LoopPicture(void *pvKey, int nFrame, Side side, int cPics, int msPic) {
    long t = HostGetMillisecondCount();
    long msGame = (long)gsim.GetUpdateCount() * 40 + UpdateAlpha256() * 40 / 256;
    long msLoop = (long)cPics * msPic;
    if (pvKey == NULL)
        return (int)((msGame % msLoop) / msPic);
    if (s_mapLoop.size() > 4096)
        s_mapLoop.clear();
    std::map<void *, HdLoopTrack>::iterator it = s_mapLoop.find(pvKey);
    if (it == s_mapLoop.end()) {
        HdLoopTrack trk = { nFrame, t, msGame, 0 };
        s_mapLoop[pvKey] = trk;
        return 0;
    }
    HdLoopTrack *ptrk = &it->second;
    if (ptrk->nFrame != nFrame) {
        ptrk->nFrame = nFrame;
        ptrk->tFrameChanged = t;
    }
    // Running: the owner's power is not low (as for the classic dishes), and
    // the game is advancing the classic animation - or, for a one-frame strip
    // (damaged R&D Center), which the game never needs to advance, always

    Animation *pani = (Animation *)pvKey;
    bool fRun;
    if (pani->GetAnimationData()->GetFrameCount(pani->GetStrip()) <= 1) {
        fRun = true;
    } else {
        long tRun = ptrk->tFrameChanged;
        if (pani->GetLastAdvanceMs() > tRun)
            tRun = pani->GetLastAdvanceMs();
        fRun = t - tRun < 1500;
    }
    if (side >= ksideNeutral && side < kcSides) {
        Player *pplr = gplrm.GetPlayer(side);
        if (pplr != NULL && pplr->IsPowerLow())
            fRun = false;
    }
    long dms = msGame - ptrk->msGameLast;
    ptrk->msGameLast = msGame;
    if (dms < 0 || dms > 200)
        dms = 0;
    if (fRun) {
        ptrk->msPhase = (ptrk->msPhase + dms) % msLoop;
        s_fTimed = true;    // keep drawing between game updates
    }
    return (int)(ptrk->msPhase / msPic);
}

// {M rf a b rb c d}: smooth in-between pictures for a classic frame that lasts
// one game update (HQ hatch, Processor door). When the game moves this
// animation (the key) onto the frame, pictures a..b-1 play over that update
// if the frame went forward, c..d-1 if it went back (d < c plays down);
// afterwards, or when the frame is held, picture rf (came forward) or rb
// (came back) shows.

struct HdMoveTrack {
    int nFrame;
    long tChanged;
    bool fForward;
};
static std::map<void *, HdMoveTrack> s_mapMove;

static int MovePicture(void *pvKey, int nFrame, int rf, int a, int b, int rb, int c, int d) {
    if (pvKey == NULL)
        return rf;
    long t = HostGetMillisecondCount();
    if (s_mapMove.size() > 4096)
        s_mapMove.clear();
    std::map<void *, HdMoveTrack>::iterator it = s_mapMove.find(pvKey);
    if (it == s_mapMove.end()) {
        HdMoveTrack trk = { nFrame, t - 100000, true };
        s_mapMove[pvKey] = trk;
        return rf;
    }
    HdMoveTrack *ptrk = &it->second;
    if (ptrk->nFrame != nFrame) {
        ptrk->fForward = nFrame > ptrk->nFrame;
        ptrk->nFrame = nFrame;
        ptrk->tChanged = t;
    }
    int r = ptrk->fForward ? rf : rb;
    long msInterval = (long)gtGameSpeed * 10;
    if (msInterval <= 0)
        return r;
    long ms = t - ptrk->tChanged;
    int i0 = ptrk->fForward ? a : c, i1 = ptrk->fForward ? b : d;
    if (ms >= msInterval || i0 == i1)
        return r;
    s_fTimed = true;    // keep drawing between game updates
    int n = i1 > i0 ? i1 - i0 : i0 - i1;
    int k = (int)(ms * n / msInterval);
    if (k > n - 1)
        k = n - 1;
    return i1 > i0 ? i0 + k : i0 - k;
}

// Expand {S} (base state), {T36} (36-frame loop in game time) and
// {L<n>x<ms>} (n-picture loop while the classic animation runs)

static std::string ExpandPic(const std::string &str, int nState, int nFrame, Side side, void *pvKey) {
    std::string strOut = str;
    size_t i = strOut.find("{S}");
    if (i != std::string::npos) {
        char sz[8];
        snprintf(sz, sizeof(sz), "%d", nState == 1 ? 1 : 0);
        strOut.replace(i, 3, sz);
    }
    i = strOut.find("{T36}");
    if (i != std::string::npos) {
        // 33 ms per picture in game time
        s_fTimed = true;
        long msGame = GameMs();
        char sz[8];
        snprintf(sz, sizeof(sz), "%02d", (int)((msGame / 33) % 36));
        strOut.replace(i, 5, sz);
    }
    i = strOut.find("{M ");
    if (i != std::string::npos) {
        size_t j = strOut.find('}', i);
        int rf, a, b, rb, c, d;
        if (j != std::string::npos &&
                sscanf(strOut.c_str() + i, "{M %d %d %d %d %d %d}", &rf, &a, &b, &rb, &c, &d) == 6) {
            char sz[8];
            snprintf(sz, sizeof(sz), "%02d", MovePicture(pvKey, nFrame, rf, a, b, rb, c, d));
            strOut.replace(i, j - i + 1, sz);
        }
    }
    i = strOut.find("{L");
    if (i != std::string::npos) {
        size_t j = strOut.find('}', i);
        int cPics = 0, msPic = 0;
        if (j != std::string::npos &&
                sscanf(strOut.c_str() + i, "{L%dx%d}", &cPics, &msPic) == 2 &&
                cPics > 0 && msPic > 0) {
            char sz[8];
            snprintf(sz, sizeof(sz), "%02d", LoopPicture(pvKey, nFrame, side, cPics, msPic));
            strOut.replace(i, j - i + 1, sz);
        }
    }
    return strOut;
}

// Game updates (with the part of the current one that has passed) since
// this animation started its strip. Each Animation (the key) remembers the
// update its strip began in; a lower frame than last time means it started
// again (or a new animation took its place).

struct HdStripClock {
    int nFrameLast;
    int cUpdateStart;
};
static std::map<void *, HdStripClock> s_mapClock;

static float StripTime(void *pvKey, int nFrame) {
    int cUpdates = gsim.GetUpdateCount();
    if (s_mapClock.size() > 4096)
        s_mapClock.clear();
    std::map<void *, HdStripClock>::iterator it = s_mapClock.find(pvKey);
    if (it == s_mapClock.end() || nFrame < it->second.nFrameLast) {
        HdStripClock clk = { nFrame, cUpdates - nFrame };
        s_mapClock[pvKey] = clk;
        it = s_mapClock.find(pvKey);
    }
    it->second.nFrameLast = nFrame;
    return (float)(cUpdates - it->second.cUpdateStart) + UpdateAlpha256() / 256.0f;
}

bool HdDrawFrame(int ihdAnir, const char *pszStrip, int nFrame, DibBitmap *pbm,
        int x, int y, Side side, int *pnState, void *pvKey) {
    if (!HdIsActive() || ihdAnir < 0 || ihdAnir >= (int)s_aanir.size())
        return false;

    // Remember which base state the animation drew last (Processor: the
    // door goes with the healthy or the damaged base)

    if (strncmp(pszStrip, "base ", 5) == 0)
        *pnState = atoi(pszStrip + 5);

    // Only the playfield; the same animations also draw in menus and forms

    if (gpfrmmSim == NULL || pbm != gpfrmmSim->GetDib())
        return false;

    char szKey[64];
    snprintf(szKey, sizeof(szKey), "%s|%d", pszStrip, nFrame);
    HdAnir *panir = &s_aanir[ihdAnir];
    std::map<std::string, std::vector<HdEntry> >::iterator it = panir->mapFrames.find(szKey);
    if (it == panir->mapFrames.end())
        return false;

    int nR = HdPixelsPerGamePixel();
    HdCommand cmd;
    bool fFirst = true;
    for (int i = 0; i < (int)it->second.size(); i++) {
        HdEntry *pent = &it->second[i];
        int dx = pent->dx, dy = pent->dy;
        HdPic *ppic;
        if (pent->strPic[0] == '@') {
            // Timed path: which picture and where, from the strip's age
            std::map<std::string, HdTrack>::iterator itT = s_mapTracks.find(pent->strPic.substr(1));
            if (itT == s_mapTracks.end() || pvKey == NULL)
                continue;
            HdTrack *ptrk = &itT->second;
            int cPts = (int)ptrk->adx.size();
            if (ptrk->cPics <= 0 || cPts < ptrk->cPics || ptrk->u1 <= ptrk->u0)
                continue;
            float t = StripTime(pvKey, nFrame);
            float p = (t - ptrk->u0) / (ptrk->u1 - ptrk->u0);
            if (p < 0 || p >= 1)
                continue;
            s_fTimed = true;    // keep drawing between game updates
            float fIdx = p * ptrk->cPics;
            int iPic = (int)fIdx;
            if (iPic > ptrk->cPics - 1)
                iPic = ptrk->cPics - 1;
            // position glides between this picture's point and the next one's
            float frac = fIdx - iPic;
            int iNext = iPic + 1 < ptrk->cPics ? iPic + 1 : iPic;
            dx = (int)(ptrk->adx[iPic] + (ptrk->adx[iNext] - ptrk->adx[iPic]) * frac + 0.5f);
            dy = (int)(ptrk->ady[iPic] + (ptrk->ady[iNext] - ptrk->ady[iPic]) * frac + 0.5f);
            char szPic[128];
            snprintf(szPic, sizeof(szPic), ptrk->strPattern.c_str(), iPic);
            ppic = GetPic(szPic);
            if (!ppic->fOk)
                continue;
        } else {
            ppic = GetPic(ExpandPic(pent->strPic, *pnState, nFrame, side, pvKey));
            if (!ppic->fOk)
                return false;   // missing picture: draw the classic sprite
        }
        HdItem itm;
        itm.ppic = ppic;
        itm.side = (short)side < 0 ? kcSides : (side < kcSides ? side : 0);
        itm.xHd = x * nR + dx;
        itm.yHd = y * nR + dy;
        cmd.aitm.push_back(itm);

        // Game pixels the picture covers

        Rect rc;
        rc.left = itm.xHd >= 0 ? itm.xHd / nR : -((-itm.xHd + nR - 1) / nR);
        rc.top = itm.yHd >= 0 ? itm.yHd / nR : -((-itm.yHd + nR - 1) / nR);
        rc.right = (itm.xHd + ppic->cx + nR - 1) / nR;
        rc.bottom = (itm.yHd + ppic->cy + nR - 1) / nR;
        if (fFirst) {
            cmd.rc = rc;
            fFirst = false;
        } else {
            cmd.rc.Union(&rc);
        }
    }

    // Nothing to draw right now (a timed puff not started yet or over): the
    // HD set still replaces the classic frame
    if (cmd.aitm.empty())
        return true;

    // Snapshot of the 8-bit pixels under the pictures

    Size siz;
    pbm->GetSize(&siz);
    Rect rcDib;
    rcDib.Set(0, 0, siz.cx, siz.cy);
    cmd.rc.Intersect(&cmd.rc, &rcDib);
    if (!cmd.rc.IsEmpty()) {
        byte *pbBits = pbm->GetBits();
        int cbRow = pbm->GetRowBytes();
        int cx = cmd.rc.Width();
        cmd.abSnap.resize(cx * cmd.rc.Height());
        for (int yT = cmd.rc.top; yT < cmd.rc.bottom; yT++)
            memcpy(&cmd.abSnap[(yT - cmd.rc.top) * cx], pbBits + yT * cbRow + cmd.rc.left, cx);
        s_acmd.push_back(cmd);
    }
    return true;
}

// Team colour: the pictures are painted blue, darker than the classic sprites'
// team colours. Every side (blue too) takes its colours from its own 5-shade
// ramp in the palette, like the classic sprites: the painted brightness,
// brightened by kHdTeamGain, picks the shade (blue ramp brightness 232 = the
// side's brightest shade ... 64 = its darkest), so team colours are as bright
// as the classic ones and similar sides (Red, Crimson) stay easy to tell apart.
// Pale (unsaturated) painted highlights keep some of their paleness.

static const float kHdTeamGain = 1.35f;

static void TeamRampColor(const float (*aclrRamp)[3], const float *avBlue, float v,
        float *pr, float *pg, float *pb) {
    // avBlue: brightness of the blue ramp, brightest first (232 ... 64)
    float c[3];
    if (v >= avBlue[0]) {
        float k = v / avBlue[0];
        for (int j = 0; j < 3; j++) c[j] = aclrRamp[0][j] * k;
    } else if (v <= avBlue[4]) {
        float k = v / avBlue[4];
        for (int j = 0; j < 3; j++) c[j] = aclrRamp[4][j] * k;
    } else {
        int i = 0;
        while (i < 3 && v < avBlue[i + 1])
            i++;
        float t = (v - avBlue[i + 1]) / (avBlue[i] - avBlue[i + 1]);
        for (int j = 0; j < 3; j++) c[j] = aclrRamp[i + 1][j] + (aclrRamp[i][j] - aclrRamp[i + 1][j]) * t;
    }
    *pr = c[0] > 255 ? 255 : c[0];
    *pg = c[1] > 255 ? 255 : c[1];
    *pb = c[2] > 255 ? 255 : c[2];
}

static SDL_Texture *GetTexture(SDL_Renderer *prend, HdPic *ppic, int side, Uint32 *palette) {
    if (ppic->aptex[side] != NULL)
        return ppic->aptex[side];
    int cb = ppic->cx * ppic->cy * 4;
    byte *pb = new byte[cb];
    memcpy(pb, ppic->pbRgba, cb);
    if (ppic->pbTeam != NULL) {
        static const int aiclrFirst[kcSides] = {
            kiclrNeutralSideFirst, kiclrBlueSideFirst, kiclrRedSideFirst,
            kiclrYellowSideFirst, kiclrCyanSideFirst, kiclrCrimsonSideFirst };
        float aclrRamp[5][3], avBlue[5];
        for (int k = 0; k < 5; k++) {
            Uint32 clrB = palette[GetColor(kiclrBlueSideFirst + k)];
            int r = (clrB >> 16) & 255, g = (clrB >> 8) & 255, b = clrB & 255;
            avBlue[k] = (float)(r > g ? (r > b ? r : b) : (g > b ? g : b));
            Uint32 clr = palette[GetColor(aiclrFirst[side < kcSides ? side : 1] + k)];
            aclrRamp[k][0] = (float)((clr >> 16) & 255);
            aclrRamp[k][1] = (float)((clr >> 8) & 255);
            aclrRamp[k][2] = (float)(clr & 255);
        }
        for (int i = 0; i < ppic->cx * ppic->cy; i++) {
            int nTeam = ppic->pbTeam[i];
            if (nTeam == 0)
                continue;
            float m = nTeam / 255.0f;
            float r = pb[i * 4], g = pb[i * 4 + 1], b = pb[i * 4 + 2];
            float r2, g2, b2;
            if (side == kcSides) {
                // flash white
                r2 = g2 = b2 = 255.0f;
            } else {
                float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
                float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
                float sat = mx > 0 ? (mx - mn) / mx : 0;
                float v = mx * kHdTeamGain;
                TeamRampColor(aclrRamp, avBlue, v, &r2, &g2, &b2);
                float k = sat / 0.6f;
                if (k > 1) k = 1;
                float grey = v > 255 ? 255 : v;
                r2 = r2 * k + grey * (1 - k);
                g2 = g2 * k + grey * (1 - k);
                b2 = b2 * k + grey * (1 - k);
            }
            pb[i * 4] = (byte)(r + (r2 - r) * m + 0.5f);
            pb[i * 4 + 1] = (byte)(g + (g2 - g) * m + 0.5f);
            pb[i * 4 + 2] = (byte)(b + (b2 - b) * m + 0.5f);
        }
    }
    SDL_Texture *ptex = SDL_CreateTexture(prend, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, ppic->cx, ppic->cy);
    if (ptex != NULL) {
        SDL_UpdateTexture(ptex, NULL, pb, ppic->cx * 4);
        SDL_SetTextureBlendMode(ptex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(ptex, SDL_ScaleModeLinear);
    }
    delete[] pb;
    ppic->aptex[side] = ptex;
    return ptex;
}

static SDL_Texture *GetShadowTexture(SDL_Renderer *prend, HdPic *ppic) {
    if (ppic->pbShadow == NULL)
        return NULL;
    if (ppic->ptexShadow != NULL)
        return ppic->ptexShadow;
    int c = ppic->cx * ppic->cy;
    byte *pb = new byte[c * 4];
    for (int i = 0; i < c; i++) {
        pb[i * 4] = pb[i * 4 + 1] = pb[i * 4 + 2] = 0;
        pb[i * 4 + 3] = (byte)(ppic->pbShadow[i] * 6 / 10);     // 255 = darken by 60%
    }
    SDL_Texture *ptex = SDL_CreateTexture(prend, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, ppic->cx, ppic->cy);
    if (ptex != NULL) {
        SDL_UpdateTexture(ptex, NULL, pb, ppic->cx * 4);
        SDL_SetTextureBlendMode(ptex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(ptex, SDL_ScaleModeLinear);
    }
    delete[] pb;
    ppic->ptexShadow = ptex;
    return ptex;
}

// Draw the part of a picture that lies inside game-pixel rect prc

static void DrawPart(SDL_Renderer *prend, SDL_Texture *ptex, HdItem *pitm, Rect *prc, int nR, float flScale) {
    int x0 = prc->left * nR - pitm->xHd;
    int y0 = prc->top * nR - pitm->yHd;
    int x1 = prc->right * nR - pitm->xHd;
    int y1 = prc->bottom * nR - pitm->yHd;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > pitm->ppic->cx) x1 = pitm->ppic->cx;
    if (y1 > pitm->ppic->cy) y1 = pitm->ppic->cy;
    if (x1 <= x0 || y1 <= y0)
        return;
    SDL_Rect rcSrc = { x0, y0, x1 - x0, y1 - y0 };
    SDL_FRect rcDst = { (pitm->xHd + x0) * flScale, (pitm->yHd + y0) * flScale,
            (x1 - x0) * flScale, (y1 - y0) * flScale };
    SDL_RenderCopyF(prend, ptex, &rcSrc, &rcDst);
}

bool HdHasCommands() {
    return HdIsActive() && !s_acmd.empty();
}

// Draw the recorded pictures onto the current render target, which holds the
// 8-bit image scaled up nScale times. pbFront is the finished 8-bit image.

void HdComposite(SDL_Renderer *prend, int nScale, byte *pbFront, int cxFront, int cyFront, Uint32 *palette) {
    int nR = HdPixelsPerGamePixel();
    float flScale = (float)nScale / nR;
    std::vector<byte> abMask;
    std::vector<Rect> arcRuns;

    for (int icmd = 0; icmd < (int)s_acmd.size(); icmd++) {
        HdCommand *pcmd = &s_acmd[icmd];
        Rect rc = pcmd->rc;
        if (rc.right > cxFront || rc.bottom > cyFront)
            continue;
        int cx = rc.Width();
        int cy = rc.Height();

        // Which pixels still show what was under the picture when it was
        // recorded (nothing drawn on top since)

        abMask.resize(cx * cy);
        int cMatch = 0;

        // A classic sprite's shadow only darkens what's under it (through
        // the level's shadow map); a shadow falling on an HD picture (the
        // classic Bullpup driving into the HD Processor) doesn't cover it

        byte *mpShadow = gsim.GetLevel() != NULL ? gsim.GetLevel()->GetShadowMap() : NULL;
        for (int y = 0; y < cy; y++) {
            byte *pbF = pbFront + (rc.top + y) * cxFront + rc.left;
            byte *pbS = &pcmd->abSnap[y * cx];
            byte *pbM = &abMask[y * cx];
            for (int x = 0; x < cx; x++) {
                pbM[x] = pbF[x] == pbS[x] || (mpShadow != NULL && pbF[x] == mpShadow[pbS[x]]);
                cMatch += pbM[x];
            }
        }

        // Mostly covered: stale (e.g. a menu over the old playfield)

        if (cMatch * 10 < cx * cy * 4)
            continue;

        // Runs of visible pixels, row by row (one rect if nothing covers it)

        arcRuns.clear();
        if (cMatch == cx * cy) {
            arcRuns.push_back(rc);
        } else {
            for (int y = 0; y < cy; y++) {
                byte *pbM = &abMask[y * cx];
                int x = 0;
                while (x < cx) {
                    while (x < cx && !pbM[x])
                        x++;
                    int xStart = x;
                    while (x < cx && pbM[x])
                        x++;
                    if (x > xStart) {
                        Rect rcRun;
                        rcRun.Set(rc.left + xStart, rc.top + y, rc.left + x, rc.top + y + 1);
                        // Merge with the same run on the row above
                        bool fMerged = false;
                        for (int i = (int)arcRuns.size() - 1; i >= 0; i--) {
                            Rect *prc = &arcRuns[i];
                            if (prc->bottom < rcRun.top)
                                break;
                            if (prc->bottom == rcRun.top && prc->left == rcRun.left && prc->right == rcRun.right) {
                                prc->bottom = rcRun.bottom;
                                fMerged = true;
                                break;
                            }
                        }
                        if (!fMerged)
                            arcRuns.push_back(rcRun);
                    }
                }
            }
        }

        // Shadows first, then the pictures

        for (int i = 0; i < (int)pcmd->aitm.size(); i++) {
            HdItem *pitm = &pcmd->aitm[i];
            SDL_Texture *ptex = GetShadowTexture(prend, pitm->ppic);
            if (ptex == NULL)
                continue;
            for (int j = 0; j < (int)arcRuns.size(); j++)
                DrawPart(prend, ptex, pitm, &arcRuns[j], nR, flScale);
        }
        for (int i = 0; i < (int)pcmd->aitm.size(); i++) {
            HdItem *pitm = &pcmd->aitm[i];
            SDL_Texture *ptex = GetTexture(prend, pitm->ppic, pitm->side, palette);
            if (ptex == NULL)
                continue;
            for (int j = 0; j < (int)arcRuns.size(); j++)
                DrawPart(prend, ptex, pitm, &arcRuns[j], nR, flScale);
        }
    }
}

} // namespace wi
