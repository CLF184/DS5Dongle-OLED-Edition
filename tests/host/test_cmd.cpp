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

TEST(cmd_identity_reports_protocol_1_15) {
    fresh();
    uint8_t id[47]{};
    CHECK_EQ(pico_cmd_get(PICO_CMD_IDENTITY, id, sizeof(id)), 47);
    CHECK(std::memcmp(id, "DS5O", 4) == 0);
    CHECK_EQ(id[4], 1);
    CHECK_EQ(id[5], 15);
    CHECK_EQ(id[6], 49);
    CHECK(id[46] & 0x02);  // the 1.13 config fields
    CHECK(id[46] & 0x04);  // the 1.14 PC sleep options
    CHECK(id[46] & 0x08);  // the 1.15 timed grip test
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

// 0xF7: a 63-byte GET keeps the 1.6 layout (42-byte body, remap block at 42),
// which every app reads the remap from; a shorter one gets the whole body.
TEST(cmd_config_get_layouts) {
    fresh();
    Config_body b = get_config();
    b.trigger_reduce = 6;
    b.status_gpio_pin = 4;
    set_config(b);
    uint8_t r[63]{};
    CHECK_EQ(pico_cmd_get(0xF7, r, 63), 63);
    CHECK(std::memcmp(r, &get_config(), kConfigBodyV16) == 0);
    CHECK_EQ(r[42], 'R');
    CHECK_EQ(r[43], 'M');
    std::memset(r, 0, sizeof(r));
    CHECK_EQ(pico_cmd_get(0xF7, r, sizeof(Config_body)), sizeof(Config_body));
    CHECK(std::memcmp(r, &get_config(), sizeof(Config_body)) == 0);
    CHECK_EQ(r[42], 6);
    CHECK_EQ(r[47], 4);
    CHECK_EQ(pico_cmd_get(0xF7, r, 62), sizeof(Config_body));
    CHECK_EQ(pico_cmd_get(0xF7, r, kConfigBodyV16), kConfigBodyV16);  // what apps before 1.13 ask for
    CHECK_EQ(pico_cmd_get(0xF7, r, 38), 38);                            // the narrator read
}

// The app's config write (0x01) carries the whole 49-byte body.
TEST(cmd_config_set_takes_the_whole_body) {
    fresh();
    Config_body b = get_config();
    b.lock_volume = 1;
    b.speaker_select = 2;
    uint8_t msg[1 + sizeof(Config_body)];
    msg[0] = 0x01;
    std::memcpy(msg + 1, &b, sizeof(b));
    pico_cmd_set(PICO_CMD_SET, msg, sizeof(msg));
    CHECK_EQ(last_result(), CMD_RESULT_OK);
    CHECK_EQ(get_config().lock_volume, 1);
    CHECK_EQ(get_config().speaker_select, 2);
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
    CHECK_EQ(khz, 150000u);
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

TEST(cmd_no_reply_needs_more_than_63_bytes) {
    // Both transports cap a reply at 63 bytes: a reply that wants more loses
    // its tail without any error (0xF5 lost core1's health that way). Ask
    // every id with room to spare and check nothing is cut off. The ids known
    // to do it are listed (0xFE, the mic-debug frame dump, wants 82: its last
    // 19 frame bytes never arrive); a new one fails.
    fresh();
    const uint8_t known_long[] = {0xFE};
    for (int rid = 0; rid < 256; rid++) {
        std::unique_ptr<uint8_t[]> buf(new uint8_t[1024]);
        const uint16_t n = pico_cmd_get((uint8_t) rid, buf.get(), 1024);
        bool known = false;
        for (uint8_t k : known_long) known |= k == rid;
        if ((n > 63) != known) std::fprintf(stderr, "  GET 0x%02X wants %u bytes (known %d)\n", rid, n, known);
        CHECK((n > 63) == known);
    }
}

TEST(cmd_f5_layout) {
    fresh();
    for (uint16_t len = 0; len <= 200; len++) {  // every length: min(len, 63)
        std::unique_ptr<uint8_t[]> buf(new uint8_t[200]);
        CHECK_EQ(pico_cmd_get(0xF5, buf.get(), len), len < 63 ? len : 63);
    }
    uint8_t r[63]{};
    auto u32 = [&](int i) {
        uint32_t x;
        std::memcpy(&x, r + 4 * i, 4);
        return x;
    };
    auto heap_kib = [&] { return (uint16_t) (r[61] | r[62] << 8); };
    g_fake_c1_flags = 0x01;
    g_fake_c1_free_heap = 100000;
    g_fake_c1_stage = 0;
    CHECK_EQ(pico_cmd_get(0xF5, r, 63), 63);
    // The 15 counters keep their offsets (fakes.cpp values).
    CHECK_EQ(u32(0), 8u);   // USB frames
    CHECK_EQ(u32(1), 9u);   // BT out
    CHECK_EQ(u32(2), 1u);   // drop raw
    CHECK_EQ(u32(5), 4u);   // core1 loops
    CHECK_EQ(u32(10), 11u); // mic PLC
    CHECK_EQ(r[60], 0x81);  // started + this layout
    CHECK_EQ(heap_kib(), 97);
    g_fake_c1_flags = 0x01 | 0x02 | 0x04 | 0x10;  // unknown bits stay out
    g_fake_c1_free_heap = 12 * 1024 + 1023;
    pico_cmd_get(0xF5, r, 63);
    CHECK_EQ(r[60], 0x87);
    CHECK_EQ(heap_kib(), 12);
    g_fake_c1_free_heap = 500;  // under 1 KiB still reads as known
    pico_cmd_get(0xF5, r, 63);
    CHECK_EQ(heap_kib(), 1);
    g_fake_c1_free_heap = 0xFFFFFFFFu;
    pico_cmd_get(0xF5, r, 63);
    CHECK_EQ(heap_kib(), 0xFFFF);
    g_fake_c1_flags = 0;  // before core1 starts
    g_fake_c1_free_heap = 0;
    pico_cmd_get(0xF5, r, 63);
    CHECK_EQ(r[60], 0x80);
    CHECK_EQ(heap_kib(), 0);
    // Core1's start-up stage in bits 3..6, beside the flags (issue #15).
    g_fake_c1_flags = 0x01 | 0x02;
    g_fake_c1_free_heap = 100000;
    for (uint32_t stage = 0; stage <= 9; stage++) {
        g_fake_c1_stage = stage;
        pico_cmd_get(0xF5, r, 63);
        CHECK_EQ(r[60], 0x80 | stage << 3 | 0x03);
        CHECK_EQ((r[60] >> 3) & 0x0F, stage);
        CHECK_EQ(r[60] & 0x07, 0x03);  // the flags old apps read are unchanged
        CHECK_EQ(heap_kib(), 97);
        CHECK_EQ(u32(5), 4u);  // and the counters
    }
    g_fake_c1_stage = 9;
    g_fake_c1_flags = 0x01;
    pico_cmd_get(0xF5, r, 63);
    CHECK_EQ(r[60], 0xC9);  // running: started, stage 9, this layout
    g_fake_c1_stage = 0x1F;  // never more than 4 bits: bit7 stays the marker
    g_fake_c1_flags = 0;
    pico_cmd_get(0xF5, r, 63);
    CHECK_EQ(r[60], 0xF8);
    g_fake_c1_flags = 1;
    g_fake_c1_free_heap = 100000;
    g_fake_c1_stage = 9;
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
    CHECK_EQ(id[5], PICO_PROTO_MINOR);
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

// Protocol 1.14: PC sleep options. Identity [46] bit2 and [47] the bits; 0xF6
// 0x17 [flags] validates, saves on its own (other unsaved edits stay unsaved)
// and survives a reload; a 47-byte identity read stays 47 bytes.
TEST(cmd_power_flags_get_set) {
    fresh();
    CHECK(config_save());  // something valid in flash, so 0x17 saves only its bits
    uint8_t id[63]{};
    CHECK_EQ(pico_cmd_get(PICO_CMD_IDENTITY, id, 47), 47);
    CHECK_EQ(pico_cmd_get(PICO_CMD_IDENTITY, id, 48), 48);
    CHECK(id[46] & 0x04);
    CHECK_EQ(id[47], POWER_FLAGS_DEFAULT);  // controller off on sleep, no wake
    Config_body b = get_config();
    b.inactive_time = 50;  // an unsaved edit
    set_config(b);
    const uint8_t on[] = {0x17, POWER_SLEEP_CONTROLLER_OFF | POWER_PS_WAKES_PC};
    pico_cmd_set(PICO_CMD_SET, on, sizeof(on));
    CHECK_EQ(last_result(), CMD_RESULT_PENDING);
    settle();
    CHECK_EQ(last_result(), CMD_RESULT_OK);
    pico_cmd_get(PICO_CMD_IDENTITY, id, 48);
    CHECK_EQ(id[47], 0x03);
    CHECK(config_unsaved());  // inactive_time still unsaved
    config_load();
    CHECK_EQ(config_power_flags(), 0x03);
    CHECK_EQ(get_config().inactive_time, 30);  // never saved
    const uint8_t bad[] = {0x17, 0x04};
    pico_cmd_set(PICO_CMD_SET, bad, sizeof(bad));
    CHECK_EQ(last_result(), CMD_RESULT_INVALID);
    const uint8_t none[] = {0x17};
    pico_cmd_set(PICO_CMD_SET, none, sizeof(none));
    CHECK_EQ(last_result(), CMD_RESULT_INVALID);
    const uint8_t off[] = {0x17, 0x00};
    pico_cmd_set(PICO_CMD_SET, off, sizeof(off));
    settle();
    config_load();
    CHECK_EQ(config_power_flags(), 0x00);
    g_fake_flash_fail = true;
    pico_cmd_set(PICO_CMD_SET, on, sizeof(on));
    settle();
    CHECK_EQ(last_result(), CMD_RESULT_FAILED);
    g_fake_flash_fail = false;
}

// 0xF6 0x03 goes through usb_power_reconnect (so the suspend it causes is
// never taken for a PC sleep), not a bare tud_disconnect/connect.
TEST(cmd_reconnect_uses_usb_power) {
    fresh();
    const int before = g_fake_usb_reconnects;
    const uint8_t rc[] = {0x03};
    pico_cmd_set(PICO_CMD_SET, rc, sizeof(rc));
    CHECK_EQ(g_fake_usb_reconnects, before + 1);
}

// Protocol 1.15: 0xF6 0x0E [side, tenths] holds the grip test's rumble for the
// whole time (re-sent every 100 ms, no gap), then stops it; no duration byte
// keeps the old 0.5 s; > 50 clamps to 5 s; 0 or a new 0x0E replaces it.
namespace {
int grip_run(uint64_t us) {  // advance in 10 ms steps; reports sent meanwhile
    const size_t before = g_fake_rumbles.size();
    for (uint64_t t = 0; t < us; t += 10'000) {
        g_fake_time_us += 10'000;
        cmd_task();
    }
    return (int) (g_fake_rumbles.size() - before);
}
bool grip_running() {
    uint8_t l, r;
    return grip_test_hold(&l, &r);
}
}  // namespace

TEST(cmd_grip_test_default_half_second) {
    fresh();
    g_fake_rumbles.clear();
    const uint8_t old_app[] = {0x0E, 2};
    pico_cmd_set(PICO_CMD_SET, old_app, sizeof(old_app));
    CHECK_EQ(last_result(), CMD_RESULT_PENDING);
    grip_run(10'000);
    CHECK_EQ(last_result(), CMD_RESULT_OK);
    CHECK(g_fake_rumbles.size() >= 1);
    CHECK_EQ(g_fake_rumbles[0].first, 64);
    CHECK_EQ(g_fake_rumbles[0].second, 64);
    grip_run(400'000);
    CHECK(grip_running());
    grip_run(200'000);
    CHECK(!grip_running());
    CHECK(g_fake_rumbles.back() == std::make_pair((uint8_t) 0, (uint8_t) 0));
}

TEST(cmd_grip_test_holds_steadily) {
    fresh();
    g_fake_rumbles.clear();
    const uint8_t left3s[] = {0x0E, 0, 30};
    pico_cmd_set(PICO_CMD_SET, left3s, sizeof(left3s));
    grip_run(10'000);
    CHECK_EQ(last_result(), CMD_RESULT_OK);
    // Refreshed every 100 ms for 3 s, never a zero in between.
    grip_run(2'900'000);
    CHECK(grip_running());
    CHECK(g_fake_rumbles.size() >= 29);
    for (auto &r : g_fake_rumbles) {
        CHECK_EQ(r.first, 64);
        CHECK_EQ(r.second, 0);
    }
    // A host report meanwhile carries the test's levels.
    uint8_t l = 0, r = 9;
    CHECK(grip_test_hold(&l, &r));
    CHECK_EQ(l, 64);
    CHECK_EQ(r, 0);
    grip_run(200'000);
    CHECK(!grip_running());
    CHECK(g_fake_rumbles.back() == std::make_pair((uint8_t) 0, (uint8_t) 0));
    const size_t n = g_fake_rumbles.size();
    grip_run(500'000);
    CHECK_EQ(g_fake_rumbles.size(), n);  // nothing after the stop
}

TEST(cmd_grip_test_clamps_to_5s) {
    fresh();
    const uint8_t long_one[] = {0x0E, 1, 200};
    pico_cmd_set(PICO_CMD_SET, long_one, sizeof(long_one));
    grip_run(4'900'000);
    CHECK(grip_running());
    grip_run(200'000);
    CHECK(!grip_running());
    const uint8_t shortest[] = {0x0E, 1, 1};
    pico_cmd_set(PICO_CMD_SET, shortest, sizeof(shortest));
    grip_run(50'000);
    CHECK(grip_running());
    grip_run(100'000);
    CHECK(!grip_running());
}

TEST(cmd_grip_test_stop_and_replace) {
    fresh();
    g_fake_rumbles.clear();
    const uint8_t left[] = {0x0E, 0, 30};
    const uint8_t right[] = {0x0E, 1, 30};
    const uint8_t stop[] = {0x0E, 2, 0};
    pico_cmd_set(PICO_CMD_SET, left, sizeof(left));
    grip_run(1'000'000);
    // Another side replaces it: one report sets the left motor to 0 and the
    // right one on, and the right one runs a fresh 3 s.
    pico_cmd_set(PICO_CMD_SET, right, sizeof(right));
    g_fake_rumbles.clear();
    grip_run(10'000);
    CHECK(!g_fake_rumbles.empty());
    CHECK(g_fake_rumbles.back() == std::make_pair((uint8_t) 0, (uint8_t) 64));
    grip_run(2'500'000);
    CHECK(grip_running());  // the new 3 s, not what was left of the first
    // Duration 0 stops at once.
    g_fake_rumbles.clear();
    pico_cmd_set(PICO_CMD_SET, stop, sizeof(stop));
    grip_run(10'000);
    CHECK_EQ(last_result(), CMD_RESULT_OK);
    CHECK(!grip_running());
    CHECK_EQ(g_fake_rumbles.size(), 1u);
    CHECK(g_fake_rumbles[0] == std::make_pair((uint8_t) 0, (uint8_t) 0));
    // A stop with nothing running is fine and sends nothing.
    pico_cmd_set(PICO_CMD_SET, stop, sizeof(stop));
    grip_run(10'000);
    CHECK_EQ(last_result(), CMD_RESULT_OK);
    CHECK_EQ(g_fake_rumbles.size(), 1u);
    // Bad side, no controller.
    const uint8_t bad[] = {0x0E, 3, 30};
    pico_cmd_set(PICO_CMD_SET, bad, sizeof(bad));
    CHECK_EQ(last_result(), CMD_RESULT_INVALID);
    g_fake_bt_connected = false;
    pico_cmd_set(PICO_CMD_SET, left, sizeof(left));
    grip_run(10'000);
    CHECK_EQ(last_result(), CMD_RESULT_FAILED);
    CHECK(!grip_running());
    // A disconnect mid-test ends it.
    g_fake_bt_connected = true;
    pico_cmd_set(PICO_CMD_SET, left, sizeof(left));
    grip_run(10'000);
    CHECK(grip_running());
    g_fake_bt_connected = false;
    grip_run(10'000);
    CHECK(!grip_running());
    g_fake_bt_connected = true;
}
