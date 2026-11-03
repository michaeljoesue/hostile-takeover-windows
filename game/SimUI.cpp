#include "game/ht.h"
#include "game/wistrings.h"
#include <string>

namespace wi {

// Global representing the contiguous rect opaquing the map, in map relative tile coords

TRect *gptrcMapOpaque;
TRect gtrcMapOpaque;

// Handy dandy global flags used for enabling various test features

#ifdef STATS_DISPLAY
int gcBitmapsDrawn;
extern bool gfShowStats;
int gcbCommandsQueued;
#endif
int gcMessagesPerUpdate;

extern bool gfShowFPS;
extern bool gfSuspendUpdates;
extern bool gfSingleStep;

short gcPaint = 0;
#ifdef STATS_DISPLAY
static short s_cUpdateRects = 0;
#endif
static fix s_cfxFPS = 0;
static long s_tLastFPSUpdate = 0;

// Misc SimUI management variables

bool gfDragSelecting;
WPoint s_wptSelect1, s_wptSelect2;
WRect gwrcSelection;
WPoint s_awptSelection[250];
int s_cwptSelection;
bool gfLassoSelection = false;
#ifdef STATS_DISPLAY
long gcPathScoresCalced = 0;
#endif

bool PtInPolygon(WPoint *awpt, int cwpt, WCoord wx, WCoord wy) secSimUIForm;

//
// SimUIForm implementation
//

bool SimUIForm::s_fReadyForPaint;
    
SimUIForm::SimUIForm()
{
	s_fReadyForPaint = false;
	gfDragSelecting = false;
	s_cwptSelection = 0;
    
	m_fTimerAdded = false;

	m_nStateMoveTarget = 0;

    m_ppenh = NULL;
#if defined(IPHONE) || defined(__IPHONEOS__) || defined(__ANDROID__)
    SetUIType(kuitFinger);
#else
    SetUIType(kuitStylus);
#endif
}

SimUIForm::~SimUIForm()
{
	if (m_fTimerAdded)
		gtimm.RemoveTimer(this);

    delete m_ppenh;
}

void SimUIForm::SetUIType(UIType uit)
{
    switch (uit) {
    case kuitFinger:
        delete m_ppenh;
        m_ppenh = new FingerHandler(this);
        break;

    case kuitStylus:
        delete m_ppenh;
        m_ppenh = new StylusHandler(this);
        break;
    }
}

bool SimUIForm::Init(FormMgr *pfrmm, IniReader *pini, word idf)
{
	// Init move target

	m_nStateMoveTarget = 0;
	m_aniMoveTarget.Init(g_panidMoveTarget);

    gtimm.AddTimer(this, gtGameSpeed);
    m_fTimerAdded = true;

	// Let the base Form initialize the controls

	bool fSuccess = Form::Init(pfrmm, pini, idf);

	// Size the rect to the dib

	DibBitmap *pbmSim = m_pfrmm->GetDib();
	Size sizSim;
	pbmSim->GetSize(&sizSim);
	Rect rcSim;
	rcSim.Set(0, 0, sizSim.cx, sizSim.cy);
	SetRect(&rcSim);

	// Set control positions
	
	Control *pctlT;
	Rect rcT;

	// Objective is at upper-left

	pctlT = GetControlPtr(kidcObjective);
	Assert(pctlT != NULL);
	pctlT->SetPosition(0, 0);

	// Countdown timer is at upper-right

	pctlT = GetControlPtr(kidcCountdown);
	Assert(pctlT != NULL);
	pctlT->GetRect(&rcT);
	pctlT->SetPosition(sizSim.cx - rcT.Width(), 0);

	// FPS is at upper right, down 1 line to leave room for countdown timer

	pctlT = GetControlPtr(kidcFps);
	Assert(pctlT != NULL);
	pctlT->GetRect(&rcT);
	pctlT->SetPosition(sizSim.cx - rcT.Width(), rcT.Height());

	// Alert is at bottom left

	pctlT = GetControlPtr(kidcAlert);
	Assert(pctlT != NULL);
	pctlT->GetRect(&rcT);
	pctlT->SetPosition(0, sizSim.cy - rcT.Height());

	// Show / hide these controls

	GetControlPtr(kidcFps)->Show(gfShowFPS);
	GetControlPtr(kidcAlert)->Show(false);
	Assert(gsim.GetLevel() != NULL);
	Assert(gsim.GetLevel()->GetTriggerMgr() != NULL);
	GetControlPtr(kidcCountdown)->Show(gsim.GetLevel()->GetTriggerMgr()->GetCountdownTimer()->GetFlags() & kfCtVisibleAtStart);

    // This form wants pen2 events
    m_wf |= kfFrmDemandPen2;

	return fSuccess;
}

void SimUIForm::OnTimer(long tCurrent) {

	if (gfSuspendUpdates && !gfSingleStep) {
		gevm.SetRedrawFlags(kfRedrawDirty);
		return;
	}
	gfSingleStep = false;
	if (gsim.IsPaused()) {
		return;
    }


    // Update (i.e., 'step') the Simulation

    gsim.Update(&gcmdq);

    // Update UI

	Update();

#ifdef DEBUG_HELPERS
    extern void UpdateLog();
    UpdateLog();
#endif
}

bool SimUIForm::EventProc(Event *pevt)
{
	return Form::EventProc(pevt);
}

void SimUIForm::Update()
{
#ifdef STRESS
	if (gfStress) {
		if (gsim.GetTickCount() > gtStressTimeout) {
            Event evt;
			memset(&evt, 0, sizeof(evt));
			evt.eType = gameOverEvent;
			evt.dw = knGoAbortLevel;
			gevm.PostEvent(&evt);
		}
	}
#endif

	// Advance the move target while it's visible

	if (m_nStateMoveTarget == 1) {
		m_aniMoveTarget.Advance(1);
		if (m_aniMoveTarget.GetFlags() & kfAniDone)
			m_nStateMoveTarget = 2;
	}

	// Have the InputUI update its displays (e.g., credits, power)

	InputUIForm *pfrmInputUI = ggame.GetInputUIForm();
	pfrmInputUI->Update();

	// Update the objective display

	LabelControl *plbl = (LabelControl *)GetControlPtr(kidcObjective);
	plbl->SetText(gpplrLocal->GetObjective());

	// Calc stats

	long tCurrent = HostGetTickCount();
	if (s_tLastFPSUpdate == 0)
		s_tLastFPSUpdate = tCurrent;
	if (tCurrent - s_tLastFPSUpdate >= 100) {
		fix32 fx1 = itofx32(gcPaint * 100);
		fix32 fx2 = itofx32(tCurrent - s_tLastFPSUpdate);
		if (fx2 == 0)
			s_cfxFPS = 0;
		else
			s_cfxFPS = (fix)divfx32(fx1, fx2);
		int cFpsWhole = fx32toi(s_cfxFPS);
		int cFpsFraction = (int)fxtoi(mulfx(fracfx(s_cfxFPS), itofx(10)));

		if (gfShowFPS) {
			char szFPS[80];
			sprintf(szFPS, "%d.%d", cFpsWhole, cFpsFraction);
			((LabelControl *)GetControlPtr(kidcFps))->SetText(szFPS);
		}

		s_tLastFPSUpdate = tCurrent;
		gcPaint = 0;

		// Set the # of pen events to accept per second. We don't need anything faster
		// than the frame rate.

		static long s_tLastPaint;
		if (s_tLastPaint == 0)
			s_tLastPaint = tCurrent;
		gevm.SetPenEventInterval((word)(tCurrent - s_tLastPaint));
		s_tLastPaint = tCurrent;
	}

	// Let the minimap decide if it should invalidate.

	gpmm->Update();

	// Everything has been updated for (at least) the first time
	// so we're ready to display it.

	if (!s_fReadyForPaint) {
		s_fReadyForPaint = true;
		InvalidateRect(NULL);
	}

	// Set redraw

	gevm.SetRedrawFlags(kfRedrawDirty);
}

void AddPointToLassoSelection(WPoint wpt)
{
	// TUNE:
	if (abs(s_awptSelection[s_cwptSelection - 1].wx - wpt.wx) >= WcFromTile16ths(4) ||
			abs(s_awptSelection[s_cwptSelection - 1].wy - wpt.wy) >= WcFromTile16ths(4)) {
		s_awptSelection[s_cwptSelection] = wpt;
		if (s_cwptSelection < (sizeof(s_awptSelection) / sizeof(WPoint)) - 1)
			s_cwptSelection++;
	}
}

void SimUIForm::OnUpdateMapInvalidate(UpdateMap *pupd, Rect *prcOpaque)
{
#ifdef DRAW_LINES
	if (gfDrawLines)
		pupd->InvalidateRect();
#endif

#ifdef DRAW_PATHS
	if (gfDrawPaths)
		pupd->InvalidateRect();
#endif

#if defined(DRAW_OCCUPIED_TILE_INDICATOR) || defined(MARK_TILE_BOUNDARIES) || defined(MARK_OCCUPIED_TILES)
	pupd->InvalidateRect();
#endif

	// Invalidate move target

	if (m_nStateMoveTarget != 0) {
		pupd->InvalidateTile(TcFromWc(m_wxMoveTarget), TcFromWc(m_wyMoveTarget));
		if (m_nStateMoveTarget == 2)
			m_nStateMoveTarget = 0;
	}

	// Handle selection invalidation

	InvalidateDragSelection();

	// Calculate the current opaquing rect for the map.
	// HACK: Store in a global since various places need it, such as when a gob gets removed or when
	// galaxite gets invalidated.

	Rect rcT;
	bool fEmpty = prcOpaque->IsEmpty();
	if (fEmpty) {
// Probably not worth it
#if 0
		// Use the minimap since it covers the map

		Control *pctl = GetControlPtr(kidcMiniMap);
		if (pctl->GetFlags() & kfCtlVisible) {
			pctl->GetRect(&rcT);
			fEmpty = false;
		}
#endif
	} else {
		// Use the opaque rect passed

		rcT = *prcOpaque;
	}

	// Map it map relative in tile coords

	if (!fEmpty) {
		WCoord wxView, wyView;
		gsim.GetViewPos(&wxView, &wyView);
		Rect rcMapRelative;
		rcMapRelative = rcT;
		rcMapRelative.Offset(PcFromWc(wxView), PcFromWc(wyView));
		gtrcMapOpaque.left = TcFromPc(rcMapRelative.left + gcxTile - 1);
		gtrcMapOpaque.top = TcFromPc(rcMapRelative.top + gcyTile - 1);
		gtrcMapOpaque.right = TcFromPc(rcMapRelative.right);
		gtrcMapOpaque.bottom = TcFromPc(rcMapRelative.bottom);

		// Expand the tile rect if the opaque rect is on the screen
		// edge. This way gobs will get opaqued when on screen edge even

		Size sizDib;
		m_pfrmm->GetDib()->GetSize(&sizDib);
		if (rcT.left <= 0)
			gtrcMapOpaque.left = 0;
		if (rcT.top <= 0)
			gtrcMapOpaque.top = 0;
		if (rcT.right >= sizDib.cx)
			gtrcMapOpaque.right = ktcMax;
		if (rcT.bottom >= sizDib.cy)
			gtrcMapOpaque.bottom = ktcMax;

		// Turn this opaque rect on by setting this global

		gptrcMapOpaque = &gtrcMapOpaque;
	} else {
		gptrcMapOpaque = NULL;
	}

	// Get visible gobs

	Gob **ppgobVisible;
	int cgobVisible;
	gsim.FindVisibleGobs(&ppgobVisible, &cgobVisible);

	// Add known invalid gobs to the update map

	Gob **ppgobStop = &ppgobVisible[cgobVisible];
	Gob **ppgobT;
	for (ppgobT = ppgobVisible; ppgobT < ppgobStop; ppgobT++) {
		Gob *pgob = *ppgobT;

		// Invalidate if asked to redraw

		if (pgob->GetFlags() & kfGobRedraw)
			pgob->Invalidate();
	}

	// Mark gobs not already marked for redraw that are touching invalid areas
	// Add this "overflow" to the damage map; this is what is used to create
	// a valid back buffer when scrolling

 	bool fNewInvalid = false;
 	do {
 		fNewInvalid = false;
 		for (ppgobT = ppgobVisible; ppgobT < ppgobStop; ppgobT++) {
 			Gob *pgob = *ppgobT;
			dword ff = pgob->GetFlags();
			if (!(ff & kfGobRedraw)) {
 				if (pupd->IsMapTileRectInvalidAndTrackDamage(&pgob->m_trcBoundingLast, &fNewInvalid))
 					pgob->SetFlags(ff | kfGobRedraw);
 			}
 		}
 	} while (fNewInvalid);

	// Let the base invalidate controls as necessary

	Form::OnUpdateMapInvalidate(pupd, prcOpaque);
}

void SimUIForm::InvalidateDragSelection()
{
	if (gfDragSelecting) {
		WCoord wxView, wyView;
		gsim.GetViewPos(&wxView, &wyView);

		if (gfLassoSelection) {
			// The slow redraw this causes is unavoidable unless we remove lasso as an option.

			WPoint wptLast;
			wptLast.wx = s_awptSelection[0].wx - wxView;
			wptLast.wy = s_awptSelection[0].wy - wyView;
			for (int i = 1; i < s_cwptSelection; i++) {
				WPoint wptNext;
				wptNext.wx = s_awptSelection[i].wx - wxView;
				wptNext.wy = s_awptSelection[i].wy - wyView;
				int x1 = PcFromWc(wptLast.wx);
				int y1 = PcFromWc(wptLast.wy);
				int x2 = PcFromWc(wptNext.wx);
				int y2 = PcFromWc(wptNext.wy);
				Rect rcT;
				rcT.left = _min(x1, x2);
				rcT.top = _min(y1, y2);
				rcT.right = rcT.left + abs(x2 - x1) + 1;
				rcT.bottom = rcT.top + abs(y2 - y1) + 1;
				gpupdSim->InvalidateRect(&rcT);
				wptLast = wptNext;
			}
		} else {
			// PERF: there is a way to make selection drawing faster by only invalidating edges
			// that move, and redrawing dirty cells only on the control layer. Can do if this isn't fast enough.

			int xLeft = PcFromWc(gwrcSelection.left - wxView);
			int yTop = PcFromWc(gwrcSelection.top - wyView);
			int xRight = PcFromWc(gwrcSelection.right - wxView);
			int yBottom = PcFromWc(gwrcSelection.bottom - wyView);

			Rect rcT;
			rcT.Set(xLeft, yTop, xRight, yTop + 1);
			gpupdSim->InvalidateRect(&rcT);
			rcT.Set(xRight - 1, yTop, xRight, yBottom);
			gpupdSim->InvalidateRect(&rcT);
			rcT.Set(xLeft, yBottom - 1, xRight, yBottom);
			gpupdSim->InvalidateRect(&rcT);
			rcT.Set(xLeft, yTop, xLeft + 1, yBottom);
			gpupdSim->InvalidateRect(&rcT);
		}
	}
}

void SimUIForm::ScrollInvalidate(UpdateMap *pupd)
{
	Form::ScrollInvalidate(pupd);
	InvalidateDragSelection();
	gpmm->Invalidate();
}

void SimUIForm::OnPaintBackground(DibBitmap *pbm, UpdateMap *pupd)
{
	if (!s_fReadyForPaint)
		return;

	gsim.DrawBackground(pupd, pbm);
}

void SimUIForm::OnPaint(DibBitmap *pbm)
{
	// We're not ready to paint until the first Update has executed because
	// Update initializes things like the Credits and Power controls.

	if (!s_fReadyForPaint)
		return;

	// Draw everything managed by the Simulation (map, Gobs, Gob UI)

	gsim.Draw(m_pfrmm->GetUpdateMap(), pbm);

	// Draw the selection rectangle or lasso lines, if any

	if (gfDragSelecting) {
		WCoord wxView, wyView;
		gsim.GetViewPos(&wxView, &wyView);

		if (gfLassoSelection) {
			Color clr = GetColor(kiclrWhite);
			WPoint wptLast;
			wptLast.wx = s_awptSelection[0].wx - wxView;
			wptLast.wy = s_awptSelection[0].wy - wyView;
			for (int i = 1; i < s_cwptSelection; i++) {
				WPoint wptNext;
				wptNext.wx = s_awptSelection[i].wx - wxView;
				wptNext.wy = s_awptSelection[i].wy - wyView;
				pbm->DrawLine(PcFromWc(wptLast.wx), PcFromWc(wptLast.wy), PcFromWc(wptNext.wx), PcFromWc(wptNext.wy), clr);
				wptLast = wptNext;
			}
		} else {
			Rect rcT;
			rcT.left = PcFromWc(gwrcSelection.left - wxView);
			rcT.top = PcFromWc(gwrcSelection.top - wyView);
			rcT.right = PcFromWc(gwrcSelection.right - wxView);
			rcT.bottom = PcFromWc(gwrcSelection.bottom - wyView);
			DrawBorder(pbm, &rcT, 1, GetColor(kiclrWhite));
		}
	}

	// Handle placement form

	gpfrmPlace->OnPaintSimUI(pbm);

    // Let the input handler draw what it needs

    m_ppenh->OnPaint(pbm);

	// Call the default Form handling

	Form::OnPaint(pbm);
}

void SimUIForm::OnPaintControls(DibBitmap *pbm, UpdateMap *pupd)
{
	// We're not ready to paint until the first Update has executed because
	// Update initializes things like the Credits and Power controls.

	if (!s_fReadyForPaint)
		return;

	// Give some time to sound servicing

	HostSoundServiceProc();

	// Draw fog

	gsim.DrawFog(pupd, pbm);

	// Draw move indicator

	if (m_nStateMoveTarget == 1) {
		WCoord wxView, wyView;
		gsim.GetViewPos(&wxView, &wyView);
		m_aniMoveTarget.Draw(pbm, PcFromUwc(m_wxMoveTarget) - (PcFromWc(wxView) & 0xfffe), PcFromUwc(m_wyMoveTarget) - (PcFromWc(wyView) & 0xfffe));
	}

	// Give some time to sound servicing

	HostSoundServiceProc();

	// Draw controls

	Form::OnPaintControls(pbm, pupd);

	// Handle Structure Placement controls here

	gpfrmPlace->OnPaintControlsSimUI(pbm, pupd); 

	// Draw stats

#ifdef STATS_DISPLAY
	if (gfShowStats) {
		Font *pfnt = gapfnt[kifntDefault];
		int cy = pfnt->GetHeight();
		int y = cy;
		char szT[80];

		// Display useful info

		// Count of active Gobs

		int cpgob = 0;
		for (Gob *pgobT = ggobm.GetFirstGob(); pgobT != NULL; pgobT = ggobm.GetNextGob(pgobT), cpgob++);
		sprintf(szT, "active Gobs: %d", cpgob);
		pfnt->DrawText(pbm, szT, 1, y);
		y += cy;

		// Count of visible sprites

		sprintf(szT, "bitmaps drawn: %d", gcBitmapsDrawn);
		pfnt->DrawText(pbm, szT, 1, y);
		y += cy;

		// Count of messages per update

		sprintf(szT, "msgs/update: %d", gcMessagesPerUpdate);
		pfnt->DrawText(pbm, szT, 1, y);
		y += cy;

		// Count of updates

		sprintf(szT, "update count: %ld", gsim.GetUpdateCount());
		pfnt->DrawText(pbm, szT, 1, y);
		y += cy;

		// Invalidate

		Rect rcT = m_rc;
		rcT.top = cy;
		rcT.bottom = y;
		InvalidateRect(&rcT);
	}
#endif

#ifdef STATS_DISPLAY
	s_cUpdateRects += gpmfrmm->GetUpdateRectCount();
#endif
}

void SimUIForm::FrameComplete()
{
    Form::FrameComplete();
}

// SimUI handles input events which may effect the whole playfield and/or
// multiple Gobs. It does as much as it can independent of the Gobs but 
// passes context-specific messages to the Gobs when specialized responses 
// are desired.
//
// SimUI's OnPenEvent handler is responsible for
//
// - individual selection/multiple selection
// - input routing, i.e., cooking and passing pertinent events to the 
//   appropriate Gobs. For example, passing selected Gobs SetTarget and
//   invoking Gob::PopupMenu.
//

bool SimUIForm::OnPenEvent(Event *pevt)
{
    // Give the form controls first crack at handling the event.  Form
    // controls include: soft menu button, mode cancel button, status
    // label)

    if (Form::OnPenEvent(pevt)) {
        return true;
    }

    // Debug gob identification

    if (pevt->eType == penHoverEvent) {
#if defined(WIN) && defined(DEBUG)
        WCoord wxTarget, wyTarget;
        gsim.GetViewPos(&wxTarget, &wyTarget);
        TCoord tx = TcFromWc(wxTarget + WcFromPc(pevt->x));
        TCoord ty = TcFromWc(wyTarget + WcFromPc(pevt->y));
        for (Gid gid = ggobm.GetFirstGid(tx, ty); gid != kgidNull;
                gid = ggobm.GetNextGid(gid)) {
            Gob *pgob = ggobm.GetGob(gid);
            if (pgob != NULL) {
                char sz[512];
                pgob->ToString(sz);
                Trace(sz);
            }
        }
#endif
        return true;
    }

    bool fScrollOnly = false;
    if (gpfrmPlace != NULL && gpfrmPlace->IsBusy()) {
        fScrollOnly = true;
    }
	if (gpplrLocal->GetFlags() & kfPlrObserver) {
        fScrollOnly = true;
    }

    return m_ppenh->OnPenEvent(pevt, fScrollOnly);
}

/*
var ar = new Array(64)
for (r = 0; r < 64; r++) {
	c = 0;
	for (y = 50; y >= -50; y--) {
		for (x = 50; x >= -50; x--) {
			d = Math.sqrt(x * x + y * y);
			if (d <= r)
				c++;
		}
	}

	ar[r] = c;
}

for (r = 0; r < 64; r++) {
	WScript.Echo("Range " + r + " fits " + ar[r]);
}

var str = "";
n = 0;
for (r = 0; r < 64; r++) {
	while (n <= ar[r]) {
		if (n % 16 == 0) {
			WScript.Echo(str);
			str = "";
		}
		str += r + ", ";
		n++;
	}
}
*/

// Map unit count to target range, i.e. what circle radius will fit a given count of units

byte gmpcUnitsToRadius[] = 
{
	0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3,
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10, 10,
};

int RadiusFromUnitCount(int cUnits)
{
	TCoord tcRadius;
	if (cUnits >= ARRAYSIZE(gmpcUnitsToRadius)) {
		tcRadius = gmpcUnitsToRadius[ARRAYSIZE(gmpcUnitsToRadius) - 1];
	} else {
		tcRadius = gmpcUnitsToRadius[cUnits];
	}
	return tcRadius;
}

MobileUnitGob *SimUIForm::SetSelectionTargets(Gid gid, WCoord wxTarget, WCoord wyTarget)
{
	// Find a target that is free and close

	WPoint wptT;
	FindNearestFreeTile(TcFromWc(wxTarget), TcFromWc(wyTarget), &wptT, 0);
	wxTarget = wptT.wx;
	wyTarget = wptT.wy;

	// Find targets for all MobileUnits. Targets are a "closely packed" version
	// of the current selection, centered around the midpoint of the selection,
	// placed at the target.

    // First figure out which units are mobile and attempt to retain their formation   

    WCoord wxMin = kwcMax;   
    WCoord wxMax = 0;   
    WCoord wyMin = kwcMax;   
    WCoord wyMax = 0;   

	WCoord wcMoveDistPerUpdate = (WCoord)kwcMax;
	int cgobsSel = 0;
    Gob *pgobT;   
    for (pgobT = ggobm.GetFirstGob(); pgobT != NULL; pgobT = ggobm.GetNextGob(pgobT)) {   
        if ((pgobT->GetFlags() & (kfGobSelected | kfGobMobileUnit)) == (kfGobSelected | kfGobMobileUnit) &&    
		        ((gfGodMode) || pgobT->GetSide() == gpplrLocal->GetSide())) {   

			// Get selection dimensions

            WPoint wpt;
			pgobT->GetCenter(&wpt);
            if (wpt.wx < wxMin)   
                    wxMin = wpt.wx;   
            if (wpt.wx > wxMax)   
                    wxMax = wpt.wx;   
            if (wpt.wy < wyMin)   
                    wyMin = wpt.wy;   
            if (wpt.wy > wyMax)   
                    wyMax = wpt.wy;   
			cgobsSel++;

			// Get min speed per update

			if (pgobT->GetFlags() & kfGobMobileUnit) {
				MobileUnitGob *pmunt = (MobileUnitGob *)pgobT;
				MobileUnitConsts *pmuntc = (MobileUnitConsts *)pmunt->GetConsts();
				if (pmuntc->GetMoveDistPerUpdate() < wcMoveDistPerUpdate)
					wcMoveDistPerUpdate = pmuntc->GetMoveDistPerUpdate();
			}
        }   
    }
	WCoord cwxSel = wxMax - wxMin;
	if (cwxSel <= 0)
		cwxSel = 1;
	WCoord cwySel = wyMax - wyMin;
	if (cwySel <= 0)
		cwySel = 1;

	// The dest scaling rect is different if moving vs. attacking.

	int ctxSel = TcFromWc(wxMax) - TcFromWc(wxMin) + 1;
	int ctySel = TcFromWc(wyMax) - TcFromWc(wyMin) + 1;
	int ctxTarg;
	int ctyTarg;
	if (gid == kgidNull) {
		// Pick a destination scaling rect for movement

		ctxTarg = ctxSel;
		ctyTarg = ctySel;

		// Pack the rect if it is <= 50% full

		if (cgobsSel <= (ctxSel * ctySel + 1) / 2) {
			int ctxT = ctxSel;
			int ctyT = ctySel;
			while (true) {
				// Remember last 

				ctxTarg = ctxT;
				ctyTarg = ctyT;

				// Try smaller size, decrease whichever size is greater

				if (ctxT > ctyT) {
					ctxT--;
				} else {
					ctyT--;
				}

				if (ctxT * ctyT < cgobsSel + cgobsSel / 3)
					break;
			}
		}

	} else {
		// Pick a destination scaling rect for attacking

		Gob *pgob = ggobm.GetGob(gid);
		ctxTarg = 1;
		ctyTarg = 1;
		if (pgob != NULL) {
			TRect trc;
			pgob->GetTileRect(&trc);
			ctxTarg = trc.right - trc.left;
			ctyTarg = trc.bottom - trc.top;
		}

		// Make the outside edge away from the gob; this hardwires some accounting
		// for some firing distance, gives a larger edge for gobs to move to

		ctxTarg += 2;
		ctyTarg += 2;
	}

	WCoord cwxTarg = WcFromTc(ctxTarg - 1);
	if (cwxTarg == 0)
		cwxTarg = 1;
	WCoord cwyTarg = WcFromTc(ctyTarg - 1);
	if (cwyTarg == 0)
		cwyTarg = 1;

	// Figure out the range of this target

	TCoord tcTargetRadius = RadiusFromUnitCount(cgobsSel);

    // Find src center   

    WCoord wxCenterSrc = wxMin + (wxMax - wxMin) / 2;   
    WCoord wyCenterSrc = wyMin + (wyMax - wyMin) / 2;   

	// Find map size

    Size sizMapTiles; 
    gsim.GetLevel()->GetTileMap()->GetTCoordMapSize(&sizMapTiles); 
	WCoord cwxMap = WcFromTc(sizMapTiles.cx);
	WCoord cwyMap = WcFromTc(sizMapTiles.cy);

	// Move & keep formation, or target destination if not a mobile unit

	Gob *pgobTarget = ggobm.GetGob(gid);

	MobileUnitGob *pmuntSetTarget = NULL;
	for (pgobT = ggobm.GetFirstGob(); pgobT != NULL; pgobT = ggobm.GetNextGob(pgobT)) {
		dword ff = pgobT->GetFlags();
		if ((ff & kfGobSelected) && 
				((gfGodMode) || pgobT->GetSide() == gpplrLocal->GetSide() || (pgobTarget != NULL && pgobTarget->GetType() == kgtReplicator))) {

			// If it's a mobile gob, give it a move command to a scaled coordinate

			if (ff & kfGobMobileUnit) {
				// Calc dst

 				MobileUnitGob *pmunt = (MobileUnitGob *)pgobT;
 				WPoint wptSrc;
				pmunt->GetCenter(&wptSrc);
				WCoord wxDst;
				if (wptSrc.wx - wxCenterSrc >= 0) {
 					wxDst = (WCoord)(((long)(wptSrc.wx - wxCenterSrc) * cwxTarg * 8 + (cwxSel * 4)) / (cwxSel * 8) + wxTarget);
				} else {
 					wxDst = (WCoord)(((long)(wptSrc.wx - wxCenterSrc) * cwxTarg * 8 - (cwxSel * 4)) / (cwxSel * 8) + wxTarget);
				}
 				if (wxDst < 0)
 					wxDst = 0;
 				if (wxDst >= cwxMap)
 					wxDst = cwxMap - 1;
 				WCoord wyDst;
				if (wptSrc.wy - wyCenterSrc >= 0) {
					wyDst = (WCoord)(((long)(wptSrc.wy - wyCenterSrc) * cwyTarg * 8 + (cwySel * 4)) / (cwySel * 8) + wyTarget);
				} else {
					wyDst = (WCoord)(((long)(wptSrc.wy - wyCenterSrc) * cwyTarg * 8 - (cwySel * 4)) / (cwySel * 8) + wyTarget);
				}
 				if (wyDst < 0)
 					wyDst = 0;
 				if (wyDst >= cwyMap)
 					wyDst = cwyMap - 1;
				if (gfGodMode)
					pmunt->ClearAction();

				// Need to clean up the targets.
				// 1. Targets can be either in open terrain or on the unit being targetted,
				//    not on other structures (other munts is ok)
				// 2. For any targets on the munt being targetted, if it's a structure it needs
				//    to be checked for accessibility
				// 3. Adjust the target radius to be on the same side of any blockage

				// Clean up dest if attacking

				if (pgobTarget != NULL) {
					// Find the closest tile to this that is unblocked by terrain
					// Ok if on a structure... which structure is important, that is checked next

					WPoint wptT;
					FindNearestFreeTile(TcFromWc(wxDst), TcFromWc(wyDst), &wptT, 0);
					wxDst = wptT.wx;
					wyDst = wptT.wy;

					// Calc these now as wxDst, wyDst may have changed

					TCoord txT = TcFromWc(wxDst);
					TCoord tyT = TcFromWc(wyDst);

					// If dest is on top of a structure make sure that is the target and dest
					// is accessible

					Assert(pgobTarget->GetFlags() & kfGobUnit);
					UnitGob *puntTarget = (UnitGob *)pgobTarget;
					Gob *pgobT = ggobm.GetShadowGob(txT, tyT);
					if (pgobT != NULL) {
                        // The destination is on top of a structure. Need to
                        // check if this is ok If it isn't ok, get a new point
                        // that is accessible. If it is ok, make sure the
                        // position is accessible.

						Assert(pgobT->GetFlags() & kfGobStructure);
						StructGob *pstruT = (StructGob *)pgobT;
						if (pgobT != pgobTarget) {
                            // Targetting something but wxDst, wyDst on a
                            // structure we're not targetting.  Get a valid
                            // attack point from the real target

							WPoint wptT;
							puntTarget->GetAttackPoint(&wptT);
							wxDst = wptT.wx;
							wyDst = wptT.wy;
						} else {
                            // wxDst, wyDst is on top of the structure we are
                            // targetting which is fine. Get an accessible
                            // position if the one we have isn't accessible

							if (!pstruT->IsAccessible(txT, tyT)) {
								WPoint wptT;
								puntTarget->GetAttackPoint(&wptT);
								wxDst = wptT.wx;
								wyDst = wptT.wy;
							}
						}
					}
				} else {
					// Not attacking, just moving.
                    // Find a clear terrain spot that isn't terrain blocked and
                    // not on a structure

					WPoint wptT;
					FindNearestFreeTile(TcFromWc(wxDst), TcFromWc(wyDst), &wptT, kbfStructure);
					wxDst = wptT.wx;
					wyDst = wptT.wy;
				}

                // Adjust the target radius so we end up on the same side of
                // any blockage

				TCoord tcTargetRadiusT = tcTargetRadius;
				TerrainMap *ptrmap = gsim.GetLevel()->GetTerrainMap();
				TCoord txTo = TcFromWc(wxTarget);
				TCoord tyTo = TcFromWc(wyTarget);
				TCoord txFrom = TcFromWc(wxDst);
				TCoord tyFrom = TcFromWc(wyDst);
				TCoord txFree, tyFree;
				if (ptrmap->FindLastUnoccupied(txTo, tyTo, txFrom, tyFrom, &txFree, &tyFree)) {
					if (txFree != txFrom || tyFree != tyFrom) {
						wxDst = WcFromTc(txFree) + kwcTileHalf;
						wyDst = WcFromTc(tyFree) + kwcTileHalf;
						int dtx = abs(txFree - txTo);
						if (dtx < ARRAYSIZE(gmpDistFromDxy)) {
							int dty = abs(tyFree - tyTo);
							if (dty < ARRAYSIZE(gmpDistFromDxy)) {
								tcTargetRadiusT = gmpDistFromDxy[dtx][dty];
							}
						}
					}
				}

				// Send command

 				pmunt->SetTarget(gid, wxDst, wyDst, wxTarget, wyTarget, tcTargetRadiusT, wcMoveDistPerUpdate);
				pmuntSetTarget = pmunt;
			}
			
            // Else give it a set target command with the original target
            // coordinate

			else if (ff & kfGobUnit) {
				UnitGob *punt = (UnitGob *)pgobT;
				punt->SetTarget(gid, wxTarget, wyTarget, 0, wcMoveDistPerUpdate);
			}
		}
	}

	return pmuntSetTarget;
}

void SimUIForm::ClearSelection()
{
	s_wptSelect1.wx = 0;
	s_wptSelect1.wy = 0;
	s_wptSelect2 = s_wptSelect1;
	s_cwptSelection = 0;
	gwrcSelection.Set(s_wptSelect1, s_wptSelect2);
	gsim.ClearGobSelection();
}

Gob *SimUIForm::HitTestGob(int x, int y, bool fFinger, WCoord *pwx,
        WCoord *pwy, bool *pfHitSurrounding)
{
	WCoord wxTarget, wyTarget;
	gsim.GetViewPos(&wxTarget, &wyTarget);
	wxTarget += WcFromPc(x);
	wyTarget += WcFromPc(y);

	// Pin to edges. Gobs created off the map will cause corruption

	TCoord ctx, cty;
	ggobm.GetMapSize(&ctx, &cty);
	if (TcFromWc(wxTarget) >= ctx)
		wxTarget = WcFromTc(ctx - 1);
	if (TcFromWc(wyTarget) >= cty)
		wyTarget = WcFromTc(cty - 1);

    *pwx = wxTarget;
    *pwy = wyTarget;

    if (fFinger) {
        return gsim.FingerHitTest(wxTarget, wyTarget, kfGobActive | kfGobUnit,
                pfHitSurrounding);
    } else {
        Gob *pgobHit = NULL;
        Enum enm;
        gsim.HitTest(&enm, wxTarget, wyTarget, kfGobActive | kfGobUnit,
                &pgobHit);
        *pfHitSurrounding = false;
        return pgobHit;
    }
}

void SimUIForm::MoveOrAttackOrSelect(int x, int y, dword ff)
{
	WCoord wxTarget, wyTarget;
    bool fHitSurrounding;
    Gob *pgobHit = HitTestGob(x, y, false, &wxTarget, &wyTarget,
            &fHitSurrounding);
    MoveOrAttackOrSelect(pgobHit, wxTarget, wyTarget, ff);
}

// This routine is called in two situations.
// 1. On pen down to select the unit tapped on (fSelectOnly == true)
// 2. On pen up to attack the unit tapped on or to move to the location tapped on (fSelectOnly == false)

const int kctDoubleTapDelay = 100; // TUNE:

void SimUIForm::MoveOrAttackOrSelect(Gob *pgobHit, WCoord wxTarget,
        WCoord wyTarget, dword ff)
{
    // TODO: break up this giant routine

    if (pgobHit == NULL) {
		if (ff & kfMasMove) {
			MobileUnitGob *pmunt = SetSelectionTargets(kgidNull, wxTarget, wyTarget);
			if (pmunt != NULL) {
				if (m_nStateMoveTarget != 0)
					m_pfrmm->GetUpdateMap()->InvalidateTile(TcFromWc(m_wxMoveTarget), TcFromWc(m_wyMoveTarget));
				m_aniMoveTarget.Start(0, 0, 0);
				m_wxMoveTarget = WcTrunc(wxTarget) + kwcTileHalf;
				m_wyMoveTarget = WcTrunc(wyTarget) + kwcTileHalf;
				m_nStateMoveTarget = 1;
				gsndm.PlaySfx(SfxFromCategory(((MobileUnitConsts *)gapuntc[pmunt->GetUnitType()])->sfxcMove));
			}

#ifdef RALLY_POINTS
			// TOTAL HACK:
            // If the player has HRC or VTS selected and they've tapped the
            // ground set that point as the structure's rally point. Of course
            // this should be visualized, etc. Ah, if only we had time for such
            // things.

			for (Gob *pgobT = ggobm.GetFirstGob(); pgobT != NULL; pgobT = ggobm.GetNextGob(pgobT)) {
				if ((pgobT->GetFlags() & (kfGobStructure | kfGobSelected)) != (kfGobStructure | kfGobSelected))
					continue;
				BuilderGob *pbldr = (BuilderGob *)pgobT;

				// We know that if an enemy structure is selected then a local player's
				// structure can't also be selected.

				if (pgobT->GetOwner() != gpplrLocal)
					break;

				// VTS or HRC?

				if (!((1UL << pbldr->GetUnitType()) & (kumVehicleTransportStation | kumHumanResourceCenter)))
					break;

				pbldr->SetRallyPoint(TcFromWc(wxTarget), TcFromWc(wyTarget));
				WCoord wxT = WcTrunc(wxTarget) + kwcTileHalf;
				WCoord wyT = WcTrunc(wyTarget) + kwcTileHalf;
				CreateAnimGob(wxT, wyT, kfAnmDeleteWhenDone | kfAnmSmokeFireLayer, NULL, g_panidMoveTarget);
				ShowAlert("Rally point set");
				break;
			}
#endif
		}
		return;
	}

    // Replicator is a special case. When it is specified as a target the
    // selected units are directed to move to its input.

	if (pgobHit->GetType() == kgtReplicator) {
		if ((ff & (kfMasMove | kfMasAttack)) == 0) {
			return;
        }

		MobileUnitGob *pmunt = SetSelectionTargets(pgobHit->GetId(), wxTarget, wyTarget);
		if (pmunt != NULL)
			gsndm.PlaySfx(SfxFromCategory(((MobileUnitConsts *)gapuntc[pmunt->GetUnitType()])->sfxcMove));
		return;
	}


	// God mode is allowed only if gfGodMode is on when playing single player

	bool fGodMode = false;
	if (gfGodMode)
		fGodMode = true;

	// Selection?

	if (IsSelectionCommand(pgobHit)) {
		static long s_tLastSelected = 0;
		static Gob *s_pgobLastSelected = NULL;

		if (ff & kfMasSelect) {
			bool fAlreadySelected = (pgobHit->GetFlags() & kfGobSelected) != 0;

            // Selection. Cancel selection for units elsewhere, set selection
            // for this unit

            // If the unit is already selected and the last selection occured
            // in within the double-tap window then select all units of the
            // same type on-screen.  UNDONE: this may not be the best place to
            // do this.

			if (fAlreadySelected && HostGetTickCount() - s_tLastSelected < kctDoubleTapDelay && s_pgobLastSelected == pgobHit) {
                SelectSameUnitTypes(pgobHit, false);
			} else {
				gsim.SetGobSelected(pgobHit);
			}
			s_tLastSelected = HostGetTickCount();
			s_pgobLastSelected = pgobHit;

			// Play sfx

			if (!gfGodMode) {
				if (pgobHit->GetFlags() & kfGobStructure) {
					gsndm.PlaySfx(((StructConsts *)gapuntc[((StructGob *)pgobHit)->GetUnitType()])->sfxSelect);
				} else {
					if (gfGodMode || pgobHit->GetSide() == gpplrLocal->GetSide())
						gsndm.PlaySfx(SfxFromCategory(((MobileUnitConsts *)gapuntc[((UnitGob *)pgobHit)->GetUnitType()])->sfxcSelect));
				}
			}
		}

        if (ff & kfMasShowMenu) {
			if (pgobHit->GetFlags() & kfGobStructure) {
                ShowUnitMenu(pgobHit);
			}
		}
	} else if (ff & kfMasAttack) {
        // An attack. Reset the wxTarget, wyTarget to be the middle of the gob
        // and issue targets

		UnitGob *puntHit = (UnitGob *)pgobHit;
		WPoint wptTarget;
		puntHit->GetAttackPoint(&wptTarget);
		wxTarget = wptTarget.wx;
		wyTarget = wptTarget.wy;
		MobileUnitGob *pmunt = SetSelectionTargets(puntHit->GetId(), wxTarget, wyTarget);
		if (pmunt != NULL)
			gsndm.PlaySfx(SfxFromCategory(((MobileUnitConsts *)gapuntc[((UnitGob *)pmunt)->GetUnitType()])->sfxcAttack));
	}
}

bool SimUIForm::IsSelectionCommand(Gob *pgobHit)
{
	// God mode is allowed only if gfGodMode is on when playing single player

	bool fGodMode = false;
	if (gfGodMode)
		fGodMode = true;

	// Figure out if this is a selection or an attack command:
	//
	// Player can select same side units
	// Player can select enemy units if no friendly units are selected
	// Player can attack enemy units
	// GodMode can select any unit if that doesn't imply an attack (from
    // either side)
	// GodMode can attack any side if there are selected enemy units

	// If there are any selected units that can attack pgobHit, it may be an
    // attack.

	bool fOwnSelection = false;
	bool fSelection = true;
	Side sideHit = pgobHit->GetSide();
	for (Gob *pgobT = ggobm.GetFirstGob(); pgobT != NULL; pgobT = ggobm.GetNextGob(pgobT)) {
		if ((pgobT->GetFlags() & (kfGobUnit | kfGobSelected)) != (kfGobUnit | kfGobSelected))
			continue;
		UnitGob *puntT = (UnitGob *)pgobT;
		if (puntT->GetOwner() == gpplrLocal)
			fOwnSelection = true;
		if (puntT->IsValidTarget(pgobHit)) {
			fSelection = false;
			break;
		}
	}

    // If not in god mode and the local player does not own any of the already
    // selected units then just select the new unit

	if ((!fGodMode) && (!fOwnSelection)) {
        fSelection = true;
    }

    return fSelection;
}

bool SimUIForm::HasSelectedUnits()
{
    Gob *pgobT = ggobm.GetFirstGob();
	for (; pgobT != NULL; pgobT = ggobm.GetNextGob(pgobT)) {
		if ((pgobT->GetFlags() & (kfGobUnit | kfGobSelected | kfGobStructure))
                != (kfGobUnit | kfGobSelected)) {
			continue;
        }
		if (((UnitGob *)pgobT)->GetOwner() == gpplrLocal) {
            return true;
        }
    }
    return false;
}

void SimUIForm::ShowUnitMenu(Gob *pgob) {
    if (pgob != NULL && (pgob->GetFlags() & kfGobUnit) != 0) {
        // Don't popup menu if the player doesn't own the Gob (unless in
        // God mode). God mode is limited for multiplayer games.
        if ((!gfGodMode) && 
                pgob->GetSide() != gpplrLocal->GetSide()) {
            return;
        }
        pgob->PopupMenu();
    }
}

void SimUIForm::SelectSameUnitTypes(Gob *pgob, bool fSfx)
{
    if ((pgob->GetFlags() & (kfGobUnit | kfGobStructure)) != kfGobUnit) {
        return;
    }

    WCoord wxViewT, wyViewT;
    gsim.GetViewPos(&wxViewT, &wyViewT);
    TCoord txView = TcFromWc(wxViewT);
    TCoord tyView = TcFromWc(wyViewT);

    Size sizT;
    ggame.GetPlayfieldSize(&sizT);
    TCoord txRight = TcFromWc(wxViewT + WcFromPc(sizT.cx + gcxTile - 1));
    TCoord tyBottom = TcFromWc(wyViewT + WcFromPc(sizT.cy + gcyTile - 1));

    TRect trc;
    trc.Set(txView, tyView, txRight, tyBottom);
    gsim.SelectSameUnitTypes((UnitGob *)pgob, &trc);

    if (gfGodMode || pgob->GetSide() == gpplrLocal->GetSide()) {
        gsndm.PlaySfx(SfxFromCategory(((MobileUnitConsts *)
                gapuntc[((UnitGob *)pgob)->GetUnitType()])->sfxcSelect));
    }
}

void SimUIForm::CalcLevelSpecificConstants()
{
}

// OPT: this isn't hideous but could be made faster

bool PtInPolygon(WPoint *awpt, int cwpt, WCoord wx, WCoord wy)
{
	bool fInside = false;
	WPoint *pwptI = &awpt[0];
	WPoint *pwptJ = &awpt[cwpt - 1];
	WPoint *pwptEnd = pwptJ + 1;

    for (int i = 0; pwptI < pwptEnd; pwptI++) {
		if ((((pwptI->wy <= wy) && (wy < pwptJ->wy)) || ((pwptJ->wy <= wy) && (wy < pwptI->wy))) &&
				(wx < (pwptJ->wx - pwptI->wx) * (wy - pwptI->wy) / (pwptJ->wy - pwptI->wy) + pwptI->wx))
			fInside = !fInside;
		pwptJ = pwptI;
    }

    return fInside;
}

//
// MiniMapControl
//

MiniMapControl *gpmm;

MiniMapControl::MiniMapControl()
{
	m_pbm = NULL;
	m_cxyBorder = 0;
	m_wfMm = 0;
	m_xOff = 0;
	m_yOff = 0;
	m_tInvalidateLast = 0;
}

MiniMapControl::~MiniMapControl()
{
	gpmm = NULL;
	delete m_pbm;
}

int MiniMapControl::CalcWidth()
{
	// Figure out scale based on playfield size

	Size siz;
	ggame.GetPlayfieldSize(&siz);
	int nScale = 1;
	if (siz.cx >= 240)
		nScale = 2;

	// Width

	Size sizMap;
	gsim.GetLevel()->GetTileMap()->GetMapSize(&sizMap);
	return sizMap.cx / (gcxTile / nScale) + 1;
}

bool MiniMapControl::Init(Form *pfrm, IniReader *pini, FindProp *pfind)
{
	// Base initialization

	if (!Control::Init(pfrm, pini, pfind))
		return false;

	// idc (x y cx cy) border

	char szBorder[32];
	int cArgs = pini->GetPropertyValue(pfind, "%*d (%*d %*d %*d %*d) %s", szBorder);

	// Remember the size of the input UI area since our border drawing needs it

	Size sizDib;
	ggame.GetInputUIForm()->GetFormMgr()->GetDib()->GetSize(&sizDib);
	m_cyInputUI = sizDib.cy;

	// Our even aligned blts depend on having a border for odd width maps

	m_cxyBorder = 1;

	// Figure out scale based on playfield size

	Size siz;
	ggame.GetPlayfieldSize(&siz);
	m_nScale = 1;
	if (siz.cx >= 240 && siz.cx < 320) {
		if (siz.cy > siz.cx)
			m_nScale = 2;
	}
	if (siz.cx >= 320)
		m_nScale = 2;

	// Size. Maps bigger than the original 64 x 64 limit use 1 pixel per tile
	// so the minimap stays the size players know (at most 127 pixels).

	Size sizMap;
	gsim.GetLevel()->GetTileMap()->GetMapSize(&sizMap);
	if (sizMap.cx / gcxTile > ktcMapOriginal || sizMap.cy / gcyTile > ktcMapOriginal)
		m_nScale = 1;
	gsim.SetMiniMapScale(m_nScale);
	m_rc.left = m_rc.right - ((sizMap.cx / (gcxTile / m_nScale)) + m_cxyBorder);
	m_rc.bottom = m_rc.top + (sizMap.cy / (gcyTile / m_nScale)) + m_cxyBorder;

	// The map is aligned to an edge. The edge is even, and the dib starts on
	// an even address, so we want the width to be even so our blts are aligned.
	// If the map is odd, draw the border inside the dib to make it even. If the
	// map is even, draw the border outside the dib.

	int cx = m_rc.Width();
	if (cx & 1) {
		m_wfMm &= ~kfMmVertBorderInside;
		cx -= m_cxyBorder;
		m_xOff = 0;
		m_yOff = m_cxyBorder;
	} else {
		m_wfMm |= kfMmVertBorderInside;
		m_xOff = m_cxyBorder;
		m_yOff = m_cxyBorder;
	}

	// Alloc a dib to hold it

	m_pbm = CreateDibBitmap(NULL, cx, m_rc.Height());
	if (m_pbm == NULL)
		return false;

	// Draw internal borders

	int x = 0;
	int y = 0;
	if (m_cxyBorder != 0) {
		Color clrBorder = GetColor(kiclr0CyanSide);
		if (m_wfMm & kfMmVertBorderInside) {
			m_pbm->Fill(x + m_cxyBorder, y, m_rc.Width() - m_cxyBorder, m_cxyBorder, clrBorder);
			m_pbm->Fill(x, y, m_cxyBorder, m_rc.Height() - m_cyInputUI + 1, clrBorder);
			m_pbm->Fill(x, y + m_rc.Height() - m_cyInputUI + 1, m_cxyBorder, m_rc.Height(), GetColor(kiclrBlack));
		} else {
			m_pbm->Fill(x, y, m_rc.Width(), m_cxyBorder, clrBorder);
		}
	}

	// Stuff constants

	TileMap *ptmap = gsim.GetLevel()->GetTileMap();
	MiniTileSetHeader *pmtseth = ptmap->GetMiniTileSetHeader(m_nScale);
	m_pbTileData = (byte *)(pmtseth + 1);
	m_pwTileMap = ptmap->m_pwMapData;
	m_pbFogMap = gsim.GetLevel()->GetFogMap()->GetMapPtr();
	ggobm.GetMapSize(&m_ctx, &m_cty);
	m_cbRowBytes = m_pbm->GetRowBytes();
	m_clrBlack = (byte)GetColor(kiclrBlack);
	m_clrWhite = (byte)GetColor(kiclrWhite);
	m_clrGalaxite = (byte)GetColor(kiclrGalaxite);
	for (Side sideT = ksideNeutral; sideT < kcSides; sideT++)
		m_aclrSide[sideT] = (byte)GetSideColor(sideT);

	// Calc powered radar flag

	if (!CalcPoweredRadar())
		Redraw();

	// Remember this object globally

	gpmm = this;
	return true;
}

void MiniMapControl::OnPaint(DibBitmap *pbm)
{
	Rect rcForm;
	m_pfrm->GetRect(&rcForm);
	int x = m_rc.left + rcForm.left;
	int y = m_rc.top + rcForm.top;

	// Lay down bits

	Size siz;
	m_pbm->GetSize(&siz);
	Rect rcSrc;
	rcSrc.Set(0, 0, siz.cx, siz.cy);

	if (m_wfMm & kfMmVertBorderInside) {
		pbm->Blt(m_pbm, &rcSrc, x, y);
	} else {
		pbm->Blt(m_pbm, &rcSrc, x + m_cxyBorder, y);
		pbm->Fill(x, y, m_cxyBorder, m_rc.Height() - m_cyInputUI + 1, GetColor(kiclr0CyanSide));
		pbm->Fill(x, y + m_rc.Height() - m_cyInputUI + 1, m_cxyBorder, m_rc.Height(), GetColor(kiclrBlack));
	}

	// Draw box showing the full-screen visible area of the map

	WCoord wxViewT, wyViewT;
	gsim.GetViewPos(&wxViewT, &wyViewT);
	short xView = PcFromUwc(wxViewT) & 0xfffe;
	short yView = PcFromUwc(wyViewT) & 0xfffe;
	WCoord wxView = WcFromUpc(xView);
	WCoord wyView = WcFromUpc(yView);

	int cxTile = gcxTile / m_nScale;
	int cyTile = gcyTile / m_nScale;

	Rect rc;
	rc.left = x + m_cxyBorder + PcFromUwc(wxView) / cxTile;
	rc.top = y + m_cxyBorder + PcFromUwc(wyView) / cyTile;
	ggame.GetPlayfieldSize(&siz);
	rc.right = rc.left + (siz.cx + cxTile - 1) / cxTile;
	rc.bottom = rc.top + (siz.cy + cyTile - 1) / cyTile;
	DrawBorder(pbm, &rc, 1, GetColor(kiclrWhite));
}

int MiniMapControl::OnHitTest(Event *pevt)
{
    // Don't use finger hit testing on the minimap control; this way
    // all input over the map goes to the map.

    if (pevt->ff & kfEvtFinger) {
        pevt->ff = ~kfEvtFinger;
        int result = Control::OnHitTest(pevt);
        pevt->ff |= kfEvtFinger;
        return result;
    }
    return Control::OnHitTest(pevt);
}

//TUNE:
#define kctMiniMapInvalidateRate 25

void MiniMapControl::Update()
{	
    // Gets called at Update time. If the minimap is dirty and it's been at
    // least kctMiniMapUpdateRate ticks since the last invalidate, invalidate
    // it now. This gives us immediate invalidates in many cases, but ensures
    // there is at least kctMiniMapUpdateRate ticks between invalidates.

    long tCurrent = gtimm.GetTickCount();
	if (m_wfMm & kfMmRedraw) {
		if (m_tInvalidateLast == 0 || tCurrent - m_tInvalidateLast >= kctMiniMapInvalidateRate) {
			m_tInvalidateLast = tCurrent;
			m_wfMm &= ~kfMmRedraw;
			Invalidate();
		}
	}

// Enough time to give the second finger time to come down, and not long
// enough to make the minimap jerky

#define kctPenDownTimeout 25

    if (m_wfMm & kfMmPenDownTimeout) {
        if (tCurrent - m_tPenDown >= kctPenDownTimeout) {
            m_wfMm &= ~kfMmPenDownTimeout;
            OnPenEvent2(&m_evtPenDown);
        }
    }
}

void MiniMapControl::OnBreakCapture()
{
    // Forget about the pen down, if there is one

    m_wfMm &= ~kfMmPenDownTimeout;
    Trace("Breaking capture");
}

void MiniMapControl::Redraw()
{
	TRect trc;
	trc.Set(0, 0, m_ctx, m_cty);
	RedrawTRect(&trc);
}

void MiniMapControl::OnPenEvent(Event *pevt)
{
    // Process the pen down in waiting if there is a pen up

    if (pevt->eType == penUpEvent) {
        if (m_wfMm & kfMmPenDownTimeout) {
            m_wfMm &= ~kfMmPenDownTimeout;
            OnPenEvent2(&m_evtPenDown);
        }
    }

	if (pevt->eType != penDownEvent && pevt->eType != penMoveEvent) {
		return;
    }

#if defined(IPHONE) || defined(__IPHONEOS__) || defined(__ANDROID__)
    // If already waiting for pen down timeout, then just update the x,y

    if (m_wfMm & kfMmPenDownTimeout) {
        m_evtPenDown.x = pevt->x;
        m_evtPenDown.y = pevt->y;
        return;
    }

    // If pen down event, remember it and don't process it for a little bit.
    // This gives enough time to forget about the pen down if the second
    // finger goes down and the control is forced to give up capture.

    if (pevt->eType == penDownEvent) {
        m_tPenDown = gtimm.GetTickCount();
        m_evtPenDown = *pevt;
        m_wfMm |= kfMmPenDownTimeout;
        return;
    }
#endif

    OnPenEvent2(pevt);
}

void MiniMapControl::OnPenEvent2(Event *pevt)
{
	static int s_xPenDown;
	static int s_yPenDown;
	static WCoord s_wxViewDown;
	static WCoord s_wyViewDown;

	Rect rcForm;
	m_pfrm->GetRect(&rcForm);

	int xPen = pevt->x - (rcForm.left + m_rc.left + m_cxyBorder);
	int yPen = pevt->y - (rcForm.top + m_rc.top + m_cxyBorder);

	WCoord wxView, wyView;
	gsim.GetViewPos(&wxView, &wyView);

	Size sizPlayfield;
	ggame.GetPlayfieldSize(&sizPlayfield);

	if (pevt->eType == penDownEvent) {
		// Calc the rectangle (in minimap pixel coordinates) bounding the map
		// area currently being viewed.

		Rect rcView;
		rcView.left = TcFromWc(wxView) * m_nScale;
		rcView.top = TcFromWc(wyView) * m_nScale;
		rcView.right = rcView.left + (((sizPlayfield.cx + gcxTile - 1) / gcxTile) * m_nScale);
		rcView.bottom = rcView.top + (((sizPlayfield.cy + gcyTile - 1) / gcyTile) * m_nScale);

#if 0
// This feels super sloppy, plus it isn't neeed for fine movement anymore,
// since the map scrolls nicely with a finger directly
        // If this is finger UI, make the rect larger
        
        if (pevt->ff & kfEvtFinger) {
            int cxInflate = (gcxTile * 2 - rcView.Width()) / 2;
            if (cxInflate < 0) {
                cxInflate = 0;
            }
            int cyInflate = (gcyTile * 2 - rcView.Height()) / 2;
            if (cyInflate < 0) {
                cyInflate = 0;
            }
            rcView.Inflate(cxInflate, cyInflate);
        }
#endif
        
		// Pen down inside the view rect?

		if (!rcView.PtIn(xPen, yPen)) {

			// No, set the view to be centered around the point tapped

			TCoord tx = xPen / m_nScale;
			TCoord ty = yPen / m_nScale;

			wxView = WcFromTc(tx - (sizPlayfield.cx / gcxTile) / 2);
			wyView = WcFromTc(ty - ((sizPlayfield.cy + gcyTile - 1) / gcyTile) / 2);
		}

		s_xPenDown = xPen;
		s_yPenDown = yPen;
		s_wxViewDown = wxView;
		s_wyViewDown = wyView;
	} else {
        // In int, then clamped: dragging far outside the minimap would
        // otherwise wrap the 16 bit world coordinates around
        int dx = xPen - s_xPenDown;
		long wxT = s_wxViewDown + (long)(dx / m_nScale) * kwcTile;
        wxT += (dx % m_nScale) * kwcTile / m_nScale;
        int dy = yPen - s_yPenDown;
		long wyT = s_wyViewDown + (long)(dy / m_nScale) * kwcTile;
        wyT += (dy % m_nScale) * kwcTile / m_nScale;
		wxView = (WCoord)_max(-(long)kwcMax, _min((long)kwcMax, wxT));
		wyView = (WCoord)_max(-(long)kwcMax, _min((long)kwcMax, wyT));
	}

	gsim.SetViewPos(wxView, wyView);
	Invalidate();
}

bool MiniMapControl::CalcPoweredRadar()
{
	// gpmm uninitialized?

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-undefined-compare"
#endif

	if (this == NULL)
		return false;

#ifdef __clang__
#pragma clang diagnostic pop
#endif

	// Remember if the local player has fully powered Radar. This determines whether
	// enemy units show on the mini-map.

	bool fHasPoweredRadar = gpplrLocal->GetUnitCount(kutRadar) > 0 && gpplrLocal->GetPowerDemand() <= gpplrLocal->GetPowerSupply();
	word wfMm = fHasPoweredRadar ? kfMmHasPoweredRadar : 0;
	if ((wfMm ^ m_wfMm) != 0) {
		m_wfMm &= ~kfMmHasPoweredRadar;
		m_wfMm |= wfMm;
		Redraw();
		return true;
	}
	return false;
}

void MiniMapControl::RedrawTile(TCoord tx, TCoord ty)
{
	TRect trc;
	trc.left = tx;
	trc.top = ty;
	trc.right = tx + 1;
	trc.bottom = ty + 1;
	RedrawTRect(&trc);
}

void MiniMapControl::RedrawTRect(TRect *ptrc)
{
	// gpmm uninitialized?

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-undefined-compare"
#endif

	if (this == NULL)
		return;

#ifdef __clang__
#pragma clang diagnostic pop
#endif

	// Redraw this rect

	byte *pbDst = m_pbm->GetBits() + (long)ptrc->top * m_cbRowBytes * m_nScale +
			ptrc->left * m_nScale + (long)m_yOff * m_cbRowBytes + m_xOff;
	int cbDstReturn = m_cbRowBytes - ptrc->Width() * m_nScale;
	long offset = (long)ptrc->top * m_ctx + ptrc->left;
	byte *pbFogMap = m_pbFogMap + offset;
	word *pwTileMap = m_pwTileMap + offset;
	int ctReturn = m_ctx - ptrc->Width();

	if (m_nScale == 1) {
		for (TCoord ty = ptrc->top; ty < ptrc->bottom; ty++) {
			for (TCoord tx = ptrc->left; tx < ptrc->right; tx++, pbFogMap++, pwTileMap++) {
				// Fogged?

				if (IsFogOpaque(*pbFogMap)) {
					*pbDst++ = m_clrBlack;
					continue;
				}

				// Not fogged; remember to redraw the minimap to the screen next timer

				m_wfMm |= kfMmRedraw;

				// Unit gob?

				UnitGob *punt = ggobm.GetUnitGob(tx, ty);

				// don't show inactive munts

				if (punt != NULL) {
					dword wf = punt->GetFlags();
					if ((wf & (kfGobMobileUnit | kfGobActive)) != (kfGobMobileUnit))  {
						if (wf & kfGobSelected) {
							*pbDst++ = m_clrWhite;
						} else {
							*pbDst++ = m_aclrSide[punt->GetSide()];
						}
						continue;
					}
				}

				// Galaxite?

				if (HasGalaxite(*pbFogMap)) {
					*pbDst++ = m_clrGalaxite;
					continue;
				}

				// Tile

				int nTile = (BigWord(*pwTileMap) & 0x7fc);
				*pbDst++ = m_pbTileData[nTile >> 2];
				continue;
			}
			pwTileMap += ctReturn;
			pbFogMap += ctReturn;
			pbDst += cbDstReturn;
		}
	} else if (m_nScale == 2) {
		for (TCoord ty = ptrc->top; ty < ptrc->bottom; ty++) {
			for (TCoord tx = ptrc->left; tx < ptrc->right; tx++, pbFogMap++, pwTileMap++) {
				// Fogged?

				if (IsFogOpaque(*pbFogMap)) {
					*pbDst++ = m_clrBlack;
					*pbDst++ = m_clrBlack;
					*(pbDst + m_cbRowBytes - 2) = m_clrBlack;
					*(pbDst + m_cbRowBytes - 1) = m_clrBlack;
					continue;
				}

				// Not fogged; remember to redraw the minimap to the screen next timer

				m_wfMm |= kfMmRedraw;

				// Unit gob?

				UnitGob *punt = ggobm.GetUnitGob(tx, ty);
				if (punt != NULL) {
					dword wf = punt->GetFlags();
					if ((wf & (kfGobMobileUnit | kfGobActive)) != (kfGobMobileUnit))  {
						byte clr;
						if (wf & kfGobSelected) {
							clr = m_clrWhite;
						} else {
							clr = m_aclrSide[punt->GetSide()];
						}

						*pbDst++ = clr;
						*pbDst++ = clr;
						*(pbDst + m_cbRowBytes - 2) = clr;
						*(pbDst + m_cbRowBytes - 1) = clr;
						continue;
					}
				}

				// Galaxite?

				if (HasGalaxite(*pbFogMap)) {
					*pbDst++ = m_clrGalaxite;
					*pbDst++ = m_clrGalaxite;
					*(pbDst + m_cbRowBytes - 2) = m_clrGalaxite;
					*(pbDst + m_cbRowBytes - 1) = m_clrGalaxite;
					continue;
				}

				// Tile

				int nTile = (BigWord(*pwTileMap) & 0x7fc);
				byte *pbSrc = &m_pbTileData[nTile];
				*pbDst++ = *pbSrc++;
				*pbDst++ = *pbSrc++;
				*(pbDst + m_cbRowBytes - 2) = *pbSrc++;
				*(pbDst + m_cbRowBytes - 1) = *pbSrc++;
				continue;
			}
			pwTileMap += ctReturn;
			pbFogMap += ctReturn;
			pbDst += cbDstReturn + m_cbRowBytes;
		}
	}
}

} // namespace wi
