// Per-stage timing counters (protocol 1.11): how long each part of core0's
// main loop and core1's audio work takes, average and worst case, for the
// app's dashboard (0xFC [11..62]) and for A/B tests of build settings.
//
// Ticks are CPU cycles on RP2350 (the core's own DWT cycle counter, enabled
// per core by perf_core_init) and microseconds on RP2040, which has no cycle
// counter. Each stage has exactly one writer core; the reader (the 0xFC GET on
// core0) copes with a sample landing mid-read. A reset (0xF6 0x15) only bumps
// a generation number; each writer zeroes its own stage on its next sample,
// so no core ever writes another core's counters.
#ifndef DS5_BRIDGE_PERF_H
#define DS5_BRIDGE_PERF_H

#include <stddef.h>
#include <stdint.h>
#include "pico.h"
#include "hardware/timer.h"
#if PICO_RP2350
#include "hardware/structs/m33.h"
#endif

enum PerfStage : uint8_t {
    PERF_LOOP = 0,    // core0: one whole main-loop pass (start to start)
    PERF_CYW43,       // core0: cyw43_arch_poll (BTstack) + BT connection watchdog
    PERF_TUD,         // core0: tud_task (TinyUSB, incl. control-request callbacks)
    PERF_AUDIO,       // core0: audio_loop + voice_task (USB audio in, 0x39 out)
    PERF_INPUT,       // core0: interrupt_loop + calib_service + cmd_task (reports)
    PERF_OLED,        // core0: oled_loop (render + flush)
    PERF_C1_SPEAKER,  // core1: one speaker frame (resample + Opus encode)
    PERF_C1_MIC,      // core1: one mic frame (Opus decode or concealment)
    PERF_NUM_STAGES
};

#if PICO_RP2350
#define PERF_TICKS_PER_US (SYS_CLOCK_KHZ / 1000u)
#else
#define PERF_TICKS_PER_US 1u
#endif

struct PerfStat {
    volatile uint32_t sum;    // ticks, halved together with count when full
    volatile uint32_t count;
    volatile uint32_t max;    // ticks
    volatile uint32_t gen;    // reset generation these numbers belong to
};

extern PerfStat g_perf[PERF_NUM_STAGES];
extern volatile uint32_t g_perf_gen;
// RP2350: false if a core's cycle counter didn't run (the Secure-state DWT
// can be stopped by debug-authentication OTP settings); then both cores use
// the microsecond timer, scaled to ticks. 0xFC [11] bit 7 reports it.
extern volatile bool g_perf_timer_fallback;

// Starts this core's cycle counter (RP2350). Call once on each core.
static inline void perf_core_init() {
#if PICO_RP2350
    m33_hw->demcr |= M33_DEMCR_TRCENA_BITS;
    m33_hw->dwt_ctrl |= M33_DWT_CTRL_CYCCNTENA_BITS;
    const uint32_t a = m33_hw->dwt_cyccnt;
    busy_wait_at_least_cycles(16);
    if (m33_hw->dwt_cyccnt == a) g_perf_timer_fallback = true;
#endif
}

static __force_inline uint32_t perf_now() {
#if PICO_RP2350
    if (__builtin_expect(g_perf_timer_fallback, false)) return timer_hw->timerawl * PERF_TICKS_PER_US;
    return m33_hw->dwt_cyccnt;
#else
    return timer_hw->timerawl;
#endif
}

// Inline (no call into flash) so core1's RAM-resident code can use it.
static __force_inline void perf_record(PerfStage stage, uint32_t ticks) {
    PerfStat &s = g_perf[stage];
    const uint32_t gen = g_perf_gen;
    uint32_t sum = s.sum, count = s.count, max = s.max;
    if (s.gen != gen) {
        sum = count = max = 0;
        s.gen = gen;
    }
    // A sample is under 2^31 ticks (the watchdog fires long before), so this
    // can't overflow; past 2^31 or 65536 samples both halve, which keeps the
    // average and weighs older samples down (a recent-history average).
    sum += ticks;
    count++;
    if (sum >= 0x80000000u || count >= 0x10000u) {
        sum >>= 1;
        count >>= 1;
    }
    if (ticks > max) max = ticks;
    s.sum = sum;
    s.count = count;
    s.max = max;
}

// Starts a new measurement window (all stages, both cores).
void perf_reset();

// The 0xFC extension: [0] layout version (1; bit 7 set = microsecond timer
// instead of the cycle counter, see g_perf_timer_fallback), [1] stage count,
// [2..3] u16 seconds since the last reset (saturating), then per stage
// (PerfStage order) u32 average ns + u16 max us (saturating). Returns the
// bytes written (52), or 0 when cap is too small.
constexpr size_t kPerfReportBytes = 4 + PERF_NUM_STAGES * 6;
size_t perf_serialize(uint8_t *out, size_t cap);

#endif  // DS5_BRIDGE_PERF_H
