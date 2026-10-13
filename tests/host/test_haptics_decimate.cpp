// src/haptics_decimate.h against the WDL resampler it replaced (upstream
// awalol 4929996): the firmware's old haptics path, set up exactly as
// audio_init() and audio_loop() had it, is the reference. Every byte the
// decimator produces must match, for 1 ms USB packets and for short reads.
#include "haptics_decimate.h"
#include "resample.h"
#include "test.h"
#include <cstdint>
#include <vector>

namespace {
// Deterministic full-scale noise plus a few out-of-range values, so the clamp
// before the rate change is exercised too (Auto Haptics Mix can't exceed 1,
// but the clamp is there).
struct Lcg {
    uint32_t s;
    float next() {
        s = s * 1664525u + 1013904223u;
        return (int32_t) s / 2147483648.0f * 1.25f;  // -1.25 .. 1.25
    }
};

struct OldPath {  // audio.cpp before this change
    WDL_Resampler r;
    OldPath() {
        r.SetMode(true, 0, false);
        r.SetRates(48000, 3000);
        r.SetFeedMode(true);
        r.Prealloc(2, 48, 4);
    }
    void feed(const float *lr, int frames, float gl, float gr, std::vector<int8_t> &out) {
        WDL_ResampleSample *in_buf;
        const int n = r.ResamplePrepare(frames, 2, &in_buf);
        for (int i = 0; i < n; i++) {
            in_buf[i * 2] = std::clamp(lr[i * 2], -1.0f, 1.0f);
            in_buf[i * 2 + 1] = std::clamp(lr[i * 2 + 1], -1.0f, 1.0f);
        }
        WDL_ResampleSample ob[64];
        const int o = r.ResampleOut(ob, n, n / 4, 2);
        for (int i = 0; i < o; i++) {
            const int vl = static_cast<int>(ob[i * 2] * 127.0f * gl);
            const int vr = static_cast<int>(ob[i * 2 + 1] * 127.0f * gr);
            out.push_back((int8_t) std::clamp(vl, -128, 127));
            out.push_back((int8_t) std::clamp(vr, -128, 127));
        }
    }
};

struct NewPath {
    HapticDecimator d;
    void feed(const float *lr, int frames, float gl, float gr, std::vector<int8_t> &out) {
        for (int i = 0; i < frames; i++) {
            if (!d.take()) continue;
            out.push_back(haptic_to_s8(lr[i * 2], gl));
            out.push_back(haptic_to_s8(lr[i * 2 + 1], gr));
        }
    }
};

// packets: number of USB reads; size(k) frames in read k.
template <typename SizeFn>
void compare(uint32_t seed, int packets, float gl, float gr, SizeFn size, bool per_packet) {
    OldPath oldp;
    NewPath newp;
    Lcg rng{seed};
    std::vector<int8_t> a, b;
    float lr[48 * 2];
    int bad_packets = 0;
    for (int k = 0; k < packets; k++) {
        const int frames = size(k);
        for (int i = 0; i < frames * 2; i++) lr[i] = rng.next();
        const size_t na = a.size(), nb = b.size();
        oldp.feed(lr, frames, gl, gr, a);
        newp.feed(lr, frames, gl, gr, b);
        if (per_packet && (a.size() - na != b.size() - nb)) bad_packets++;
    }
    CHECK_EQ(bad_packets, 0);
    // WDL can hold a sample back on a very short read; the stream is the same.
    const size_t n = std::min(a.size(), b.size());
    CHECK(b.size() >= a.size());
    CHECK(b.size() - a.size() <= 2);
    size_t diff = 0;
    for (size_t i = 0; i < n; i++) diff += a[i] != b[i];
    CHECK_EQ(diff, 0u);
    CHECK(n > 0);
}
}  // namespace

TEST(haptics_decimate_phase) {
    HapticDecimator d;
    int taken = 0;
    for (int i = 0; i < 48 * 10; i++) {
        const bool t = d.take();
        CHECK_EQ(t, i % 16 == 0);
        taken += t;
    }
    CHECK_EQ(taken, 30);  // 3 samples per 1 ms packet
}

TEST(haptics_to_s8) {
    CHECK_EQ(haptic_to_s8(0.0f, 1.0f), 0);
    CHECK_EQ(haptic_to_s8(1.0f, 1.0f), 127);
    CHECK_EQ(haptic_to_s8(-1.0f, 1.0f), -127);
    CHECK_EQ(haptic_to_s8(1.5f, 1.0f), 127);     // clamped before the gain
    CHECK_EQ(haptic_to_s8(0.5f, 4.0f), 127);     // gain clips at int8
    CHECK_EQ(haptic_to_s8(-0.5f, 4.0f), -128);
    CHECK_EQ(haptic_to_s8(0.5f, 0.0f), 0);       // grip off
}

// The real stream: 48 frames per 1 ms USB packet, ten minutes, per-grip gains.
TEST(haptics_decimate_matches_wdl_1ms) {
    const float gains[][2] = {{1.0f, 1.0f}, {0.5f, 2.5f}, {3.0f, 0.0f}};
    for (const auto &g : gains)
        compare(42, 600000, g[0], g[1], [](int) { return 48; }, true);
}

// Short and uneven reads (tud_audio_read returns what has arrived).
TEST(haptics_decimate_matches_wdl_uneven) {
    Lcg sizes{7};
    compare(99, 200000, 1.0f, 1.0f,
            [&](int) { return 1 + (int) ((sizes.next() + 1.25f) / 2.5f * 47.0f); }, false);
}
