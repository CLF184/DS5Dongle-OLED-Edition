//
// Stick, trigger and gyro calibration, kept in the dongle (its own flash
// sector) and applied to the OUTGOING host report only, like remap: the raw
// interrupt_in_data (OLED, live input 0xF0) stays uncorrected, so the app can
// measure from it. Nothing is ever written to the controller.
//

#ifndef DS5_BRIDGE_CALIB_H
#define DS5_BRIDGE_CALIB_H

#include <cstdint>

constexpr uint8_t kCalibProtoVer = 1;

struct __attribute__((packed)) CalibStick {
    int8_t  cx, cy;     // resting center, as an offset from 128
    uint8_t deadzone;   // radius (0..64, of 128) treated as centered
    uint8_t outer;      // radius (64..128) that counts as full tilt
};
struct __attribute__((packed)) CalibTrigger {
    uint8_t lo, hi;     // raw value read as 0 and as 255 (lo < hi)
};
struct __attribute__((packed)) CalibBody {
    CalibStick   stick[2];    // left, right
    CalibTrigger trigger[2];  // L2, R2
    int16_t      gyro[3];     // zero offset subtracted from gyro x, y, z
};
constexpr uint16_t kCalibBodySize = sizeof(CalibBody); // 18

void calib_load();
// Correct a 63-byte host report copy in place (sticks [0..3], triggers [4,5],
// gyro int16 at [15,17,19]). Fast no-op while calibration is identity.
void calib_apply(uint8_t *report);
enum class CalibSet : uint8_t { Ok, Invalid, NotSaved };
CalibSet calib_set(const uint8_t *body);
void calib_get(uint8_t out[kCalibBodySize]);

#endif // DS5_BRIDGE_CALIB_H
