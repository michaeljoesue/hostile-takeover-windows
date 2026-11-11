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
static int s_cUpdatesLast = -1;
static long s_tUpdateLast;

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

// Milliseconds of game time: whole game updates (80 ms) plus the real time
// since the last one (at most 80), so pictures between updates run smoothly
// and stop when the game is paused

static long GameMs(long *pmsSinceUpdate) {
    int cUpdates = gsim.GetUpdateCount();
    long t = HostGetMillisecondCount();
    if (cUpdates != s_cUpdatesLast) {
        s_cUpdatesLast = cUpdates;
        s_tUpdateLast = t;
    }
    long ms = t - s_tUpdateLast;
    if (ms > 80)
        ms = 80;
    if (pmsSinceUpdate != NULL)
        *pmsSinceUpdate = ms;
    return (long)cUpdates * 80 + ms;
}

// {F3}: three HD pictures per classic frame. The in-between pictures are
// shown only while this animation's classic frame is advancing (the
// Surveillance Center stops turning when power is low), so each animation
// remembers its last frame and the game update it changed in

struct HdFrameTrack {
    int nFrame;
    int cUpdateChanged;
};
static std::map<void *, HdFrameTrack> s_mapTrack;

static int SubFrame3(void *pvKey, int nFrame) {
    if (pvKey == NULL)
        return 0;
    long ms;
    GameMs(&ms);
    int cUpdates = gsim.GetUpdateCount();
    if (s_mapTrack.size() > 4096)
        s_mapTrack.clear();
    std::map<void *, HdFrameTrack>::iterator it = s_mapTrack.find(pvKey);
    if (it == s_mapTrack.end()) {
        HdFrameTrack trk = { nFrame, -1000 };
        s_mapTrack[pvKey] = trk;
        return 0;
    }
    if (it->second.nFrame != nFrame) {
        it->second.nFrame = nFrame;
        it->second.cUpdateChanged = cUpdates;
    }
    if (it->second.cUpdateChanged != cUpdates)
        return 0;
    s_fTimed = true;
    int n = (int)(ms * 3 / 80);
    return n > 2 ? 2 : n;
}

// Expand {S} (base state), {T36} (36-frame loop in game time) and {F3}
// (classic frame x 3 + in-between picture)

static std::string ExpandPic(const std::string &str, int nState, int nFrame, void *pvKey) {
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
        long msGame = GameMs(NULL);
        char sz[8];
        snprintf(sz, sizeof(sz), "%02d", (int)((msGame / 33) % 36));
        strOut.replace(i, 5, sz);
    }
    i = strOut.find("{F3}");
    if (i != std::string::npos) {
        char sz[8];
        snprintf(sz, sizeof(sz), "%02d", nFrame * 3 + SubFrame3(pvKey, nFrame));
        strOut.replace(i, 4, sz);
    }
    return strOut;
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
        HdPic *ppic = GetPic(ExpandPic(pent->strPic, *pnState, nFrame, pvKey));
        if (!ppic->fOk)
            return false;   // missing picture: draw the classic sprite
        HdItem itm;
        itm.ppic = ppic;
        itm.side = (short)side < 0 ? kcSides : (side < kcSides ? side : 0);
        itm.xHd = x * nR + pent->dx;
        itm.yHd = y * nR + pent->dy;
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

// Team colour: the pictures are painted blue (side 1). Other sides get the
// hue and saturation of their own team colour, keeping the brightness.

static void RgbToHsv(float r, float g, float b, float *ph, float *ps, float *pv) {
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float d = mx - mn;
    *pv = mx;
    *ps = mx > 0 ? d / mx : 0;
    float h = 0;
    if (d > 0) {
        if (mx == r)
            h = (g - b) / d;
        else if (mx == g)
            h = 2 + (b - r) / d;
        else
            h = 4 + (r - g) / d;
        h *= 60;
        if (h < 0)
            h += 360;
    }
    *ph = h;
}

static void HsvToRgb(float h, float s, float v, float *pr, float *pg, float *pb) {
    float c = v * s;
    float hh = h / 60;
    float xx = c * (1 - fabsf(fmodf(hh, 2) - 1));
    float r = 0, g = 0, b = 0;
    if (hh < 1) { r = c; g = xx; }
    else if (hh < 2) { r = xx; g = c; }
    else if (hh < 3) { g = c; b = xx; }
    else if (hh < 4) { g = xx; b = c; }
    else if (hh < 5) { r = xx; b = c; }
    else { r = c; b = xx; }
    float m = v - c;
    *pr = r + m; *pg = g + m; *pb = b + m;
}

static SDL_Texture *GetTexture(SDL_Renderer *prend, HdPic *ppic, int side, Uint32 *palette) {
    if (ppic->aptex[side] != NULL)
        return ppic->aptex[side];
    int cb = ppic->cx * ppic->cy * 4;
    byte *pb = new byte[cb];
    memcpy(pb, ppic->pbRgba, cb);
    if (side != kside1 && ppic->pbTeam != NULL) {
        static const int aiclrMid[kcSides] = {
            kiclrNeutralSideFirst + 1, kiclrBlueSideFirst + 1, kiclrRedSideFirst + 1,
            kiclrYellowSideFirst + 1, kiclrCyanSideFirst + 1, kiclrCrimsonSideFirst + 1 };
        float hT = 0, sT = 0, vT = 1;
        if (side < kcSides) {
            Uint32 clr = palette[GetColor(aiclrMid[side])];
            RgbToHsv(((clr >> 16) & 255) / 255.0f, ((clr >> 8) & 255) / 255.0f, (clr & 255) / 255.0f, &hT, &sT, &vT);
        }
        for (int i = 0; i < ppic->cx * ppic->cy; i++) {
            int nTeam = ppic->pbTeam[i];
            if (nTeam == 0)
                continue;
            float m = nTeam / 255.0f;
            float r = pb[i * 4] / 255.0f, g = pb[i * 4 + 1] / 255.0f, b = pb[i * 4 + 2] / 255.0f;
            float r2, g2, b2;
            if (side == kcSides) {
                // flash white
                r2 = g2 = b2 = 1.0f;
            } else {
                float h, s, v;
                RgbToHsv(r, g, b, &h, &s, &v);
                // keep the painted shading (brightness); hue and saturation of the side
                float sNew = sT < 0.15f ? sT : s * sT / 0.85f;     // pale trims stay pale
                if (sNew > 1) sNew = 1;
                HsvToRgb(hT, sNew, v * (0.85f + 0.15f * vT), &r2, &g2, &b2);
            }
            pb[i * 4] = (byte)((r + (r2 - r) * m) * 255 + 0.5f);
            pb[i * 4 + 1] = (byte)((g + (g2 - g) * m) * 255 + 0.5f);
            pb[i * 4 + 2] = (byte)((b + (b2 - b) * m) * 255 + 0.5f);
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
        for (int y = 0; y < cy; y++) {
            byte *pbF = pbFront + (rc.top + y) * cxFront + rc.left;
            byte *pbS = &pcmd->abSnap[y * cx];
            byte *pbM = &abMask[y * cx];
            for (int x = 0; x < cx; x++) {
                pbM[x] = pbF[x] == pbS[x];
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
