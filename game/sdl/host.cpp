#include "game/ht.h"
#include "game/sdl/hosthelpers.h"
#include "base/tick.h"
#include "base/log.h"
#include "base/thread.h"
#include "game/packs.h"
#include "game/completemanager.h"
#include <sys/types.h>
#include <errno.h>
#include <SDL.h>

namespace wi {

SdlPackFileReader gpakr;
PackManager *gppackm;
PackInfoManager *gppim;
CompleteManager *gpcptm;
base::Thread *gpgt;

SurfaceProperties gprops;
SDL_FingerID gtouches[2];
wi::Point gaptLast[2];

char *gpszUdid;

bool HostInit()
{
    HostHelpers::Init();
    HostHelpers::GetSurfaceProperties(&gprops);

    gppackm = new PackManager(HostHelpers::GetMissionPacksDir());
    gppackm->InitFromInstalled();
    gppim = new PackInfoManager(HostHelpers::GetMissionPackInfosDir());
    gpcptm = new CompleteManager(HostHelpers::GetCompletesDir());
    gpcptm->Init();

    // These initilize to 0 but are expected to be -1
    // since 0 is a valid touch id on some devices
    gtouches[0] = -1;
    gtouches[1] = -1;

	// TODO(darrinm): get user id (gpszUdid)

    return true;
}

void HostExit()
{
    delete gppim;
    delete gppackm;

    HostHelpers::Cleanup();
}

void HostSuspendModalLoop(DibBitmap *pbm)
{
    // Wait for WI to become active again

    LOG() << "Entering SuspendModalLoop";

    base::Thread& thread = base::Thread::current();
    while (true) {
        base::Message msg;
        thread.Get(&msg);
        if (msg.id == kidmAppSetFocus) {
            break;
        }
        if (msg.id == kidmAppTerminate) {
            thread.Post(&msg);
            break;
        }
        thread.Dispatch(&msg);
    }

    LOG() << "Leaving SuspendModalLoop";
}

//
// Post motion events into the message queue in order
// to handle event coalescing for kidmMoveEvent / kidmMoveEvent2.
//
void PostSdlMotionEvent(const SDL_Event &event)
{
    // Post these into the message queue with coalescing
    // The second id is a special exception. Coalesce sequential
    // events, but allow this exception. This allows coalescing
    // to occur across an intermixed stream of finger 1 and finger 2
    // move events.
    for (int i = 0; i < 2; i++) {
        if (gtouches[i] == event.tfinger.fingerId) {
            base::Message msg;
            msg.id = (i == 0) ? kidmFingerMoveEvent : kidmFingerMoveEvent2;
            msg.x = (int)(event.tfinger.x * gprops.cxWidth);
            msg.y = (int)(event.tfinger.y * gprops.cyHeight);
            msg.ff = 0;
            msg.ms = HostGetMillisecondCount();

            gaptLast[i].x = (int)msg.x;
            gaptLast[i].y = (int)msg.y;

            HostGetGameThread().Post(&msg, i == 0 ? kidmFingerMoveEvent2 : kidmFingerMoveEvent);
            break;
        }
    }
}

void CheckTouchTracking() {
    // This is just in case - if we lose an up, this detects it and resets
    // things.

    int cTouchesTracking = 0;
    for (int i = 0; i < 2; i++) {
        if (gtouches[i] != -1) {
            cTouchesTracking++;
        }
    }
    int cTouchesFound = 0;

    // Check each touch device

    int ntd = SDL_GetNumTouchDevices();
    for (int i = 0; i < ntd; i++) {
        SDL_TouchID device = SDL_GetTouchDevice(i);

        for (int n = 0; n < SDL_GetNumTouchFingers(device); n++) {
            SDL_FingerID fingerId = SDL_GetTouchFinger(device, n)->id;
            if (fingerId == gtouches[0] || fingerId == gtouches[1]) {
                cTouchesFound++;
            }
        }
    }

    // If it's whacked, post an up and set the slot to -1.

    if (cTouchesFound < cTouchesTracking) {
        for (int i = 0; i < 2; i++) {
            if (gtouches[i] != -1) {
                gtouches[i] = -1;
                base::Message msg;
                msg.id = (i == 0) ? kidmFingerUpEvent : kidmFingerUpEvent2;
                msg.x = gaptLast[i].x;
                msg.y = gaptLast[i].y;
                msg.ff = 0;
                msg.ms = wi::HostGetMillisecondCount();
                HostGetGameThread().Post(&msg);
            }
        }
    }
}

#ifdef _WIN32
// Mouse clicks in the letterbox bars around the scaled game image come in
// outside of game space; pin them to the edge.
static void ClampToGameSurface(Event *pevt) {
    SurfaceProperties props;
    HostHelpers::GetSurfaceProperties(&props);
    if (pevt->x < 0) pevt->x = 0;
    if (pevt->y < 0) pevt->y = 0;
    if (pevt->x >= props.cxWidth) pevt->x = props.cxWidth - 1;
    if (pevt->y >= props.cyHeight) pevt->y = props.cyHeight - 1;
}

#endif

bool ProcessSdlEvent(base::Message *pmsg, Event *pevt)
{
    memset(pevt, 0, sizeof(*pevt));


    SDL_Event event;
    switch (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT,
            SDL_LASTEVENT)) {
    case -1:
        // App is exiting, or there is an error
        pevt->eType = appStopEvent;
        break;

    case 0:
        return false;
    }

