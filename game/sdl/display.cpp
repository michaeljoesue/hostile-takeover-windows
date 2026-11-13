#include "../ht.h"
#include "game/sdl/sdlspritemgr.h"
#include "game/sdl/hosthelpers.h"
#include "game/sdl/hdsprites.h"
#include "game/sdl/sdlspritemgr.h"

namespace wi {

static SdlSpriteManager *s_psprm;
static Size s_siz;

Display *HostCreateDisplay() {
	// Create a display

	Display *pdisp = new Display();
	if (pdisp == NULL)
		return NULL;
	if (!pdisp->Init()) {
		delete pdisp;
		return NULL;
	}
	return pdisp;
}

Display::Display()
{
	m_cx = 0;
	m_cy = 0;
	memset(m_amodeInfo, 0, sizeof(m_amodeInfo));
	m_imode = -1;
	m_cmodes = 0;
	m_pbmBack = NULL;
	m_pbmFront = NULL;
	m_pbmClip = NULL;

    m_window = NULL;
    m_renderer = NULL;
    m_texture = NULL;
    m_textureHd = NULL;
    m_cxHd = 0;
    m_cyHd = 0;

    m_gamePixels = NULL;
    m_gamePixels32 = NULL;
    memset(m_palette, 0, sizeof(m_palette));
    m_pitch32 = 0;
    m_pixelCount = 0;

    m_density = 0;
    m_fShouldRender = false;
}

Display::~Display()
{
	delete m_pbmBack;
	m_pbmBack = NULL;
	delete m_pbmFront;
	m_pbmFront = NULL;
	delete m_pbmClip;
	m_pbmClip = NULL;

    SDL_DestroyWindow(m_window);
    m_window = NULL;
    SDL_DestroyRenderer(m_renderer);
    m_renderer = NULL;
    SDL_DestroyTexture(m_texture);
    m_texture = NULL;

    free(m_gamePixels);
    m_gamePixels = NULL;
    free(m_gamePixels32);
    m_gamePixels32 = NULL;
}

bool Display::Init()
{
    dword videoflags;

    /* Initialize SDL */
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        LOG() << "Couldn't initialize SDL: " << SDL_GetError();
        return false;
    }

	// Absolutely do not mess with SDL_FULLSCREEN if there is any chance the app
	// will crash or stop at a breakpoint. If it does you will be lost in full
	// screen mode! (ssh from another machine and kill the Xcode process)
    videoflags = SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN;

    // Desktop Windows: a fixed-size window (not resizable, no fullscreen).
    // By default it fills the work area and the game picture size is picked
    // from the monitor so it's a whole number of times smaller than the
    // window (see CalcGameModes in win/hosthelpers.cpp); Options > Display
    // can pick a fixed resolution instead (SetMode). It's scaled up with
    // sharp, square pixels.
    #if defined(_WIN32)
    videoflags = SDL_WINDOW_SHOWN;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    #endif

    // Android wants to process mouse and touch events separately

    // Get surface properties
    SurfaceProperties props;
    HostHelpers::GetSurfaceProperties(&props);
    m_cx = props.cxWidth;
    m_cy = props.cyHeight;
    m_density = props.density;
    
    // Create window
    int xWindow = SDL_WINDOWPOS_CENTERED;
    int yWindow = SDL_WINDOWPOS_CENTERED;
    int cxWindow = m_cx;
    int cyWindow = m_cy;
#if defined(_WIN32)
    int nScale = HostGetGameScale();
    cxWindow = m_cx * nScale;
    cyWindow = m_cy * nScale;
    HostGetGameWindowPos(&xWindow, &yWindow);
#endif
    if ((m_window = SDL_CreateWindow("Hostile Takeover", xWindow, yWindow, cxWindow, cyWindow, videoflags)) == NULL) {
        LOG() << "Couldn't create window: " << m_cx << "x" << m_cy << " \nError: " << SDL_GetError();
        return false;
    }

    // Create renderer
    m_renderer = SDL_CreateRenderer(m_window, 0, SDL_RENDERER_TARGETTEXTURE);
#if defined(_WIN32)
    if (m_renderer == NULL) {
        // Fall back to whatever renderer is available
        m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_TARGETTEXTURE);
    }
    // Scale the game image up by a whole number (sharp pixels), centered.
    // SDL also maps mouse coordinates back into game space.
    SDL_RenderSetLogicalSize(m_renderer, m_cx, m_cy);
    SDL_RenderSetIntegerScale(m_renderer, SDL_TRUE);
