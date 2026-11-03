#ifndef __PACKS_H__
#define __PACKS_H__

// The mission packs installed in the game (by the map editor, or copied in
// by hand) and their title/author info files. (Downloading packs from the
// original servers was removed.)

#include "mpshared/packmanager.h"
#include "mpshared/packinfomanager.h"

namespace wi {

extern PackManager *gppackm;
extern PackInfoManager *gppim;

} // namespace wi

#endif // __PACKS_H__
