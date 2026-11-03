// SDL sound device.
//
// The game's sound effects are 4-bit ADPCM at 8 kHz, made for the mono
// speakers of 2000s handhelds. This device decodes them, upsamples them
// smoothly to the output rate and mixes up to kcVoices of them into 16-bit
// stereo, each with its own volume and left/right position (see
// SoundMgr::PlaySfx(sfx, wx, wy)). It also implements the master volume used
// by the Options screen.

#include "../ht.h"
#include <SDL.h>

#include "../sounddevice.h"
#include "../mixer.h"

namespace wi {

extern byte gabAdpcmSteppings[89][16];
extern char ganStepIndexMap[89][16];
extern long HostGetTickCount();

#define kcVoices kcChannelsMax      // 16 sounds at once (was 4)
#define knRateSource 8000           // sample rate of the sound effects
#define knRateOutput 44100
#define kcSamplesBuffer 1024        // per SDL callback (~23 ms)

// One voice's peak level, out of 32767. Leaves headroom so several loud
// sounds can play together before the limiter has to work.
#define knVoiceLevel 12000

struct Voice
{
    byte *pb;               // next ADPCM byte
    byte *pbEnd;
    bool fLowNibble;        // next nibble is the low half of *pb
    byte bSampleLast;       // ADPCM decoder state (unsigned 8 bit sample)
    int nStepIndex;
    int nSample0;           // previous and next decoded samples (-128..127)
    int nSample1;
    dword dwFrac;           // position between them, 16.16 fixed point
    bool fSource;           // decoder still has samples
    int nGainLeft;          // 0..256
    int nGainRight;
};

void AudioCallback(void *pvUser, Uint8 *stream, int len);

class SdlSoundDevice : public SoundDevice // sndd
{
public:
    SdlSoundDevice();
    virtual ~SdlSoundDevice();

    bool Init();
    virtual void Enable(bool fEnable);
    virtual bool IsEnabled();
    virtual void PlayAdpcm(int ichnl, byte *pb, word cb);
    virtual void PlayAdpcmEx(int ichnl, byte *pb, word cb, int nVolume, int nPan);
    virtual int GetChannelCount();
    virtual bool IsChannelFree(int ichnl);
    virtual void ServiceProc();
    virtual bool IsSilent();
    virtual void SetVolume(int nVolume);
    virtual int GetVolume();

    // Mixes cSamples stereo samples into ps (public for testing)
    void Mix(Sint16 *ps, int cSamples);

private:
    bool DecodeNext(Voice *pvoi, int *pnSample);

    bool m_fEnable;
    bool m_fOpen;
    long m_tSilence;
    int m_nVolume;          // master volume, 0..255
    Voice m_avoi[kcVoices];
    SDL_AudioSpec m_spec;

