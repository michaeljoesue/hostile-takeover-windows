#ifndef __WINCHATCONTROLLER_H__
#define __WINCHATCONTROLLER_H__

#include <string>
#include "game/sdl/hosthelpers.h"
#include "base/thread.h"
#include "game/chatcontroller.h"

#define kidmOnSend 1
#define kidmOnDismissed 2
#define kidmOnPlayers 3

namespace wi {

struct ChatParams : base::MessageData {
    ChatParams(const std::string& chat) : chat(chat) {}
    std::string chat;
};

// Game thread c++ interface to the ChatViewController
class WinChatController : public IChatController, public base::MessageHandler {
public:
    WinChatController();

    void Clear();
    void AddChat(const char *player, const char *chat);
    void Show(bool fShow);
    void SetTitle(const char *title);
    const char *GetTitle();
    IChatControllerCallback *SetCallback(IChatControllerCallback *pcccb);

private:
    // MessageHandler
    virtual void OnMessage(base::Message *pmsg);
};

} // namespace wi

#endif // __WINCHATCONTROLLER_H__