#endif
    // Wait for the monitor's refresh when presenting, so frames are evenly
    // paced and don't tear (see SetVSync)
    m_nVSync = -1;
    SetVSync(true);
    this->SetShouldRender(true);

    // Keep the screen size around
    s_siz.cx = m_cx;
    s_siz.cy = m_cy;

    // Create texture
    m_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_TARGET, m_cx, m_cy);

    // Set pixel info
    m_pixelCount = m_cx * m_cy;
    m_gamePixels = (byte *)malloc(m_pixelCount);
    m_gamePixels32 = (dword *)malloc(m_pixelCount * 4);
    m_pitch32 = m_cx * sizeof(dword);

    // Keep the screen size around
    s_siz.cx = m_cx;
    s_siz.cy = m_cy;

    ModeInfo *pmode = m_amodeInfo;

#if defined(_WIN32)
    // One mode per display size (Options > Display): the automatic best fit
    // first (marked native, so it's what the game picks as "best"), then
    // fixed resolutions that fit on the screen

    GameMode agm[kcGameModesMax];
    int cgm = HostGetGameModes(agm, kcGameModesMax);
    for (int igm = 0; igm < cgm && igm < kcmodesMax; igm++) {
        pmode->nDepth = 8;
        pmode->cx = agm[igm].cx;
        pmode->cy = agm[igm].cy;
        pmode->cyGraffiti = 0;
        pmode->fNative = agm[igm].fAuto;
        pmode->nDegreeOrientation = 0;
        pmode++;
    }
#else
#if 1
    pmode->nDepth = 8;
    pmode->cx = props.cxWidth;
    pmode->cy = props.cyHeight;
    pmode->cyGraffiti = 0;
    pmode->fNative = true;
    pmode->nDegreeOrientation = 0;
    pmode++;
#endif

#if 0
    pmode->nDepth = 8;
    pmode->cx = props.cyHeight;
    pmode->cy = props.cxWidth;
    pmode->cyGraffiti = 0;
    pmode->fNative = true;
    pmode->nDegreeOrientation = 90; // leftie controls
    pmode++;
#endif

#if 0
    pmode->nDepth = 8;
    pmode->cx = props.cxWidth;
    pmode->cy = props.cyHeight;
    pmode->cyGraffiti = 0;
    pmode->fNative = true;
    pmode->nDegreeOrientation = 180; // bizzaro controls
    pmode++;
#endif
    
#if 0
    pmode->nDepth = 8;
    pmode->cx = props.cyHeight;
    pmode->cy = props.cxWidth;
    pmode->cyGraffiti = 0;
    pmode->fNative = true;
    pmode->nDegreeOrientation = 270; // rightie controls
    pmode++;
#endif
#endif // !_WIN32

    m_cmodes = pmode - m_amodeInfo;

    HostOutputDebugString("Display::Init %d", pmode - m_amodeInfo);
    HostOutputDebugString("Display::Init %d modes", m_cmodes);

    return true;
}

void Display::SetPalette(Palette *ppal)
{
	SDL_Color aclr[256];
	int cEntries = BigWord(ppal->cEntries);
	byte *pb = (byte *)ppal->argb;
	SDL_Color *pclr = aclr;
	
	for (int i = 0; i < cEntries; i++) {
		pclr->r = *pb++;
		pclr->g = *pb++;
		pclr->b = *pb++;
		pclr++;
	}
    
    for (int i = 0; i < cEntries; i++) {
        m_palette[i] = ((Uint8)aclr[i].r << 16) | ((Uint8)aclr[i].g << 8) | ((Uint8)aclr[i].b << 0);
    }
    
}

int Display::GetModeCount()
{
    return m_cmodes;
}

void Display::GetModeInfo(int imode, ModeInfo *pmode)
{
	memset(pmode, 0, sizeof(*pmode));
	if (imode >= 0 && imode < m_cmodes)
		*pmode = m_amodeInfo[imode];
}

int Display::GetMode(ModeInfo *pmode)
{
	if (pmode != NULL) {
		if (m_imode == -1) {
			memset(pmode, 0, sizeof(*pmode));
		} else {
			*pmode = m_amodeInfo[m_imode];
		}
	}
	return m_imode;
}

#if defined(_WIN32)
// Switch the window, renderer and buffers to another display size
// (Options > Display picks a mode; the game then restarts its display)

