//
// Stick response curves (protocol 1.10): pure integer math, no Pico SDK, so it
// can be compiled on a PC to check the app's copy (DS5Dongle-OLED-Config-Web
// src/protocol/stickCurve.ts implements the same steps and must give the same
// bytes).
//
// A curve maps the stick's radial tilt (0..1, after calibration) to a new
// tilt; the direction is kept. Everything is in Q12 (4096 = full tilt).
//   Linear      identity (skipped entirely)
//   Precise     y = x^1.8   (slow start, fine aim)
//   Steady      y = x^1.35  (milder slow start)
//   Aggressive  y = x^0.6   (quick start)
//   Digital     y = x^0.3   (almost on/off)
//   Custom      piecewise linear through (0,0), 4 points, (1,1); the points
//               are bytes 0..255 (255 = full tilt), x and y non-decreasing.
// The presets are 33-entry tables (x = i/32) of round(4096 * (i/32)^g),
// linearly interpolated, so no pow() runs anywhere.
//

#ifndef DS5_BRIDGE_STICK_CURVE_H
#define DS5_BRIDGE_STICK_CURVE_H

#include <cstdint>

enum StickCurveType : uint8_t {
    kCurveLinear = 0,
    kCurvePrecise = 1,
    kCurveSteady = 2,
    kCurveAggressive = 3,
    kCurveDigital = 4,
    kCurveCustom = 5,
    kCurveTypeCount = 6,
};

// On the wire and in flash: [type, p1x, p1y, p2x, p2y, p3x, p3y, p4x, p4y].
// Presets keep the points as sent (the app stores the last custom points
// there), only Custom uses them. A type out of range (0xFF = erased flash)
// reads as Linear.
struct __attribute__((packed)) StickCurve {
    uint8_t type;
    uint8_t pt[8];
};
static_assert(sizeof(StickCurve) == 9, "StickCurve is 9 bytes on the wire");

constexpr int kCurveQ = 4096;      // Q12 full tilt
constexpr int kCurveLutSize = 33;  // presets: x = i * 128

// A curve ready to apply: up to 33 nodes (x, y) in Q12, x non-decreasing,
// starting at (0, 0) and ending at (4096, 4096). n == 0: Linear (skip).
struct StickCurveNodes {
    uint8_t n;
    uint16_t x[kCurveLutSize];
    uint16_t y[kCurveLutSize];
};