	switch (event.type) {

    /*
    Note: SDL sees all mouse/touch input as "coursor input". Mouse events are 
    processed as touch events and real touch events raise fake mosue events. 
    Because WI supports multitouch, mouse events and finger events need to be 
    processed separately. Until a better way is found, use macros to only 
    process the input events appropriate for the given platform:

    - OS X doesn't support touch input. Thus, only process SDL_MOUSE events.
    - iOS doesn't support mouse input. Thus, only process SDL_FINGER events.
    - Android supports both mouse and touch input. Fortunately,
    SDL_HINT_ANDROID_SEPARATE_MOUSE_AND_TOUCH can be used to process mouse events
    separately from touch events (it's a shame a similar hint doesn't exist for
    other platforms). Thus, Android can process both input event types.
    - Linux... We'll try only processing SDL_MOUSE events for now.
    */


#if defined(_WIN32)
	case SDL_MOUSEBUTTONDOWN:
        pevt->eType = penDownEvent;
        pevt->x = event.button.x;
        pevt->y = event.button.y;
#ifdef _WIN32
        ClampToGameSurface(pevt);
        if (event.button.button == SDL_BUTTON_RIGHT) {
            pevt->ff |= kfEvtRightButton;
        } else if (event.button.button != SDL_BUTTON_LEFT) {
            return false;
        }
        if (SDL_GetModState() & KMOD_CTRL) {
            pevt->ff |= kfEvtForceAttack;
        }
#endif
		break;
		
	case SDL_MOUSEBUTTONUP:
        pevt->eType = penUpEvent;
		pevt->x = event.button.x;
		pevt->y = event.button.y;
#ifdef _WIN32
        ClampToGameSurface(pevt);
        if (event.button.button == SDL_BUTTON_RIGHT) {
            pevt->ff |= kfEvtRightButton;
        } else if (event.button.button != SDL_BUTTON_LEFT) {
            return false;
        }
        if (SDL_GetModState() & KMOD_CTRL) {
            pevt->ff |= kfEvtForceAttack;
        }
#endif
		break;
		
	case SDL_MOUSEMOTION:
        pevt->eType = penMoveEvent;
		pevt->x = event.motion.x;
		pevt->y = event.motion.y;
#ifdef _WIN32
        ClampToGameSurface(pevt);
#endif
		break;
#endif
		
	case SDL_KEYDOWN:
#ifdef _WIN32
        // F11 / Alt+Enter used to toggle fullscreen. The screen setup is
        // fixed now (always fullscreen, sharp scaling), so swallow them.
        if (event.key.keysym.sym == SDLK_F11 ||
                ((event.key.keysym.sym == SDLK_RETURN ||
                  event.key.keysym.sym == SDLK_KP_ENTER) &&
                 (event.key.keysym.mod & KMOD_ALT))) {
            return false;
        }
#endif
        pevt->eType = keyDownEvent;
        switch (event.key.keysym.sym) {
		case SDLK_UP:
			pevt->chr = chrPageUp;
			break;
			
		case SDLK_DOWN:
			pevt->chr = chrPageDown;
			break;
			
		case SDLK_LEFT:
			pevt->chr = chrLeft;
			break;
			
		case SDLK_RIGHT:
			pevt->chr = chrRight;
			break;
			
		case SDLK_BACKSPACE:
			pevt->chr = chrBackspace;
			break;
			
		case SDLK_DELETE:
			pevt->chr = chrDelete;
			break;

        case SDLK_AC_BACK:
        case SDLK_ESCAPE:
            // Escape: closes dialogs and menus, cancels placing a
            // structure, opens the game menu
            pevt->chr = vchrBack;
            break;

#if 0			
		case SDLK_F7:
			if (gpavir == NULL) {
				gpavir = new AviRecorder();
				Size siz;
				gpdisp->GetFrontDib()->GetSize(&siz);
				if (!gpavir->Start(siz.cx, siz.cy)) {
					delete gpavir;
					gpavir = NULL;
				}
			}
			break;

		case SDLK_F8:
			if (gpavir != NULL) {
				gpavir->Stop();
				delete gpavir;
				gpavir = NULL;
			}
			break;
#endif

		case SDLK_KP_MINUS:	// numpad
			{
				int i = 0;
				for (; i < ARRAYSIZE(gatGameSpeeds); i++)
					if (gatGameSpeeds[i] == gtGameSpeed)
						break;
				i--;
				if (i < 0)
					i = 0;
				ggame.SetGameSpeed(gatGameSpeeds[i]);
			}
			break;

		case SDLK_KP_PLUS:	// numpad
			{
				int i = 0;
				for (; i < ARRAYSIZE(gatGameSpeeds); i++)
					if (gatGameSpeeds[i] == gtGameSpeed)
						break;
				i++;
				if (i >= ARRAYSIZE(gatGameSpeeds))
					i = ARRAYSIZE(gatGameSpeeds) - 1;
				ggame.SetGameSpeed(gatGameSpeeds[i]);
			}
			break;

        default:
            pevt->chr = event.key.keysym.sym;
            break;
        }
        break;

    case SDL_APP_DIDENTERFOREGROUND:
        // Allow the display to render
        gpdisp->SetShouldRender(true);

        // Unpause simulation
        ggame.GamePause(false);

        // SDL may have released its graphics context if the app was previously
        // backgrounded. This leaves the screen black when the user returns.
        // Hack: Draw dib and render
        gpmfrmm->DrawFrame(true);
        gpdisp->RenderGameSurface();
        break;

    case SDL_APP_DIDENTERBACKGROUND:
        // Pause simulation
        ggame.GamePause(true);

        // Stop display rendering; SDL may release its graphics context when
        // backgrounded, so we don't want to try to render to a non-existant context.
        gpdisp->SetShouldRender(false);
        break;

    case SDL_APP_WILLENTERFOREGROUND:
        break;

    case SDL_APP_WILLENTERBACKGROUND:
        break;

    case SDL_APP_TERMINATING:
        // NOTE: The app seems to terminate before the code in this
        // case can be executed (iOS and Android).
        break;

	case SDL_QUIT:
        pevt->eType = appStopEvent;
        break;

    case SDL_WINDOWEVENT:
        gpmfrmm->DrawFrame(true);
        gpdisp->RenderGameSurface();
        break;

    default:
        return false;
    }    

#if 0 // TODO(scottlu)
    - Add SDL processing for touch events.
    - Generate penMoveEvent2, penDownEvent2, penUpEvent2
    - add pevt->ff |= kfEvtFinger
    - add SDL support for device turned off, or locked and return
      gameSuspendEvent. If no SDL support for this, post kidmAppKillFocus
      in native layer and translate into gameSuspendEvent in ProcessEvents.
      old code:

    case kidmAppKillFocus:
        pevt->eType = gameSuspendEvent;
        pevt->ff = 0;
        break;
#endif

#if 0 // TODO(darrinm)
    if (gpdisp != NULL) {
        int x = pevt->x;
        int y = pevt->y;

        SurfaceProperties props;
        HostHelpers::GetSurfaceProperties(&props);
        int cx = props.cxWidth;
        int cy = props.cyHeight;

        ModeInfo mode;
        gpdisp->GetMode(&mode);
        switch (mode.nDegreeOrientation) {
        case 0:
            // Screen rotated 90 degrees but coordinates unrotated
            pevt->x = y;
            pevt->y = (cy - 1) - x;
            break;

        case 90:
            pevt->x = (cy - 1) - y;
            pevt->y = x;
            break;

        case 180:
            pevt->x = (cx - 1) - x;
            pevt->y = (cy - 1) - y;
            break;

        case 270:
            pevt->x = y;
            pevt->y = (cx - 1) - x;
            break;
        }
    }
#endif

    pevt->ms = pmsg->ms;
    if (pevt->ms == 0)
        pevt->ms = HostGetMillisecondCount();

    return true;
}

bool ProcessMessage(base::Message *pmsg, Event *pevt)
{
    memset(pevt, sizeof(*pevt), 0);
    switch (pmsg->id) {
    case base::kidmNullEvent:
        pevt->eType = nullEvent;
        pevt->ff = 0;
        break;

    case base::kidmTransportEvent:
        pevt->eType = transportEvent;
        pevt->ff = 0;
        break;

    case kidmAskStringEvent:
        pevt->eType = askStringEvent;
        pevt->ff = 0;
        break;

    case kidmFingerMoveEvent:
        pevt->eType = penMoveEvent;
        pevt->ff = kfEvtFinger;
        break;

    case kidmFingerMoveEvent2:
        pevt->eType = penMoveEvent2;
        pevt->ff = kfEvtFinger;
        break;

    case kidmFingerUpEvent:
        pevt->eType = penUpEvent;
        pevt->ff = kfEvtFinger;
        break;

    case kidmFingerUpEvent2:
        pevt->eType = penUpEvent2;
        pevt->ff = kfEvtFinger;
        break;
		
    default:
        return false;
    }

    pevt->dw = 0;
    pevt->x = pmsg->x;
    pevt->y = pmsg->y;
    pevt->ms = pmsg->ms;
    if (pmsg->ff & kfMsgCoalesce) {
        pevt->ff |= kfEvtCoalesce;
    }
    if (pmsg->ff & kfMsgCancelMode) {
        pevt->dw = 1;
    }
    
    return true;
}