bool Display::ApplyGameMode(int imode)
{
    GameMode agm[kcGameModesMax];
    int cgm = HostGetGameModes(agm, kcGameModesMax);
    if (imode < 0 || imode >= cgm)
        return false;
    GameMode *pgm = &agm[imode];
    if (pgm->cx == m_cx && pgm->cy == m_cy && pgm->nScale == HostGetGameScale())
        return true;

    byte *pbPixels = (byte *)malloc(pgm->cx * pgm->cy);
    dword *pdwPixels32 = (dword *)malloc(pgm->cx * pgm->cy * 4);
    SDL_Texture *ptex = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGB888,
            SDL_TEXTUREACCESS_TARGET, pgm->cx, pgm->cy);
    if (pbPixels == NULL || pdwPixels32 == NULL || ptex == NULL) {
        free(pbPixels);
        free(pdwPixels32);
        if (ptex != NULL)
            SDL_DestroyTexture(ptex);
        return false;
    }

    HostSetGameMode(pgm);
    SDL_SetWindowSize(m_window, pgm->cx * pgm->nScale, pgm->cy * pgm->nScale);
    SDL_SetWindowPosition(m_window, pgm->xWindow, pgm->yWindow);
    SDL_RenderSetLogicalSize(m_renderer, pgm->cx, pgm->cy);

    SDL_DestroyTexture(m_texture);
    m_texture = ptex;
    free(m_gamePixels);
    m_gamePixels = pbPixels;
    free(m_gamePixels32);
    m_gamePixels32 = pdwPixels32;
    m_cx = pgm->cx;
    m_cy = pgm->cy;
    m_pixelCount = m_cx * m_cy;
    m_pitch32 = m_cx * sizeof(dword);
    s_siz.cx = m_cx;
    s_siz.cy = m_cy;
    memset(m_gamePixels, 0, m_pixelCount);
    return true;
}
#endif

bool Display::SetMode(int imode)
{
#if defined(_WIN32)
    if (!ApplyGameMode(imode))
        return false;
#endif

    // Allocate dib

	ModeInfo *pmode = &m_amodeInfo[imode];

	DibBitmap *pbmBack = CreateDibBitmap(NULL, pmode->cx, pmode->cy);
	if (pbmBack == NULL)
		return false;

    DibBitmap *pbmFront = CreateDibBitmap(m_gamePixels, pmode->cx, pmode->cy);
	if (pbmFront == NULL) {
		delete pbmBack;
		return NULL;
	}
	delete m_pbmBack;
	delete m_pbmFront;
	m_pbmBack = pbmBack;
	m_pbmFront = pbmFront;
	m_imode = imode;

	return true;
}

void Display::DrawText(const char *psz, int x, int y, word wf)
{
}

void Display::DrawFrameInclusive(Rect *prc)
{
}

DibBitmap *Display::GetBackDib()
{
    return m_pbmBack;
}

DibBitmap *Display::GetFrontDib()
{
    return m_pbmFront;
}

DibBitmap *Display::GetClippingDib()
{
    DibBitmap *pbm = CreateDibBitmap(NULL, kcCopyBy4Procs * 4, kcCopyBy4Procs * 4);
    if (pbm == NULL)
		return NULL;
	m_pbmClip = pbm;
	return pbm;
}

void Display::GetHslAdjustments(short *pnHueOffset, short *pnSatMultiplier, short *pnLumOffset)
{
	*pnHueOffset = 0;
	*pnSatMultiplier = 0;
	*pnLumOffset = 0;
}

void Display::FrameStart()
{
    // surface->pixels can change every time the surface is locked.
	// TODO(darrinm): problem for, e.g. scrolling optimizations?
    m_pbmFront->Init(m_gamePixels, m_cx, m_cy);
}

void Display::FrameComplete(int cfrmm, UpdateMap **apupd, Rect *arc,
        bool fScrolled)
{
    for (int i = 0; i < m_pixelCount; i++) {
        m_gamePixels32[i] = m_palette[m_gamePixels[i]];
    }

    RenderGameSurface();
}

