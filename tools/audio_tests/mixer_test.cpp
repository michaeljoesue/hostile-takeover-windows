// Tests the SDL mixer (game/sdl/sdlsounddev.cpp) by including it directly.
// Build and run with tools/audio_tests/run.sh.
#define private public
#include "game/sdl/sdlsounddev.cpp"
#undef private
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

namespace wi {
}
using namespace wi;

static double Rms(Sint16 *ps, int c, int ich) {
    double s = 0; for (int i = 0; i < c; i++) { double v = ps[i*2+ich]; s += v*v; } return sqrt(s / c);
}
static int Fails = 0;
#define CHECK(c, msg) do { bool _ok = (c); printf("%s  %s\n", _ok ? "PASS" : "FAIL", msg); if (!_ok) Fails++; } while (0)

int main() {
    // A test sound: a 400 Hz-ish tone encoded crudely isn't needed; random
    // ADPCM bytes exercise the decoder fully.
    srand(1);
    const int cb = 4000;            // 8000 samples = 1 second at 8 kHz
    static byte ab[cb];
    for (int i = 0; i < cb; i++) ab[i] = rand() & 0xff;

    // 1. Decoder matches the original mixer (one channel = exact samples)
    {
        Channel ch; memset(&ch, 0, sizeof(ch)); ch.pb = ab; ch.pbEnd = ab + cb; ch.bSampleLast = 128;
        static byte abOld[cb * 2];
        MixChannels(&ch, 1, abOld, cb * 2);
        Voice v; memset(&v, 0, sizeof(v)); v.pb = ab; v.pbEnd = ab + cb; v.bSampleLast = 128;
        SdlSoundDevice d;
        int mism = 0, n = 0, s;
        while (d.DecodeNext(&v, &s)) {
            if ((byte)(s + 128) != abOld[n]) mism++;
            n++;
        }
        printf("     decoded %d samples with the new mixer's decoder, %d differ\n", n, mism);
        mism += (n != cb * 2);
        CHECK(mism == 0, "1. ADPCM decoding matches the original mixer sample for sample");
    }

    SdlSoundDevice d;
    d.Enable(true);
    const int cOut = 44100 * 3 / 2;     // 1.5 s of output
    static Sint16 as[cOut * 2];

    // 2. Pan
    d.PlayAdpcmEx(0, ab, cb, kcVolumeFull, -kcPanFull);
    d.Mix(as, 44100 / 2);
    double l = Rms(as, 44100/2, 0), r = Rms(as, 44100/2, 1);
    printf("     full left:  L=%.0f R=%.0f\n", l, r);
    CHECK(l > 1000 && r == 0, "2a. Panned fully left: only the left channel plays");

    d.PlayAdpcmEx(0, ab, cb, kcVolumeFull, kcPanFull / 2);
    d.Mix(as, 44100 / 2);
    l = Rms(as, 44100/2, 0); r = Rms(as, 44100/2, 1);
    printf("     half right: L=%.0f R=%.0f ratio R/L=%.2f\n", l, r, r / l);
    CHECK(fabs(r / l - 2.0) < 0.05, "2b. Panned half right: right is twice as loud as left");

    d.PlayAdpcmEx(0, ab, cb, kcVolumeFull, 0);
    d.Mix(as, 44100 / 2);
    l = Rms(as, 44100/2, 0); r = Rms(as, 44100/2, 1);
    CHECK(fabs(l - r) < 1, "2c. Centered: both channels equal");
    double rmsFull = l;

    // 3. Volume
    d.PlayAdpcmEx(0, ab, cb, kcVolumeFull / 2, 0);
    d.Mix(as, 44100 / 2);
    l = Rms(as, 44100/2, 0);
    printf("     half volume: %.0f vs full %.0f (ratio %.3f)\n", l, rmsFull, l / rmsFull);
    CHECK(fabs(l / rmsFull - 0.5) < 0.02, "3a. Half volume is half as loud");

    d.SetVolume(0);
    d.PlayAdpcmEx(0, ab, cb, kcVolumeFull, 0);
    d.Mix(as, 44100 / 2);
    CHECK(Rms(as, 44100/2, 0) == 0 && Rms(as, 44100/2, 1) == 0, "3b. Master volume 0 (Options slider) is silent");
    d.SetVolume(128);
    d.PlayAdpcmEx(0, ab, cb, kcVolumeFull, 0);
    d.Mix(as, 44100 / 2);
    l = Rms(as, 44100/2, 0);
    CHECK(fabs(l / rmsFull - 128.0/255) < 0.02, "3c. Master volume 128 is about half");
    CHECK(d.GetVolume() == 128, "3d. GetVolume reports the slider value");
    d.SetVolume(255);

    // 4. Length: 8000 source samples -> ~44100 output samples
    d.PlayAdpcmEx(0, ab, cb, kcVolumeFull, 0);
    memset(as, 0, sizeof(as));
    d.Mix(as, cOut);
    int last = 0;
    for (int i = 0; i < cOut; i++) if (as[i*2] != 0) last = i;
    printf("     last non-silent output sample: %d (expect ~44100)\n", last);
    CHECK(abs(last - 44100) < 50, "4a. One second of sound lasts one second (no pitch change)");
    CHECK(d.IsChannelFree(0), "4b. The voice is free again when the sound ends");

    // 5. 16 loud sounds together: no wrap-around, limited
    for (int i = 0; i < kcVoices; i++) d.PlayAdpcmEx(i, ab, cb, kcVolumeFull, 0);
    d.Mix(as, 44100 / 2);
    int mx = 0; bool wrapped = false;
    for (int i = 0; i < 44100 / 2 * 2; i++) { int v = abs((int)as[i]); if (v > mx) mx = v; }
    printf("     16 voices: peak %d\n", mx);
    CHECK(mx <= 32768, "5a. 16 voices at once stay in range (limited, no wrap-around)");
    CHECK(d.GetChannelCount() == 16, "5b. 16 voices available (was 4)");

    printf("\n%s\n", Fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return Fails != 0;
}