bool HostGetEvent(Event *pevt, long ctWait)
{
    base::Thread& thread = base::Thread::current();
    while (true) {
        base::Message msg;
        if (!thread.Get(&msg, ctWait)) {
            return false;
        }

        if (msg.handler != NULL) {
            thread.Dispatch(&msg);
            continue;
        }

        if (msg.id == kidmSdlEvent) {
            if (ProcessSdlEvent(&msg, pevt)) {
                return true;
            }
            continue;
        }

        if (ProcessMessage(&msg, pevt)) {
            return true;
        }
    }
}

void HostOutputDebugString(char *pszFormat, ...)
{
#ifdef DEBUG
    va_list va;
    va_start(va, pszFormat);
    HostHelpers::Log(pszFormat, va);
    va_end(va);
#endif
}

long HostGetTickCount()
{
    return (long)base::GetTickCount();
}

long HostGetMillisecondCount()
{
    return (long)base::GetMillisecondCount();
}
    
long HostRunSpeedTests(DibBitmap *pbmSrc)
{
	return 0;
}

dword HostGetCurrentKeyState(dword keyBit)
{
    return 0;
}

// Arrow keys held right now, as a direction (-1, 0 or 1 on each axis)

void HostGetScrollKeys(int *pdx, int *pdy)
{
    const Uint8 *pkeys = SDL_GetKeyboardState(NULL);
    *pdx = 0;
    *pdy = 0;
    if (pkeys == NULL)
        return;
    if (pkeys[SDL_SCANCODE_LEFT])
        (*pdx)--;
    if (pkeys[SDL_SCANCODE_RIGHT])
        (*pdx)++;
    if (pkeys[SDL_SCANCODE_UP])
        (*pdy)--;
    if (pkeys[SDL_SCANCODE_DOWN])
        (*pdy)++;
}

