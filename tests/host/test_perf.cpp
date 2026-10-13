// src/perf.h / perf.cpp: running average with halving, max, the reset
// generation, and the 0xFC byte layout. On the host a tick is 1 us (the
// RP2040 configuration); on RP2350 it is a CPU cycle.
#include <atomic>
#include <cstring>
#include <thread>
#include "fakes.h"
#include "perf.h"
#include "test.h"

namespace {
uint32_t avg_ns(const uint8_t *r, int stage) { uint32_t v; std::memcpy(&v, r + 4 + stage * 6, 4); return v; }
uint16_t max_us(const uint8_t *r, int stage) { uint16_t v; std::memcpy(&v, r + 8 + stage * 6, 2); return v; }
}  // namespace

TEST(perf_average_max_and_layout) {
    perf_reset();
    g_fake_time_us += 5'000'000;
    for (int i = 0; i < 10; i++) perf_record(PERF_OLED, 100);
    perf_record(PERF_OLED, 1100);  // avg (1000 + 1100) / 11 = 190.9 us
    uint8_t r[kPerfReportBytes];
    CHECK_EQ(perf_serialize(r, sizeof(r)), kPerfReportBytes);
    CHECK_EQ(kPerfReportBytes, 52u);
    CHECK_EQ(r[0], 1);
    CHECK_EQ(r[1], PERF_NUM_STAGES);
    uint16_t secs;
    std::memcpy(&secs, r + 2, 2);
    CHECK_EQ(secs, 5);
    CHECK_EQ(avg_ns(r, PERF_OLED), 190909u);
    CHECK_EQ(max_us(r, PERF_OLED), 1100);
    CHECK_EQ(avg_ns(r, PERF_TUD), 0u);  // nothing recorded
    CHECK_EQ(perf_serialize(r, kPerfReportBytes - 1), 0u);
}

TEST(perf_halving_keeps_the_average) {
    perf_reset();
    for (int i = 0; i < 200000; i++) perf_record(PERF_LOOP, (i & 1) ? 3 : 5);  // avg 4 us
    uint8_t r[kPerfReportBytes];
    perf_serialize(r, sizeof(r));
    CHECK_EQ(avg_ns(r, PERF_LOOP), 4000u);
    CHECK_EQ(max_us(r, PERF_LOOP), 5);
    CHECK(g_perf[PERF_LOOP].count < 0x10000u);
    // Large samples halve on the sum instead.
    perf_reset();
    for (int i = 0; i < 5000; i++) perf_record(PERF_C1_SPEAKER, 1'000'000);
    perf_serialize(r, sizeof(r));
    CHECK_EQ(avg_ns(r, PERF_C1_SPEAKER), 1'000'000'000u);
    CHECK_EQ(max_us(r, PERF_C1_SPEAKER), 0xFFFF);  // saturates
}

TEST(perf_reset_zeroes_on_next_sample_and_in_reports) {
    perf_reset();
    perf_record(PERF_CYW43, 50);
    perf_record(PERF_C1_MIC, 70);
    perf_reset();
    uint8_t r[kPerfReportBytes];
    perf_serialize(r, sizeof(r));
    CHECK_EQ(avg_ns(r, PERF_CYW43), 0u);  // stale generation reads as zero
    CHECK_EQ(max_us(r, PERF_C1_MIC), 0);
    perf_record(PERF_CYW43, 10);
    perf_serialize(r, sizeof(r));
    CHECK_EQ(avg_ns(r, PERF_CYW43), 10000u);
    CHECK_EQ(max_us(r, PERF_CYW43), 10);
}

// F15: the 0xFC reader runs on core0 while core1 records. With 2^28-tick
// samples every fourth one halves sum and count, so a reader that pairs a new
// sum with the old count (a torn read) reports a different average.
TEST(perf_reader_never_tears) {
    perf_reset();
    constexpr uint32_t kTicks = 1u << 28;
    perf_record(PERF_C1_SPEAKER, kTicks);
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        while (!stop.load(std::memory_order_relaxed)) perf_record(PERF_C1_SPEAKER, kTicks);
    });
    const uint32_t want = (uint32_t) ((uint64_t) kTicks * 1000u);  // as perf_serialize truncates it
    int torn = 0;
    uint8_t r[kPerfReportBytes];
    for (int i = 0; i < 200000; i++) {
        perf_serialize(r, sizeof(r));
        torn += avg_ns(r, PERF_C1_SPEAKER) != want;
    }
    stop = true;
    writer.join();
    CHECK_EQ(torn, 0);
}
