// src/fw_report.h: Edge detection from feature report 0x20 (sw series [22] ==
// 0x44, report id at [0]) and which 0x20 the USB host gets per mode: the real
// one for a DualSense (any mode) and for an Edge shown as an Edge, the latest
// known DualSense report only for an Edge shown as a DualSense.
#include <cstring>
#include <vector>
#include "fw_report.h"
#include "test.h"

namespace {
// A Bluetooth 0x20 reply as bt.cpp caches it: id, 59 data bytes, crc32.
std::vector<uint8_t> fw_cache(uint8_t sw_series, uint8_t marker) {
    std::vector<uint8_t> r(64, marker);
    r[0] = 0x20;
    r[22] = sw_series;
    r[23] = 0x00;
    return r;
}
const auto kDs5 = fw_cache(0x0B, 0x11);
const auto kEdge = fw_cache(0x44, 0x22);
}  // namespace

TEST(fw_report_detects_edge_by_sw_series) {
    CHECK(fw_report::is_edge(kEdge.data(), kEdge.size()));
    CHECK(!fw_report::is_edge(kDs5.data(), kDs5.size()));
    // The pinned DualSense report reads as a DualSense.
    CHECK(!fw_report::is_edge(fw_report::kLatestKnownDs5, sizeof(fw_report::kLatestKnownDs5)));
    CHECK_EQ(fw_report::kLatestKnownDs5[22], 0x0B);
    // Too short, wrong id, nothing: not an Edge (connects as a DualSense).
    CHECK(!fw_report::is_edge(kEdge.data(), 22));
    auto wrong = kEdge;
    wrong[0] = 0x22;
    CHECK(!fw_report::is_edge(wrong.data(), wrong.size()));
    CHECK(!fw_report::is_edge(nullptr, 0));
}

TEST(fw_report_pinned_ds5_layout) {
    const uint8_t *r = fw_report::kLatestKnownDs5;
    CHECK_EQ(sizeof(fw_report::kLatestKnownDs5), 64u);
    CHECK_EQ(r[0], 0x20);
    CHECK(std::memcmp(r + 1, "Jul  4 2025", 11) == 0);
    CHECK(std::memcmp(r + 12, "10:38:40", 8) == 0);
    const uint32_t main_fw = r[28] | r[29] << 8 | r[30] << 16 | (uint32_t) r[31] << 24;
    CHECK_EQ(main_fw, 0x0110002Au);
    CHECK_EQ(r[44] | r[45] << 8, 0x0630);
}

TEST(fw_report_host_gets_per_mode) {
    // (is_dse, ds_mode): DualSense shown as DualSense, DualSense shown as an
    // Edge (mode DSE), Edge shown as an Edge, Edge shown as a DualSense.
    CHECK(!fw_report::host_gets_fake(false, true));
    CHECK(!fw_report::host_gets_fake(false, false));
    CHECK(!fw_report::host_gets_fake(true, false));
    CHECK(fw_report::host_gets_fake(true, true));

    uint8_t buf[63];
    // DualSense in DS5 mode: its real data, byte for byte (native triggers).
    std::memset(buf, 0, sizeof(buf));
    CHECK_EQ(fw_report::host_report(kDs5.data(), kDs5.size(), false, true, buf, 63), 63);
    CHECK(std::memcmp(buf, kDs5.data() + 1, 63) == 0);
    // Edge in DSE mode: its real data.
    std::memset(buf, 0, sizeof(buf));
    CHECK_EQ(fw_report::host_report(kEdge.data(), kEdge.size(), true, false, buf, 63), 63);
    CHECK(std::memcmp(buf, kEdge.data() + 1, 63) == 0);
    // Edge in DS5 mode: the DualSense report.
    std::memset(buf, 0, sizeof(buf));
    CHECK_EQ(fw_report::host_report(kEdge.data(), kEdge.size(), true, true, buf, 63), 63);
    CHECK(std::memcmp(buf, fw_report::kLatestKnownDs5 + 1, 63) == 0);
    CHECK_EQ(buf[21], 0x0B);  // [22] with the id
}

TEST(fw_report_host_report_bounds) {
    uint8_t buf[64];
    std::memset(buf, 0xEE, sizeof(buf));
    // Short request: clamped, nothing written past it.
    CHECK_EQ(fw_report::host_report(kEdge.data(), kEdge.size(), true, true, buf, 10), 10);
    CHECK_EQ(buf[10], 0xEE);
    // No cached report (before the Bluetooth link): 0, the caller's stub answers.
    CHECK_EQ(fw_report::host_report(nullptr, 0, true, true, buf, 63), 0);
    const uint8_t id_only[1] = {0x20};
    CHECK_EQ(fw_report::host_report(id_only, 1, false, true, buf, 63), 0);
}

// Update version [44..45]; 0 when nothing (or not a 0x20) is cached.
TEST(fw_report_update_version) {
    auto fw = fw_cache(0x0B, 0x00);
    fw[44] = 0x30; fw[45] = 0x06;
    CHECK_EQ(fw_report::update_version(fw.data(), fw.size()), 0x0630);
    CHECK_EQ(fw_report::update_version(nullptr, 0), 0);
}

// Linux hid-playstation's FF_RUMBLE report (dualsense_output_worker).
TEST(fw_report_linux_rumble_report) {
    uint8_t r[fw_report::kUsbOut02Len];
    fw_report::linux_rumble_report(r, 0x0630, false, 0xFF, 0x80);
    CHECK_EQ(r[0], 0x02);
    CHECK_EQ(r[1], 0x02);   // HAPTICS_SELECT only
    CHECK_EQ(r[3], 0x80);   // motor_right (weak)
    CHECK_EQ(r[4], 0xFF);   // motor_left (strong)
    CHECK_EQ(r[39], 0x04);  // valid_flag2 COMPATIBLE_VIBRATION2
    int nonzero = 0;
    for (uint8_t b : r) nonzero += b != 0;
    CHECK_EQ(nonzero, 5);
    fw_report::linux_rumble_report(r, 0x0214, false, 1, 2);
    CHECK_EQ(r[1], 0x03);   // + COMPATIBLE_VIBRATION
    CHECK_EQ(r[39], 0x00);
    fw_report::linux_rumble_report(r, 0x0215, false, 1, 2);
    CHECK_EQ(r[1], 0x02);
    CHECK_EQ(r[39], 0x04);
    fw_report::linux_rumble_report(r, 0x0100, true, 1, 2);  // Edge: always v2
    CHECK_EQ(r[1], 0x02);
    CHECK_EQ(r[39], 0x04);
}
