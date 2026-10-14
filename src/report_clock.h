// The clock for the 0x39 audio reports the dongle sends on its own while the
// host streams no audio (the haptic feedback test's and the narrator's): one
// report per 64 haptic samples at 3 kHz (21.33 ms, two 10.67 ms speaker
// frames: the rate the host's reports go out and the controller plays them),
// the first few at once so the controller's buffer has a head start, then a
// fixed clock that catches up after a late report and resyncs after a stall.
// Time is kept in thirds of a microsecond so 64 / 3000 s is exact.
//
// Pure, always_inline (folded into the RAM-resident grip_test_task;
// tools/check_ram_residency.sh), checked by tests/host/test_grip_wave.cpp.
#ifndef DS5_BRIDGE_REPORT_CLOCK_H
#define DS5_BRIDGE_REPORT_CLOCK_H

#include <cstdint>

struct AudioReportClock {
    static constexpr uint64_t kPeriod3 = 64ull * 3'000'000 / 3000;  // 64000 = 21333.3 us
    uint8_t lead_reports;
    bool running = false;
    uint8_t lead = 0;
    uint64_t next3 = 0;

    explicit constexpr AudioReportClock(uint8_t lead_reports_ = 2) : lead_reports(lead_reports_) {}

    __attribute__((always_inline)) inline void reset() { running = false; }
    // True when a report is due now; call sent() after sending it.
    __attribute__((always_inline)) inline bool due(uint64_t now_us) {
        if (!running) {
            running = true;
            lead = lead_reports;
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

#endif // DS5_BRIDGE_REPORT_CLOCK_H