namespace stick_curve_detail {
constexpr uint16_t kPreset[4][kCurveLutSize] = {
    // Precise x^1.8
    {0, 8, 28, 58, 97, 145, 201, 266, 338, 418, 505, 599, 701, 809, 925, 1047, 1176,
     1312, 1454, 1603, 1758, 1919, 2087, 2260, 2440, 2627, 2819, 3017, 3221, 3431, 3647, 3868, 4096},
    // Steady x^1.35
    {0, 38, 97, 168, 247, 334, 427, 526, 630, 739, 852, 969, 1090, 1214, 1342, 1473, 1607,
     1744, 1884, 2026, 2172, 2320, 2470, 2623, 2778, 2935, 3095, 3256, 3420, 3586, 3754, 3924, 4096},
    // Aggressive x^0.6
    {0, 512, 776, 990, 1176, 1345, 1500, 1646, 1783, 1913, 2038, 2158, 2274, 2386, 2494, 2600, 2702,
     2802, 2900, 2996, 3089, 3181, 3271, 3360, 3447, 3532, 3616, 3699, 3781, 3861, 3940, 4019, 4096},
    // Digital x^0.3
    {0, 1448, 1783, 2013, 2195, 2347, 2479, 2596, 2702, 2800, 2889, 2973, 3052, 3126, 3196, 3263, 3327,
     3388, 3447, 3503, 3557, 3610, 3661, 3710, 3757, 3804, 3849, 3892, 3935, 3977, 4017, 4057, 4096},
};

inline uint32_t isqrt(uint32_t v) {
    uint32_t r = 0, bit = 1u << 30;
    // loop: HOT-PATH-OK — ≤ 15 shifts to align, per stick per input report with a curve
    while (bit > v) bit >>= 2;
    // loop: HOT-PATH-OK — ≤ 16 iterations integer sqrt, per stick per input report with a curve
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

// A curve point byte (0..255) in Q12, rounded.
inline uint16_t byte_q(uint8_t b) { return (uint16_t) (((uint32_t) b * kCurveQ + 127) / 255); }
} // namespace stick_curve_detail

// Custom points must be non-decreasing in x and y (0..255 each).
inline bool stick_curve_valid(const StickCurve &c) {
    if (c.type >= kCurveTypeCount) return false;
    if (c.type != kCurveCustom) return true;
    // loop: NOT-WORTH — 3 custom points, validating a host set / load
    for (int i = 1; i < 4; i++) {
        if (c.pt[2 * i] < c.pt[2 * i - 2] || c.pt[2 * i + 1] < c.pt[2 * i - 1]) return false;
    }
    return true;
}

inline void stick_curve_build(const StickCurve &c, StickCurveNodes &out) {
    using namespace stick_curve_detail;
    out.n = 0;
    if (!stick_curve_valid(c) || c.type == kCurveLinear) return;
    if (c.type == kCurveCustom) {
        out.x[0] = 0;
        out.y[0] = 0;
        // loop: NOT-WORTH — 4 custom points, on a curve build (host set / load)
        for (int i = 0; i < 4; i++) {
            out.x[1 + i] = byte_q(c.pt[2 * i]);
            out.y[1 + i] = byte_q(c.pt[2 * i + 1]);
        }
        out.x[5] = kCurveQ;
        out.y[5] = kCurveQ;
        out.n = 6;
        return;
    }
    const uint16_t *t = kPreset[c.type - kCurvePrecise];
    // loop: NOT-WORTH — kCurveLutSize = 33 preset nodes, on a curve build
    for (int i = 0; i < kCurveLutSize; i++) {
        out.x[i] = (uint16_t) (i * (kCurveQ / (kCurveLutSize - 1)));
        out.y[i] = t[i];
    }
    out.n = kCurveLutSize;
}

// The curve at tilt m (Q12, 0..4096): the first segment that reaches m,
// linear inside it (zero-width segments are steps and are skipped).
inline uint32_t stick_curve_eval(const StickCurveNodes &c, uint32_t m) {
    // loop: HOT-PATH-OK — ≤ 32 segments linear scan per stick per input report with a curve
    for (int k = 0; k + 1 < c.n; k++) {
        const uint32_t xa = c.x[k], xb = c.x[k + 1];
        if (m > xb || xb == xa) continue;
        const uint32_t ya = c.y[k], yb = c.y[k + 1];
        return ya + (yb - ya) * (m - xa) / (xb - xa);
    }
    return m;
}

// Apply to one stick's two report bytes (x, y; 128 = center) in place.
// Each half-axis counts its own reach (127 up/right, 128 down/left, like
// calibration), the radial tilt goes through the curve and the direction is
// kept. Tilt at or past full (square-gate corners) is left alone.
inline void stick_curve_apply(uint8_t *xy, const StickCurveNodes &c) {
    using namespace stick_curve_detail;
    if (c.n == 0) return;
    const int dx = (int) xy[0] - 128, dy = (int) xy[1] - 128;
    if (dx == 0 && dy == 0) return;
    const uint32_t denx = dx >= 0 ? 127 : 128, deny = dy >= 0 ? 127 : 128;
    const uint32_t ax = (uint32_t) (dx >= 0 ? dx : -dx), ay = (uint32_t) (dy >= 0 ? dy : -dy);
    const uint32_t nx = ax * kCurveQ / denx, ny = ay * kCurveQ / deny;
    const uint32_t mag = isqrt(nx * nx + ny * ny);
    if (mag == 0 || mag >= (uint32_t) kCurveQ) return;
    const uint32_t out = stick_curve_eval(c, mag);
    uint32_t ox = (nx * out / mag * denx + kCurveQ / 2) / kCurveQ;
    uint32_t oy = (ny * out / mag * deny + kCurveQ / 2) / kCurveQ;
    if (ox > denx) ox = denx;
    if (oy > deny) oy = deny;
    xy[0] = (uint8_t) (dx >= 0 ? 128 + ox : 128 - ox);
    xy[1] = (uint8_t) (dy >= 0 ? 128 + oy : 128 - oy);
}

#endif // DS5_BRIDGE_STICK_CURVE_H
