// src/stick_curve.h against the sample points and full-range hashes the app's
// tests hold (DS5Dongle-OLED-Config-Web tests/unit/stickCurve.test.ts), so the
// firmware and the app's copy of the math can't drift apart unnoticed.
#include "stick_curve.h"
#include "test.h"

namespace {
StickCurve preset(uint8_t type) { return StickCurve{type, {51, 31, 102, 77, 153, 133, 204, 194}}; }
StickCurve custom_step() { return StickCurve{kCurveCustom, {0, 40, 64, 64, 128, 200, 255, 255}}; }

void apply(const StickCurve &c, uint8_t x, uint8_t y, uint8_t &ox, uint8_t &oy) {
    StickCurveNodes n{};
    stick_curve_build(c, n);
    uint8_t xy[2] = {x, y};
    if (n.n) stick_curve_apply(xy, n);
    ox = xy[0];
    oy = xy[1];
}

uint32_t full_hash(const StickCurve &c) {
    uint32_t h = 2166136261u;
    for (int x = 0; x < 256; x++)
        for (int y = 0; y < 256; y++) {
            uint8_t ox, oy;
            apply(c, (uint8_t) x, (uint8_t) y, ox, oy);
            h = (h ^ ox) * 16777619u;
            h = (h ^ oy) * 16777619u;
        }
    return h;
}

struct Sample { uint8_t x, y, ox, oy; };
void check_samples(const StickCurve &c, std::initializer_list<Sample> samples) {
    for (const auto &s : samples) {
        uint8_t ox, oy;
        apply(c, s.x, s.y, ox, oy);
        CHECK_EQ(ox, s.ox);
        CHECK_EQ(oy, s.oy);
    }
}
}  // namespace

TEST(stick_curve_matches_app_samples) {
    check_samples(preset(kCurvePrecise), {{128, 128, 128, 128}, {140, 128, 130, 128}, {160, 128, 139, 128},
                                          {200, 128, 174, 128}, {255, 128, 255, 128}, {0, 128, 0, 128},
                                          {128, 0, 128, 0}, {170, 90, 150, 108}, {60, 200, 72, 187},
                                          {220, 220, 220, 220}, {129, 127, 128, 128}});
    check_samples(preset(kCurveSteady), {{140, 128, 133, 128}, {160, 128, 148, 128}, {200, 128, 187, 128},
                                         {170, 90, 160, 99}, {60, 200, 66, 194}, {129, 127, 128, 128}});
    check_samples(preset(kCurveAggressive), {{140, 128, 159, 128}, {160, 128, 184, 128}, {200, 128, 218, 128},
                                             {170, 90, 186, 75}, {60, 200, 53, 208}, {129, 127, 132, 124}});
    check_samples(preset(kCurveDigital), {{140, 128, 191, 128}, {160, 128, 212, 128}, {200, 128, 235, 128},
                                          {170, 90, 202, 61}, {60, 200, 47, 214}, {129, 127, 139, 117}});
    check_samples(preset(kCurveCustom), {{140, 128, 135, 128}, {160, 128, 149, 128}, {200, 128, 190, 128},
                                         {170, 90, 161, 98}, {60, 200, 64, 196}, {129, 127, 129, 127}});
    check_samples(custom_step(), {{140, 128, 152, 128}, {160, 128, 160, 128}, {200, 128, 231, 128},
                                  {170, 90, 191, 71}, {60, 200, 49, 212}, {129, 127, 143, 113}});
}

TEST(stick_curve_matches_app_full_range) {
    CHECK_EQ(full_hash(preset(kCurvePrecise)), 4148090507u);
    CHECK_EQ(full_hash(preset(kCurveSteady)), 2271996083u);
    CHECK_EQ(full_hash(preset(kCurveAggressive)), 907744103u);
    CHECK_EQ(full_hash(preset(kCurveDigital)), 4165919479u);
    CHECK_EQ(full_hash(preset(kCurveCustom)), 2090334541u);
    CHECK_EQ(full_hash(custom_step()), 4148320443u);
}

TEST(stick_curve_linear_and_invalid_are_identity) {
    StickCurve erased{};
    erased.type = 0xFF;
    for (auto &p : erased.pt) p = 0xFF;
    CHECK(!stick_curve_valid(erased));
    StickCurve bad = custom_step();
    bad.pt[2] = 0;  // x goes backwards
    bad.pt[0] = 10;
    CHECK(!stick_curve_valid(bad));
    for (const StickCurve &c : {preset(kCurveLinear), erased, bad}) {
        StickCurveNodes n{};
        stick_curve_build(c, n);
        CHECK_EQ(n.n, 0);
    }
}

TEST(stick_curve_every_input_in_range_and_monotonic_on_axis) {
    for (uint8_t t = kCurvePrecise; t <= kCurveCustom; t++) {
        int prev = -1;
        for (int x = 128; x < 256; x++) {
            uint8_t ox, oy;
            apply(preset(t), (uint8_t) x, 128, ox, oy);
            CHECK_EQ(oy, 128);
            CHECK(ox >= prev);
            prev = ox;
        }
    }
}
