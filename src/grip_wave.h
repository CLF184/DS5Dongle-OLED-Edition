//
// The app's haptic feedback test (0xF6 0x0E [side, tenths], protocol 1.15) as
// an HD haptics waveform: a steady 125 Hz square wave in the controller's
// 3 kHz haptic sample stream, on the chosen grip(s), for exactly the asked
// time. It goes out in the 0x39 audio report's haptic block, the way a game's
// HD haptics (and Audio Auto Haptics) do: audio.cpp puts these samples in
// place of the host's while a test runs, and sends its own 0x39 reports at
// the haptic packet rate when the host isn't streaming audio.
//
// Pure, no Pico SDK: everything called per frame is always_inline, folded
// into audio_loop / grip_test_task (RAM-resident, core0;
// tools/check_ram_residency.sh), and checked by tests/host/test_grip_wave.cpp.
//

#ifndef DS5_BRIDGE_GRIP_WAVE_H
#define DS5_BRIDGE_GRIP_WAVE_H

#include <cstdint>

#include "haptics_decimate.h"

constexpr uint32_t kGripWaveRate = 3000;            // haptic samples per second (per grip)
constexpr uint32_t kGripSamplesPerTenth = kGripWaveRate / 10;
constexpr uint8_t kGripWavePeriod = 24;             // 3000 / 24 = 125 Hz
// The wave's level before the grip's haptics strength [1, 6]: strength 1 is a
// quarter of full scale, 4 and up full, as the old rumble test's 64 x strength.
constexpr float kGripWaveLevel = 0.25f;

struct GripWave {
    uint32_t remaining = 0;  // samples still to play; 0 = idle
    uint8_t phase = 0;
    bool left = false, right = false;

    __attribute__((always_inline)) inline bool active() const { return remaining != 0; }
    // Start (or replace) a test. A replacement keeps the wave's phase, so a
    // side or time change has no gap or click; from idle it starts at 0.
    void start(bool l, bool r, uint32_t samples) {
        if (!active()) phase = 0;
        left = l;
        right = r;
        remaining = (l || r) ? samples : 0;
    }
    void stop() { remaining = 0; }

    // The next sample pair (lr[0] left grip, lr[1] right) with each grip's
    // strength; both 0 once the time is up.
    __attribute__((always_inline)) inline void next(int8_t *lr, float gain_l, float gain_r) {
        if (remaining == 0) {
            lr[0] = lr[1] = 0;
            return;
        }
        remaining--;
        // The level through the host haptics' gain math; the low half is its
        // exact negative (haptic_to_s8 would give -128 against +127 at full
        // scale, a small DC offset).
        const bool high = phase < kGripWavePeriod / 2;
        if (++phase == kGripWavePeriod) phase = 0;
        const int8_t l = left ? haptic_to_s8(kGripWaveLevel, gain_l) : 0;
        const int8_t r = right ? haptic_to_s8(kGripWaveLevel, gain_r) : 0;
        lr[0] = high ? l : static_cast<int8_t>(-l);
        lr[1] = high ? r : static_cast<int8_t>(-r);
    }
    // `pairs` interleaved L/R pairs, e.g. one 0x39 report's haptic block.
    __attribute__((always_inline)) inline void fill(int8_t *out, int pairs, float gain_l, float gain_r) {
        // loop: HOT-PATH-OK — N = SAMPLE_SIZE pairs per test report, only during the haptic test
        for (int i = 0; i < pairs; i++) next(out + i * 2, gain_l, gain_r);
    }
};

// The clock for the test's own 0x39 reports while the host streams no audio:
// one report per `pairs_per_report` samples (64 = 21.33 ms, the rate the
// host's haptics go out), the first few at once so the controller's buffer
// has a head start (as the narrator's reports), then a fixed clock that
// catches up after a late report and resyncs after a stall. Time is kept in
// thirds of a microsecond so 64 / 3000 s is exact.
struct GripReportClock {
    static constexpr uint64_t kPeriod3 = 64ull * 3'000'000 / kGripWaveRate;  // 64000 = 21333.3 us
    static constexpr uint8_t kLeadReports = 2;
    bool running = false;
    uint8_t lead = 0;
    uint64_t next3 = 0;

    __attribute__((always_inline)) inline void reset() { running = false; }
    // True when a report is due now; call sent() after sending it.
    __attribute__((always_inline)) inline bool due(uint64_t now_us) {
        if (!running) {
            running = true;
            lead = kLeadReports;
            next3 = now_us * 3;
        }
        return lead > 0 || now_us * 3 >= next3;
    }
    __attribute__((always_inline)) inline void sent(uint64_t now_us) {
        const uint64_t now3 = now_us * 3;
        if (lead > 0) {
            if (--lead == 0) next3 = now3 + kPeriod3;
            return;
        }
        next3 += kPeriod3;
        if (now3 > next3 + 5 * kPeriod3) next3 = now3 + kPeriod3;  // stalled: resync
    }
};

#endif
