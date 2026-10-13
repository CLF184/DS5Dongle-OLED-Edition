//
// 48 kHz USB haptics -> 3 kHz Bluetooth haptics (upstream awalol 4929996).
// The ratio is exactly 16:1, and the WDL resampler we used before ran in mode 0
// (linear interpolation, no filter) at integer phase, so it only ever emitted
// every 16th source frame, starting with the first. Picking that frame
// directly gives the same samples (tests/host/test_haptics_decimate.cpp checks
// it against WDL itself) without copying all 48 frames of each USB packet into
// the resampler and back out.
//
// Pure, no Pico SDK: it is inlined into audio_loop (RAM-resident, core0).
//

#ifndef DS5_BRIDGE_HAPTICS_DECIMATE_H
#define DS5_BRIDGE_HAPTICS_DECIMATE_H

#include <algorithm>
#include <cstdint>

constexpr uint8_t kHapticsDecimation = 16;  // 48000 / 3000

struct HapticDecimator {
    uint8_t phase = 0;
    // Call once per 48 kHz frame; true for the frame that becomes the next
    // 3 kHz sample. The phase carries across USB packets of any length.
    __attribute__((always_inline)) inline bool take() {
        const bool emit = phase == 0;
        if (++phase == kHapticsDecimation) phase = 0;
        return emit;
    }
};

// One haptic sample (-1..1 after Auto Haptics) to the int8 the DS5 plays, with
// the grip gain applied after the rate change as upstream a55fd46. Same
// expression as the old post-resampler code, so the bytes are identical.
__attribute__((always_inline)) inline int8_t haptic_to_s8(float x, float gain) {
    const int v = static_cast<int>(std::clamp(x, -1.0f, 1.0f) * 127.0f * gain);
    return static_cast<int8_t>(std::clamp(v, -128, 127));
}

#endif