bool HostIsPenDown()
{
    return false;
}

void HostMessageBox(TCHAR *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    HostHelpers::MessageBox(pszFormat, va);
    va_end(va);
}

void HostGetUserName(char *pszBuff, int cb)
{
    strncpyz(pszBuff, "anonymous", cb);
}

bool HostGetOwnerName(char *pszBuff, int cb, bool fShowError)
{
    strncpyz(pszBuff, "Player", cb);
    return true;
}

// UNDONE: prefix directory?

FileHandle HostOpenFile(const char *pszFilename, word wf)
{
    const char *pszMode;
    if (wf == kfOfRead)
        pszMode = "rb";
    else if (wf == kfOfWrite)
        pszMode = "wb";
    else if (wf == (kfOfRead | kfOfWrite))
        pszMode = "rb+";

    return (FileHandle)fopen((char *)pszFilename, pszMode);
}

void HostCloseFile(FileHandle hf)
{
    fclose((FILE *)hf);
}

dword HostWriteFile(FileHandle hf, void *pv, dword cb)
{
    return fwrite(pv, 1, cb, (FILE *)hf);
}

dword HostReadFile(FileHandle hf, void *pv, dword cb)
{
    return fread(pv, 1, cb, (FILE *)hf);
}

void HostSleep(dword ct)
{
    SDL_Delay(ct * 10);
}

void HostGetSilkRect(int irc, Rect *prc)
{
    return;
}

// Figure out what kind of sound device exists, and return a SoundDevice for it

SoundDevice *HostOpenSoundDevice()
{
    return CreateSdlSoundDevice();
}

SoundDevice::~SoundDevice()
{
}

// Used for sound buffer maintenance requirements

bool HostSoundServiceProc()
{
    return true;
}

void HostGetCurrentDate(Date *pdate)
{
    time_t result = time(NULL);
    struct tm *ptm = localtime(&result);
    pdate->nYear = ptm->tm_year + 1900;
    pdate->nMonth = ptm->tm_mon + 1;
    pdate->nDay = ptm->tm_mday;
}

bool HostSavePreferences(void *pv, int cb)
{
    LOG() << HostHelpers::GetPrefsFilename();

    FILE *pf = fopen(HostHelpers::GetPrefsFilename(), "wb");
    if (pf == NULL) {
        LOG() << "error opening preferences! " << errno;
        return false;
    }
    if (fwrite(pv, cb, 1, pf) != 1) {
        LOG() << "error writing preferences! " << errno;
        fclose(pf);
        return false;
    }
    fclose(pf);
    return true;
}

int HostLoadPreferences(void *pv, int cb)
{
    FILE *pf = fopen(HostHelpers::GetPrefsFilename(), "rb");
    if (pf == NULL) {
        return -1;
    }

    // Read prefs

    int cbRead = (int)fread(pv, 1, cb, pf);
    fclose(pf);
    return cbRead;
}

const char *HostGetMainDataDir()
{
    return HostHelpers::GetMainDataDir();
}

const char *HostGetSaveGamesDir()
{
    return HostHelpers::GetSaveGamesDir();
}

void HostNotEnoughMemory(bool fStorage, dword cbFree, dword cbNeed)
{
	HostMessageBox(TEXT((char *)"Need %ld bytes of memory but only %ld bytes are free!"), cbNeed, cbFree);
}

bool HostEnumAddonFiles(Enum *penm, char *pszAddonDir, int cbDir,
        char *pszAddon, int cb)
{
    // PackManager is the way to do this now
    return false;
}

void HostSetGameThread(base::Thread *thread) {
    gpgt = thread;
}

base::Thread& HostGetGameThread() {
    return *gpgt;
}

base::Thread *HostGetGameThreadPointer() {
    return gpgt;
}

void HostAppStop() {
    // Only use this function for when you can't post an appStopEvent

    ggame.SaveReinitializeGame();
    gevm.SetAppStopping();
}

} // namespace wi
