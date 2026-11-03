#ifndef __PACKMANAGER_H__
#define __PACKMANAGER_H__

#include "inc/basictypes.h"
#include "mpshared/enum.h"
#include "mpshared/mpht.h"
#include "mpshared/packfile.h"
#include <string>
#include <map>
#include <sys/types.h>

namespace wi {

// Manages the mission packs installed in the game's MissionPacks folder

class PackManager { // packm
public:
    PackManager(const char *cachedir);
    ~PackManager();

    void InitFromInstalled();

    bool EnumPacks(Enum *penm, PackId *ppackid);
    int IsInstalled(const PackId *ppackid, PackId *ppackidUpgrade = NULL);
    bool Remove(const PackId *ppackid, bool fCheckHash = true);
    bool Mount(PackFileReader& pakr, const PackId *ppackid);
    bool Unmount(PackFileReader& pakr, const PackId *ppackid);
    static const char *GetPackFilename(const PackId *ppackid);

protected:
    const char *GetPackFilepath(const PackId *ppackid);
    bool ParseFilename(const char *filename, PackId *ppackid);

    std::string cachedir_;

    typedef std::map<dword,PackId> PackMap;
    PackMap map_;
};

} // namespace wi

#endif // __PACKMANAGER_H__
