// Platform-specific entrypoints (e.g. mac/SDLMain.m) do early platform-specific
// setup and establish a uniform 'SDL' environment. Then they invoke SDL_main
// (see below) which does platform-independent SDL setup and calls the main
// game entrypoint wi::GameMain.
//
// Further platform-specific setup is done as the game calls Host* interfaces
// as it initializes.

#include "game/ht.h"
#include "game/sdl/sdleventserver.h"
#include "base/thread.h"
#include <string.h>

#if 0
/* Call this instead of exit(), so we can clean up SDL: atexit() is evil. */
static void quit(int rc)
{
    SDL_Quit();
    exit(rc);
}
#endif

#ifdef __cplusplus
extern "C"
#endif

#ifdef __LINUX__
int main (int argc, char *argv[])
#else
int SDL_main(int argc, char *argv[])
#endif
{
    // Create the main thread
    base::Thread *main_thread = new base::Thread();

    // Set up the main thread as the SDL event thread.
    base::Thread::current().set_ss(new wi::SdlEventServer());

    // Let the host have a pointer to the thread
    wi::HostSetGameThread(main_thread);

    // Pass the command line through (e.g. "-pack <id>" from the editor)
    static char szCmds[1024];
    szCmds[0] = 0;
    for (int i = 1; i < argc; i++) {
        if (strlen(szCmds) + strlen(argv[i]) + 2 >= sizeof(szCmds)) {
            break;
        }
        if (i > 1) {
            strcat(szCmds, " ");
        }
        strcat(szCmds, argv[i]);
    }
    wi::GameMain(szCmds);
    return 0;
}
