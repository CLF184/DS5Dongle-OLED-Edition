// Feature report 0x20 (firmware info): Edge detection and what the USB host
// is served. Pure (no SDK), so tests/host covers it.
//
// Reports here keep the report-id byte at [0], like bt.cpp's feature_data
// cache (the Bluetooth reply minus its 0xA3 header). Field offsets with the id
// (daidr/dualsense-tester, Linux hid-playstation): [1..11] build date,
// [12..19] build time, [20] fw type, [22] sw series, [24] hw info,
// [28] main fw version, [32..43] device info, [44] update version.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fw_report {

// Software series [22]: 0x44 on the DualSense Edge (upstream 2efc855,
// "packet[23]" there counts the 0xA3 header), 0x0B on a DualSense.
constexpr size_t kSwSeriesOffset = 22;
constexpr uint8_t kEdgeSwSeries = 0x44;

inline bool is_edge(const uint8_t *rep, size_t len) {
    return rep != nullptr && len > kSwSeriesOffset && rep[0] == 0x20 && rep[kSwSeriesOffset] == kEdgeSwSeries;
}

// The latest known DualSense firmware report, served instead of the Edge's own
// while an Edge is presented as a DualSense (controller mode DS5). Otherwise
// PS Accessories sees a DualSense ID with Edge firmware, offers a DualSense
// update and the update fails (FW-000007-5; upstream ae83907, issues #194,
// #197, #198). Bytes are upstream's capture of a real DualSense: build
// "Jul  4 2025 10:38:40", main fw 0x0110002A, update version 0x0630.
// LATEST KNOWN: once Sony ships a newer DualSense firmware, PS Accessories
// offers that update again for an Edge in DS5 mode (and it still fails). Then
// replace these bytes with a GET 0x20 capture from an updated DualSense (USB,
// or the dongle's cache minus the trailing Bluetooth crc32). Mode Auto/DSE
// presents the Edge as itself and never uses this.
constexpr uint8_t kLatestKnownDs5[64] = {
    0x20,
    0x4a, 0x75, 0x6c, 0x20, 0x20, 0x34, 0x20, 0x32,
    0x30, 0x32, 0x35, 0x31, 0x30, 0x3a, 0x33, 0x38,
    0x3a, 0x34, 0x30, 0x03, 0x00, 0x0b, 0x00, 0x11,
    0x08, 0x00, 0x00, 0x2a, 0x00, 0x10, 0x01, 0x00,
    0x28, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x30, 0x06, 0x00, 0x00, 0x01,
    0x00, 0x03, 0x00, 0x10, 0x10, 0x03, 0x00, 0x06,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// Which report the host's GET 0x20 gets. The real cached one, except for an
// Edge presented as a DualSense. A real DualSense (any mode) and an Edge in
// DSE mode keep their real data: native game triggers validate it (CLAUDE.md).
inline bool host_gets_fake(bool is_dse, bool ds_mode) { return is_dse && ds_mode; }

// Fills the host's GET 0x20 data (no report id, like usbhid asks) from the
// real cached report `real` (id at [0]). Returns the length, 0 if `real` is
// empty (the caller then serves its pre-link stub).
inline uint16_t host_report(const uint8_t *real, size_t real_len, bool is_dse, bool ds_mode, uint8_t *buf,
                            uint16_t reqlen) {
    if (real == nullptr || real_len <= 1) return 0;
    const uint8_t *src = real;
    size_t len = real_len;
    if (host_gets_fake(is_dse, ds_mode)) {
        src = kLatestKnownDs5;
        len = sizeof(kLatestKnownDs5);
    }
    uint16_t n = static_cast<uint16_t>(len - 1);
    if (n > reqlen) n = reqlen;
    memcpy(buf, src + 1, n);
    return n;
}

// Update version [44..45] little-endian (Linux hid-playstation
// dualsense_get_firmware_info, SDL firmware_version). 0 = unknown.
constexpr size_t kUpdateVersionOffset = 44;
inline uint16_t update_version(const uint8_t *rep, size_t len) {
    if (rep == nullptr || len < kUpdateVersionOffset + 2 || rep[0] != 0x20) return 0;
    return static_cast<uint16_t>(rep[kUpdateVersionOffset] | (rep[kUpdateVersionOffset + 1] << 8));
}

// The rumble report Linux hid-playstation sends for FF_RUMBLE
// (dualsense_output_worker, update_rumble set, nothing else pending): USB
// output report 0x02, 63 bytes, all zero but valid_flag0 HAPTICS_SELECT
// (0x02), then COMPATIBLE_VIBRATION2 (valid_flag2 [1 + 38] 0x04) on update
// version 2.21+ (0x0215) or an Edge, else COMPATIBLE_VIBRATION (flag0 0x01);
// motor_right [1 + 2] = weak >> 8, motor_left [1 + 3] = strong >> 8. On the
// owner's DualSense (0x0630) this, through the host forward, is the rumble
// that feels right; the grip test sends exactly it. Unknown version (0): the
// v2 mode, as every DualSense since 2021 has it.
constexpr size_t kUsbOut02Len = 63;
constexpr uint16_t kVibrationV2Version = 0x0215;  // DS_FEATURE_VERSION(2, 21)
inline void linux_rumble_report(uint8_t out[kUsbOut02Len], uint16_t update_version, bool edge, uint8_t left,
                                uint8_t right) {
    std::memset(out, 0, kUsbOut02Len);
    out[0] = 0x02;
    // Measured on hardware (2026-10-13, DualSense update version 0x0630): the
    // report Linux hid-playstation actually sends for a rumble effect through
    // the dongle is valid_flag0 = 0x03 (COMPATIBLE_VIBRATION | HAPTICS_SELECT),
    // valid_flag2 = 0, sent once; that is the steady full-strength rumble.
    // The COMPATIBLE_VIBRATION2 variant (flag0 0x02 + flag2 0x04) was silent,
    // and re-sending the report every 100 ms is what made it pulse.
    (void) update_version;
    (void) edge;
    out[1] = 0x03;
    out[1 + 2] = right;
    out[1 + 3] = left;
}

}  // namespace fw_report
