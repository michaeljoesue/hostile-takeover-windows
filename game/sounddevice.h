#ifndef __SOUNDDEVICE_H__
#define __SOUNDDEVICE_H__

#include "basictypes.h"

namespace wi {

// Full volume for PlayAdpcmEx; pan runs from -kcPanFull (left) to
// kcPanFull (right).
#define kcVolumeFull 256
#define kcPanFull 256

class SoundDevice // sndd
{
public:
    virtual ~SoundDevice() = 0;
    virtual void Enable(bool fEnable) = 0;
    virtual bool IsEnabled() = 0;
    virtual void PlayAdpcm(int ichnl, byte *pb, word cb) = 0;

    // Plays a sound at a volume (0..kcVolumeFull) and stereo position
    // (-kcPanFull..kcPanFull). Devices without stereo or per-sound volume
    // just play it normally.
    virtual void PlayAdpcmEx(int ichnl, byte *pb, word cb, int nVolume, int nPan) {
        PlayAdpcm(ichnl, pb, cb);
    }

    virtual int GetChannelCount() = 0;
    virtual bool IsChannelFree(int ichnl) = 0;
    virtual void ServiceProc() = 0;
    virtual bool IsSilent() = 0;
    virtual void SetVolume(int nVolume) = 0;
    virtual int GetVolume() = 0;
};

} // namespace wi

#endif // __SOUNDDEVICE_H__
