#include "eventer.h"
#include <string.h>
#ifdef _WIN32
#include "base/wincompat.h"
#else
#include <unistd.h>
#include <signal.h>
#endif

namespace base {

#ifdef _WIN32

// Windows select() only works on sockets, not pipes. Use a non-blocking UDP
// socket bound to loopback and "connected" to itself: Signal() sends a byte
// to it, which makes it readable and wakes select().

Eventer::Eventer(SocketServer *ss, bool *wait) : Dispatchee(ss), wait_(wait),
        signaled_(false) {
    WinsockInit();
    SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ::bind(s, (sockaddr *)&addr, sizeof(addr));
    int cb = sizeof(addr);
    ::getsockname(s, (sockaddr *)&addr, &cb);
    ::connect(s, (sockaddr *)&addr, sizeof(addr));
    SocketSetNonBlocking((int)s, true);
    afd_[0] = (int)s;
    afd_[1] = (int)s;
    dispatcher_->SetDispatchee(this, afd_[0]);
    dispatcher_->SetEvents(Dispatcher::kfRead);
}

Eventer::~Eventer()
{
    SocketCloseHandle(afd_[0]);
}

void Eventer::Signal()
{
    CritScope cs(&crit_);
    if (!signaled_) {
        char b = 0;
        ::send((SOCKET)afd_[1], &b, sizeof(b), 0);
        signaled_ = true;
    }
}

void Eventer::OnEvent(dword ff)
{
    CritScope cs(&crit_);
    if (signaled_) {
        // Drain everything queued
        char ab[16];
        while (::recv((SOCKET)afd_[0], ab, sizeof(ab), 0) > 0)
            ;
        signaled_ = false;
    }
    if (wait_ != NULL) {
        *wait_ = false;
    }
}

#else

Eventer::Eventer(SocketServer *ss, bool *wait) : Dispatchee(ss), wait_(wait),
        signaled_(false) {
    pipe(afd_);
    signal(SIGPIPE, SIG_IGN);
    dispatcher_->SetDispatchee(this, afd_[0]);
    dispatcher_->SetEvents(Dispatcher::kfRead);
}

Eventer::~Eventer()
{
    close(afd_[0]);
    close(afd_[1]);
}

void Eventer::Signal()
{
    CritScope cs(&crit_);
    if (!signaled_) {
        byte b = 0;
        write(afd_[1], &b, sizeof(b));
        signaled_ = true;
    }
}

void Eventer::OnEvent(dword ff)
{
    CritScope cs(&crit_);
    if (signaled_) {
        byte b;
        read(afd_[0], &b, sizeof(b));
        signaled_ = false;
    }
    if (wait_ != NULL) {
        *wait_ = false;
    }
}

#endif

} // namespace base
