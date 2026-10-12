// src/cmd.cpp, the configurator protocol: every GET id at every request length
// stays inside the caller's buffer (ASan), random SETs never crash, and the
// protocol 1.11 additions (0xFC timings, 0xF6 0x15) have the documented layout.
#include <cstring>
#include <memory>
#include <random>
#include "cmd.h"
#include "config.h"
#include "calib.h"
#include "fakes.h"
#include "perf.h"
#include "remap.h"
#include "test.h"

namespace {
void fresh() {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    g_fake_bt_connected = true;
    config_load();
    remap_load();
    calib_load();
}
uint8_t last_result() {
    uint8_t id[46];
    pico_cmd_get(PICO_CMD_IDENTITY, id, sizeof(id));
    return id[11];
}
void settle() {  // let a deferred command run (reboot/BOOTSEL wait 50 ms)
    for (int i = 0; i < 3; i++) {
        g_fake_time_us += 100'000;
        cmd_task();
    }
}
}  // namespace

TEST(cmd_identity_reports_protocol_1_12) {
    fresh();
    uint8_t id[46]{};
    CHECK_EQ(pico_cmd_get(PICO_CMD_IDENTITY, id, sizeof(id)), 46);
    CHECK(std::memcmp(id, "DS5O", 4) == 0);
    CHECK_EQ(id[4], 1);
    CHECK_EQ(id[5], 12);
    CHECK_EQ(id[6], sizeof(Config_body));
    CHECK(id[29] & 0x80);  // timings in 0xFC
    CHECK(std::memcmp(id + 30, "Host", 5) == 0);
}

TEST(cmd_every_get_stays_in_bounds) {
    fresh();
    // Both transports hand pico_cmd_get a 64-byte buffer (vendor_itf.cpp s_buf,
    // TinyUSB's control buffer) with reqlen = wLength capped to 63: ASan guards
    // the 64 bytes, and a canary shows bytes written past reqlen. Writing there
    // is harmless with those buffers but is a bug waiting for a smaller one;
    // the ids known to do it are listed, a new one fails.
    constexpr uint16_t kBuf = 64;
    const uint8_t known_overreach[] = {0xFD};  // writes [0..15] before checking reqlen
    for (int rid = 0; rid < 256; rid++) {
        bool overreach = false;
        for (uint16_t len = 0; len <= 63; len++) {
            std::unique_ptr<uint8_t[]> buf(new uint8_t[kBuf]);
            std::memset(buf.get(), 0xA5, kBuf);
            const uint16_t n = pico_cmd_get((uint8_t) rid, buf.get(), len);
            if (n > len) {
                std::fprintf(stderr, "  GET 0x%02X with %u bytes answered %u\n", rid, len, n);
                CHECK(n <= len);
            }
            for (uint16_t i = len; i < kBuf; i++) overreach |= buf[i] != 0xA5;
        }
        bool known = false;
        for (uint8_t k : known_overreach) known |= k == rid;
        if (overreach != known) std::fprintf(stderr, "  GET 0x%02X writes past reqlen: %d (known %d)\n", rid, overreach, known);
        CHECK(overreach == known);
    }
}

TEST(cmd_fc_timings_layout_and_reset) {
    fresh();
    uint8_t r[63]{};
    CHECK_EQ(pico_cmd_get(0xFC, r, 11), 11);  // what apps before 1.11 ask for
    perf_reset();
    perf_record(PERF_TUD, 40);
    CHECK_EQ(pico_cmd_get(0xFC, r, 63), 63);
    uint32_t khz;
    std::memcpy(&khz, r, 4);
    CHECK_EQ(khz, 320000u);
    CHECK_EQ(r[11], 1);
    CHECK_EQ(r[12], 8);
    uint32_t avg;
    uint16_t mx;
    std::memcpy(&avg, r + 15 + 6 * PERF_TUD, 4);
    std::memcpy(&mx, r + 19 + 6 * PERF_TUD, 2);
    CHECK_EQ(avg, 40000u);
    CHECK_EQ(mx, 40);
    const uint8_t reset[] = {0x15};
    pico_cmd_set(PICO_CMD_SET, reset, sizeof(reset));
    CHECK_EQ(last_result(), CMD_RESULT_OK);
    pico_cmd_get(0xFC, r, 63);
    std::memcpy(&avg, r + 15 + 6 * PERF_TUD, 4);
    CHECK_EQ(avg, 0u);
}

TEST(cmd_random_sets_never_crash) {
    fresh();
    std::mt19937 rng(42);
    for (int n = 0; n < 20000; n++) {
        const uint16_t len = 1 + rng() % 63;
        std::unique_ptr<uint8_t[]> buf(new uint8_t[len]);
        for (uint16_t i = 0; i < len; i++) buf[i] = (uint8_t) rng();
        buf[0] = (uint8_t) (rng() % 0x18);  // mostly real function ids
        if (buf[0] == 0x03) buf[0] = 0x01;  // USB reconnect sleeps 150 ms
        if (len > 2 && (rng() & 1)) {       // and valid-looking framing half the time
            const uint8_t magic[][2] = {{'R', 'M'}, {'R', 'X'}, {'C', 'L'}, {'C', 'V'}};
            const auto &m = magic[rng() % 4];
            buf[1] = m[0];
            buf[2] = m[1];
        }
        pico_cmd_set(PICO_CMD_SET, buf.get(), len);
        settle();
    }
    // Still answering, and still the same protocol.
    uint8_t id[46];
    CHECK_EQ(pico_cmd_get(PICO_CMD_IDENTITY, id, sizeof(id)), 46);
    CHECK_EQ(id[5], 12);
}

TEST(cmd_only_f6_runs_functions) {
    fresh();
    const int reboots = g_fake_reboots;
    const uint8_t reboot[] = {0x07};
    for (int rid = 0; rid < 256; rid++) {
        if (rid == PICO_CMD_SET) continue;
        pico_cmd_set((uint8_t) rid, reboot, sizeof(reboot));
    }
    settle();
    CHECK_EQ(g_fake_reboots, reboots);
    pico_cmd_set(PICO_CMD_SET, reboot, sizeof(reboot));
    settle();
    CHECK_EQ(g_fake_reboots, reboots + 1);
}
