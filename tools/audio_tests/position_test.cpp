// Tests CalcSfxPosition (game/soundmgr.cpp): volume and left/right pan of a
// sound relative to the view. Build and run with tools/audio_tests/run.sh.
#include <stdio.h>
#include <stdlib.h>
namespace wi { void CalcSfxPosition(long, long, long, long, long, long, int *, int *); }
using namespace wi;
static int Fails = 0;
#define T 256L  // world units per tile
static void Case(const char *name, double txSound, double tySound, bool (*ok)(int, int)) {
    // View: top-left at tile (30, 30), 25 x 18 tiles
    int v, p;
    CalcSfxPosition((long)(txSound * T), (long)(tySound * T), 30 * T, 30 * T, 25 * T, 18 * T, &v, &p);
    bool f = ok(v, p);
    printf("%s  %-52s volume %3d/256  pan %+4d/256\n", f ? "PASS" : "FAIL", name, v, p);
    if (!f) Fails++;
}
int main() {
    Case("center of the screen", 42.5, 39, [](int v, int p) { return v == 256 && p == 0; });
    Case("left edge of the screen", 30, 39, [](int v, int p) { return v == 256 && p == -128; });
    Case("right edge of the screen", 55, 39, [](int v, int p) { return v == 256 && p == 128; });
    Case("12 tiles right of the screen", 67, 39, [](int v, int p) { return v == 128 && p > 128 && p <= 205; });
    Case("12 tiles left of the screen", 18, 39, [](int v, int p) { return v == 128 && p < -128 && p >= -205; });
    Case("far right (60 tiles away)", 115, 39, [](int v, int p) { return v == 42 && p == 204; });
    Case("straight above, 12 tiles (no left/right)", 42.5, 18, [](int v, int p) { return v == 128 && p == 0; });
    Case("very far corner (0,0): quiet but audible", 0, 0, [](int v, int p) { return v >= 32 && v < 64 && p == -204; });
    Case("nearer is louder than farther (same side)", 70, 39, [](int v, int p) {
        int v2, p2; CalcSfxPosition(100 * T, 39 * T, 30 * T, 30 * T, 25 * T, 18 * T, &v2, &p2); return v > v2; });
    printf("\n%s\n", Fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return Fails != 0;
}