void Display::RenderGameSurface() {
    if (!m_fShouldRender)
        return;

    // Update the texture
    SDL_UpdateTexture(m_texture, NULL, m_gamePixels32, m_pitch32);

    // Draw any sprites onto the texture
    SDL_SetRenderTarget(m_renderer, m_texture);
    s_psprm->DrawSprites(m_renderer, s_siz);
    SDL_SetRenderTarget(m_renderer, NULL);

    // HD sprites: scale the 8-bit image up to the window's size and draw
    // the HD pictures over it at full resolution
    if (HdHasCommands()) {
        int cxOut, cyOut;
        SDL_GetRendererOutputSize(m_renderer, &cxOut, &cyOut);
        int nScale = cxOut / m_cx;
        if (cyOut / m_cy < nScale)
            nScale = cyOut / m_cy;
        if (nScale < 1)
            nScale = 1;
        if (m_textureHd == NULL || m_cxHd != m_cx * nScale || m_cyHd != m_cy * nScale) {
            if (m_textureHd != NULL)
                SDL_DestroyTexture(m_textureHd);
            m_cxHd = m_cx * nScale;
            m_cyHd = m_cy * nScale;
            m_textureHd = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_TARGET, m_cxHd, m_cyHd);
        }
        if (m_textureHd != NULL) {
            SDL_SetRenderTarget(m_renderer, m_textureHd);
            SDL_RenderCopy(m_renderer, m_texture, NULL, NULL);
            HdComposite(m_renderer, nScale, m_gamePixels, m_cx, m_cy, m_palette);
            SDL_SetRenderTarget(m_renderer, NULL);
            SDL_RenderClear(m_renderer);
            SDL_RenderCopy(m_renderer, m_textureHd, NULL, NULL);
            SDL_RenderPresent(m_renderer);
            return;
        }
    }

    // Present the renderer to the screen
    SDL_RenderClear(m_renderer);
    SDL_RenderCopy(m_renderer, m_texture, NULL, NULL);
    SDL_RenderPresent(m_renderer);
}

// Smooth rendering draws a frame for every 10 ms timer tick while things
// move; vsync paces those frames to the monitor. At the fastest game speed
// (100 updates a second) waiting for the refresh would slow the game down,
// so vsync is turned off there (SmoothRenderTimer in SimUI.cpp decides).

void Display::SetVSync(bool fVSync)
{
    int nVSync = fVSync ? 1 : 0;
    if (m_renderer == NULL || nVSync == m_nVSync)
        return;
    if (SDL_RenderSetVSync(m_renderer, nVSync) == 0)
        m_nVSync = nVSync;
}

// For map scrolling with the mouse (SimUI.cpp): where the mouse is in game
// coordinates, whether it's at a window edge (-1 / 0 / 1 on each axis) and
// whether the middle button is held. Returns false if the game window
// doesn't have the input focus or the mouse isn't over it.

bool Display::GetMouseScrollState(int *px, int *py, int *pdxEdge,
        int *pdyEdge, bool *pfMiddle)
{
    *px = 0;
    *py = 0;
    *pdxEdge = 0;
    *pdyEdge = 0;
    *pfMiddle = false;
    if (m_window == NULL || m_renderer == NULL)
        return false;
    if (!(SDL_GetWindowFlags(m_window) & SDL_WINDOW_INPUT_FOCUS))
        return false;
    if (SDL_GetMouseFocus() != m_window)
        return false;

    int xWindow, yWindow;
    Uint32 ffButtons = SDL_GetMouseState(&xWindow, &yWindow);
    int cxWindow, cyWindow;
    SDL_GetWindowSize(m_window, &cxWindow, &cyWindow);

    // Edge band: a few game pixels, at least 6 window pixels

    int cpEdge = 3 * HostGetGameScale();
    if (cpEdge < 6)
        cpEdge = 6;
    if (xWindow < cpEdge)
        *pdxEdge = -1;
    else if (xWindow >= cxWindow - cpEdge)
        *pdxEdge = 1;
    if (yWindow < cpEdge)
        *pdyEdge = -1;
    else if (yWindow >= cyWindow - cpEdge)
        *pdyEdge = 1;

    float x, y;
    SDL_RenderWindowToLogical(m_renderer, xWindow, yWindow, &x, &y);
    *px = (int)x;
    *py = (int)y;
    *pfMiddle = (ffButtons & SDL_BUTTON_MMASK) != 0;
    return true;
}

void Display::ToggleFullscreen()
{
    // The window setup is fixed (see Init): nothing to do
}

void Display::ResetScrollOffset()
{
#if 0 // TODO(darrinm): SDL-ify
    IPhone::ResetScrollOffset();
#endif
}

SpriteManager *Display::GetSpriteManager()
{
	if (s_psprm == NULL)
		s_psprm = new SdlSpriteManager();
	return s_psprm;
}

void Display::SetFormMgrs(FormMgr *pfrmmSimUI, FormMgr *pfrmmInput)
{
#if 0 // TODO(darrinm): SDL-ify
    IPhone::SetFormMgrs(pfrmmSimUI, pfrmmInput);
#endif
}

float Display::Density() {
    return m_density;
}

void Display::SetShouldRender(bool fsr) {
    m_fShouldRender = fsr;
}

} // namespace wi
