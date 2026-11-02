#ifndef __WINCOMPAT_H__
#define __WINCOMPAT_H__

// Windows (MinGW-w64) compatibility shims for the POSIX socket code in base/.
// Included only when building for Windows.

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <errno.h>
#include <io.h>

// windows.h defines macros that collide with method names in this code base.
#undef SetPort
#undef GetObject
#undef DrawText
#undef CreateWindow
#undef LoadString
#undef SendMessage
#undef PostMessage
#undef GetMessage
#undef MessageBox
#undef DeleteFile
#undef GetUserName
#undef Yield
#undef CreateEvent
#undef GetCurrentTime
#undef DispatchMessage
#undef FindResource
#undef LoadBitmap
#undef LoadImage
#undef GetClassName
#undef CopyFile
#undef MoveFile
#undef CreateDirectory
#undef RemoveDirectory
#undef GetFileAttributes
#undef ERROR
#undef TRANSPARENT
#undef OPAQUE
#undef RGB
#undef min
#undef max
#undef near
#undef far
#undef interface

// POSIX errno names that MinGW's <errno.h> may not provide.
#ifndef ESOCKTNOSUPPORT
#define ESOCKTNOSUPPORT WSAESOCKTNOSUPPORT
#endif
#ifndef EPFNOSUPPORT
#define EPFNOSUPPORT WSAEPFNOSUPPORT
#endif
#ifndef ESHUTDOWN
#define ESHUTDOWN WSAESHUTDOWN
#endif
#ifndef ETOOMANYREFS
#define ETOOMANYREFS WSAETOOMANYREFS
#endif
#ifndef EHOSTDOWN
#define EHOSTDOWN WSAEHOSTDOWN
#endif
#ifndef EUSERS
#define EUSERS WSAEUSERS
#endif
#ifndef EDQUOT
#define EDQUOT WSAEDQUOT
#endif
#ifndef ESTALE
#define ESTALE WSAESTALE
#endif
#ifndef EREMOTE
#define EREMOTE WSAEREMOTE
#endif
#ifndef EPROCLIM
#define EPROCLIM WSAEPROCLIM
#endif

namespace base {

// Initializes Winsock once per process. Safe to call repeatedly.
inline void WinsockInit() {
    static bool s_initialized = false;
    if (!s_initialized) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        s_initialized = true;
    }
}

// Returns the last socket error mapped onto the POSIX errno values used
// throughout base/ (EWOULDBLOCK, EINPROGRESS, ...).
inline int SocketErrno() {
    int err = WSAGetLastError();
    switch (err) {
    case 0: return 0;
    case WSAEWOULDBLOCK: return EWOULDBLOCK;
    case WSAEINPROGRESS: return EINPROGRESS;
    case WSAEALREADY: return EALREADY;
    case WSAEINTR: return EINTR;
    case WSAEBADF: return EBADF;
    case WSAEACCES: return EACCES;
    case WSAEFAULT: return EFAULT;
    case WSAEINVAL: return EINVAL;
    case WSAEMFILE: return EMFILE;
    case WSAENOTSOCK: return ENOTSOCK;
    case WSAECONNRESET: return ECONNRESET;
    case WSAECONNREFUSED: return ECONNREFUSED;
    case WSAECONNABORTED: return ECONNABORTED;
    case WSAENOTCONN: return ENOTCONN;
    case WSAETIMEDOUT: return ETIMEDOUT;
    case WSAEADDRINUSE: return EADDRINUSE;
    case WSAEHOSTUNREACH: return EHOSTUNREACH;
    case WSAENETUNREACH: return ENETUNREACH;
    default: return err;
    }
}

inline int SocketCloseHandle(int s) {
    return closesocket((SOCKET)s);
}

inline void SocketSetNonBlocking(int s, bool nonblocking) {
    u_long mode = nonblocking ? 1 : 0;
    ioctlsocket((SOCKET)s, FIONBIO, &mode);
}

} // namespace base

#endif // _WIN32

#endif // __WINCOMPAT_H__
