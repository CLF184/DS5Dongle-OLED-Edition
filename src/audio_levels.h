// Core0 audio observations. Reading a display meter never changes DSP state.
#pragma once
#include <cstdint>

struct AudioPeak {
    uint16_t peak = 0;
    uint64_t at_us = 0;

    uint16_t level(uint64_t now_us) const {
        const uint64_t age = (now_us - at_us) >> 10;
        if (age >= 256) return 0;
        return static_cast<uint16_t>(peak - static_cast<uint32_t>(peak) * age / 256);
    }

    void record(uint16_t frame_peak, uint64_t now_us) {
        if (frame_peak > level(now_us)) {
            peak = frame_peak;
            at_us = now_us;
        }
    }

    uint8_t read(uint64_t now_us) const {
        const uint16_t value = level(now_us) >> 7;
        return static_cast<uint8_t>(value > 255 ? 255 : value);
    }
};

// Only raw ch3/ch4 peaks from the current USB chunk enter this detector.
// Its threshold and 100-chunk fallback delay are unchanged.
struct NativeHapticsPresence {
    static constexpr uint16_t kThreshold = 256;
    static constexpr int kTimeout = 100;
    int silent_chunks = kTimeout * 2;

    bool silent() const { return silent_chunks >= kTimeout; }
    void record(uint16_t frame_peak) {
        if (frame_peak > kThreshold) silent_chunks = 0;
        else if (silent_chunks < kTimeout * 2) ++silent_chunks;
    }
};
