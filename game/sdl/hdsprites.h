#ifndef __HDSPRITES_H__
#define __HDSPRITES_H__

// HD sprites, display side (see hdsprites.cpp). The game side is declared in ht.h.

namespace wi {

void HdToggle();
bool HdHasCommands();
void HdComposite(SDL_Renderer *prend, int nScale, byte *pbFront, int cxFront, int cyFront, Uint32 *palette);

} // namespace wi

#endif // __HDSPRITES_H__
