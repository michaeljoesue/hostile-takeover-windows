#include "winchatcontroller.h"
#include "game/sdl/hosthelpers.h"

namespace wi {

IChatControllerCallback *pcccb_;
wi::WinChatController *chatc_;

WinChatController::WinChatController() {
}

void WinChatController::Clear() {
}

void WinChatController::AddChat(const char *player, const char *chat) {
}

void WinChatController::Show(bool fShow) {
}

void WinChatController::SetTitle(const char *title) {
}

const char *WinChatController::GetTitle() {
}

IChatControllerCallback *WinChatController::SetCallback(
        IChatControllerCallback *pcccb) {
    IChatControllerCallback *old = pcccb_;
    pcccb_ = pcccb;
    return old;
}

void WinChatController::OnMessage(base::Message *pmsg) {
    switch (pmsg->id) {
    case kidmOnDismissed:
        if (pcccb_ != NULL) {
            pcccb_->OnChatDismissed();
        }
        break;

    case kidmOnSend:
        if (pcccb_ != NULL) {
            ChatParams *params = (ChatParams *)pmsg->data;
            pcccb_->OnChatSend(params->chat.c_str());
            delete params;
        }
        break;

    case kidmOnPlayers:
        if (pcccb_ != NULL) {
            pcccb_->OnPlayers();
        }
        break;
    }
}

} // namespace wi