    friend void AudioCallback(void *pvUser, Uint8 *stream, int len);
};

SoundDevice *CreateSdlSoundDevice()
{
    SdlSoundDevice *psndd = new SdlSoundDevice;
    if (psndd == NULL)
        return NULL;
    if (!psndd->Init()) {
        delete psndd;
        return NULL;
    }
    return (SoundDevice *)psndd;
}

SdlSoundDevice::SdlSoundDevice()
{
    m_fEnable = false;
    m_fOpen = false;
    m_tSilence = 0;
    m_nVolume = 255;
    memset(m_avoi, 0, sizeof(m_avoi));
}

SdlSoundDevice::~SdlSoundDevice()
{
    if (m_fOpen)
        SDL_CloseAudio();
}

bool SdlSoundDevice::Init()
{
    // 16-bit stereo. Passing NULL for the obtained spec makes SDL convert
    // to whatever the hardware wants.

    memset(&m_spec, 0, sizeof(m_spec));
    m_spec.freq = knRateOutput;
    m_spec.format = AUDIO_S16SYS;
    m_spec.channels = 2;
    m_spec.samples = kcSamplesBuffer;
    m_spec.callback = AudioCallback;
    m_spec.userdata = this;
    if (SDL_OpenAudio(&m_spec, NULL) != 0)
        return false;
    m_fOpen = true;
    return true;
}

bool SdlSoundDevice::IsEnabled()
{
    return m_fEnable;
}

void SdlSoundDevice::Enable(bool fEnable)
{
    SDL_LockAudio();
    if (fEnable) {
        if (!m_fEnable) {
            memset(m_avoi, 0, sizeof(m_avoi));
            m_tSilence = 0;
            m_fEnable = true;
            SDL_PauseAudio(0);
        }
    } else {
        if (m_fEnable) {
            m_fEnable = false;
            memset(m_avoi, 0, sizeof(m_avoi));
            m_tSilence = 0;
            SDL_PauseAudio(1);
        }
    }
    SDL_UnlockAudio();
}

int SdlSoundDevice::GetChannelCount()
{
    return kcVoices;
}

bool SdlSoundDevice::IsChannelFree(int ichnl)
{
    if (ichnl < 0 || ichnl >= kcVoices)
        return false;
    SDL_LockAudio();
    bool fFree = !m_avoi[ichnl].fSource;
    SDL_UnlockAudio();
    return fFree;
}

bool SdlSoundDevice::IsSilent()
{
    if (!m_fEnable)
        return true;
    if (m_tSilence == 0)
        return true;
    long tCurrent = HostGetTickCount();
    if (tCurrent < 0 && m_tSilence >= 0)
        return true;
    return tCurrent >= m_tSilence;
}

void SdlSoundDevice::ServiceProc()
{
}

void SdlSoundDevice::PlayAdpcm(int ichnl, byte *pb, word cb)
{
    PlayAdpcmEx(ichnl, pb, cb, kcVolumeFull, 0);
}

void SdlSoundDevice::PlayAdpcmEx(int ichnl, byte *pb, word cb, int nVolume, int nPan)
{
    if (ichnl < 0 || ichnl >= kcVoices)
        return;
    if (!m_fEnable)
        return;
    if (nVolume < 0)
        nVolume = 0;
    if (nVolume > kcVolumeFull)
        nVolume = kcVolumeFull;
    if (nPan < -kcPanFull)
        nPan = -kcPanFull;
    if (nPan > kcPanFull)
        nPan = kcPanFull;

    SDL_LockAudio();
    Voice *pvoi = &m_avoi[ichnl];
    memset(pvoi, 0, sizeof(*pvoi));
    pvoi->pb = pb;
    pvoi->pbEnd = pb + cb;
    pvoi->bSampleLast = 128;
    pvoi->nStepIndex = 0;
    pvoi->fSource = cb != 0;

    // Linear pan: the far speaker fades as the sound moves to one side, the
    // near one stays at full level.
    pvoi->nGainLeft = nVolume * (nPan > 0 ? kcPanFull - nPan : kcPanFull) / kcPanFull;
    pvoi->nGainRight = nVolume * (nPan < 0 ? kcPanFull + nPan : kcPanFull) / kcPanFull;

    // Prime the interpolator with the first sample
    int nSample;
    pvoi->nSample0 = 0;
    pvoi->nSample1 = DecodeNext(pvoi, &nSample) ? nSample : 0;

    // 2 samples per byte at 8 kHz = cb / 40 ticks (1/100 s), plus output latency
    m_tSilence = HostGetTickCount() + (cb + 39) / 40 +
            (kcSamplesBuffer * 100 + knRateOutput - 1) / knRateOutput + 1;
    SDL_UnlockAudio();
}

bool SdlSoundDevice::DecodeNext(Voice *pvoi, int *pnSample)
{
    if (pvoi->pb >= pvoi->pbEnd)
        return false;
    int nDelta;
    if (!pvoi->fLowNibble) {
        nDelta = (*pvoi->pb) >> 4;
        pvoi->fLowNibble = true;
    } else {
        nDelta = (*pvoi->pb) & 0xf;
        pvoi->fLowNibble = false;
        pvoi->pb++;
    }
    // Byte arithmetic wraps on purpose: the table encodes negative steps
    // as 255, 254, ...
    pvoi->bSampleLast += gabAdpcmSteppings[pvoi->nStepIndex][nDelta];
    pvoi->nStepIndex = ganStepIndexMap[pvoi->nStepIndex][nDelta];
    *pnSample = (int)pvoi->bSampleLast - 128;
    return true;
}

void SdlSoundDevice::Mix(Sint16 *ps, int cSamples)
{
    const dword dwStep = (dword)(((long long)knRateSource << 16) / knRateOutput);
    int nMaster = m_nVolume;

    for (int i = 0; i < cSamples; i++) {
        int nLeft = 0;
        int nRight = 0;
        for (int ivoi = 0; ivoi < kcVoices; ivoi++) {
            Voice *pvoi = &m_avoi[ivoi];
            if (!pvoi->fSource)
                continue;

            // Linear interpolation between the last two decoded samples
            int nSample = pvoi->nSample0 +
                    (int)(((long long)(pvoi->nSample1 - pvoi->nSample0) * pvoi->dwFrac) >> 16);
            int nLevel = nSample * knVoiceLevel / 128;
            nLeft += nLevel * pvoi->nGainLeft / kcVolumeFull;
            nRight += nLevel * pvoi->nGainRight / kcVolumeFull;

            pvoi->dwFrac += dwStep;
            while (pvoi->dwFrac >= 0x10000) {
                pvoi->dwFrac -= 0x10000;
                pvoi->nSample0 = pvoi->nSample1;
                int nNext;
                if (!DecodeNext(pvoi, &nNext)) {
                    pvoi->fSource = false;
                    break;
                }
                pvoi->nSample1 = nNext;
            }
        }

        // Master volume, then a soft limiter so a pile-up of loud sounds
        // compresses instead of clipping harshly.
        nLeft = nLeft * nMaster / 255;
        nRight = nRight * nMaster / 255;
        int an[2] = { nLeft, nRight };
        for (int ich = 0; ich < 2; ich++) {
            int n = an[ich];
            const int knKnee = 24000;
            if (n > knKnee)
                n = knKnee + (n - knKnee) / 4;
            else if (n < -knKnee)
                n = -knKnee + (n + knKnee) / 4;
            if (n > 32767)
                n = 32767;
            if (n < -32768)
                n = -32768;
            *ps++ = (Sint16)n;
        }
    }
}

void SdlSoundDevice::SetVolume(int nVolume)
{
    if (nVolume < 0)
        nVolume = 0;
    if (nVolume > 255)
        nVolume = 255;
    SDL_LockAudio();
    m_nVolume = nVolume;
    SDL_UnlockAudio();
}

int SdlSoundDevice::GetVolume()
{
    return m_nVolume;
}

void AudioCallback(void *pvUser, Uint8 *stream, int len)
{
    // SDL holds the audio lock while calling this
    SdlSoundDevice *psndd = (SdlSoundDevice *)pvUser;
    if (!psndd->m_fEnable) {
        SDL_memset(stream, 0, len);
        return;
    }
    psndd->Mix((Sint16 *)stream, len / (2 * sizeof(Sint16)));
}

} // namespace wi
