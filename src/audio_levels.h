//
// Audio observations made in audio_loop (core0, RAM-resident): the Audio Auto
// Haptics native-haptics detector. Pure, no Pico SDK, so tests/host checks it;
// always_inline so it folds into audio_loop (an out-of-line copy would sit in
// flash; tools/check_ram_residency.sh checks for one).
// Based on CLF184's PR #18.
//

#ifndef DS5_BRIDGE_AUDIO_LEVELS_H
#define DS5_BRIDGE_AUDIO_LEVELS_H

#include <cstdint>

// Is the host sending its own HD haptics (ch3/ch4)? Fed the raw ch3/ch4 peak
// of each USB chunk only, never a held meter peak: the VU meter's peak only
// fell when someone read it, so with the OLED off and no app polling, one
// loud haptic moment kept Fallback mode from ever engaging. Threshold and the
// 100-chunk delay are unchanged.
struct NativeHapticsPresence {
    static constexpr uint16_t kThreshold = 256;
    static constexpr int kTimeout = 100;  // chunks below kThreshold before "silent"
    int silent_chunks = kTimeout * 2;

    __attribute__((always_inline)) inline bool silent() const { return silent_chunks >= kTimeout; }
    __attribute__((always_inline)) inline void record(uint16_t chunk_peak) {
        if (chunk_peak > kThreshold) silent_chunks = 0;
        else if (silent_chunks < kTimeout * 2) ++silent_chunks;
    }
};

#endif  // DS5_BRIDGE_AUDIO_LEVELS_H
