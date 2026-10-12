// src/calib.cpp: validation, persistence (flash is a RAM array here), the
// per-slot stick / trigger / gyro correction and the stick-curve storage.
#include <cstring>
#include "calib.h"
#include "fakes.h"
#include "flash_layout.h"
#include "hardware/flash.h"
#include "test.h"

namespace {
CalibBody identity() {
    CalibBody b{};
    for (auto &s : b.stick) s = {0, 0, 0, 128};
    for (auto &t : b.trigger) t = {0, 255};
    return b;
}
CalibSet set(uint8_t slot, const CalibBody &b) { return calib_set(slot, reinterpret_cast<const uint8_t *>(&b)); }
void fresh() {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    g_fake_slot = 0;
    g_fake_bt_connected = false;
    curves_revert();
    calib_load();
}
}  // namespace

TEST(calib_erased_flash_is_identity) {
    fresh();
    uint8_t r[63];
    for (int i = 0; i < 63; i++) r[i] = (uint8_t) (i * 7);
    uint8_t before[63];
    std::memcpy(before, r, sizeof(r));
    calib_apply(r);
    CHECK(std::memcmp(r, before, sizeof(r)) == 0);
    uint8_t out[kCalibBodySize];
    calib_get(0, out);
    const CalibBody id = identity();
    CHECK(std::memcmp(out, &id, sizeof(id)) == 0);
}

TEST(calib_rejects_out_of_range_bodies) {
    fresh();
    CalibBody b = identity();
    b.stick[0].cx = 41;
    CHECK(set(0, b) == CalibSet::Invalid);
    b = identity();
    b.stick[1].deadzone = 65;
    CHECK(set(0, b) == CalibSet::Invalid);
    b = identity();
    b.stick[0].outer = 63;
    CHECK(set(0, b) == CalibSet::Invalid);
    b = identity();
    b.trigger[0] = {100, 160};  // span < 64
    CHECK(set(0, b) == CalibSet::Invalid);
    CHECK(set(4, identity()) == CalibSet::Invalid);  // slot out of range
}

TEST(calib_triggers_and_sticks_apply_and_persist) {
    fresh();
    CalibBody b = identity();
    b.trigger[1] = {20, 220};
    b.stick[0].deadzone = 10;
    CHECK(set(0, b) == CalibSet::Ok);
    uint8_t r[63]{};
    r[0] = r[1] = r[2] = r[3] = 128;
    r[0] = 132;  // inside the deadzone -> centered
    r[5] = 20;
    calib_apply(r);
    CHECK_EQ(r[0], 128);
    CHECK_EQ(r[5], 0);
    r[5] = 220;
    calib_apply(r);
    CHECK_EQ(r[5], 255);
    r[0] = 255;  // full tilt stays full tilt
    calib_apply(r);
    CHECK_EQ(r[0], 255);

    // Reload from "flash": same result.
    calib_load();
    uint8_t out[kCalibBodySize];
    calib_get(0, out);
    CHECK(std::memcmp(out, &b, sizeof(b)) == 0);
    // Other slots untouched.
    calib_get(1, out);
    const CalibBody id = identity();
    CHECK(std::memcmp(out, &id, sizeof(id)) == 0);
}

TEST(calib_failed_flash_write_is_not_saved) {
    fresh();
    g_fake_flash_fail = true;
    CHECK(set(0, identity()) == CalibSet::NotSaved);
    g_fake_flash_fail = false;
}

TEST(calib_curves_saved_temporary_and_revert) {
    fresh();
    StickCurve pair[2] = {{kCurveDigital, {}}, {kCurvePrecise, {}}};
    CHECK(curves_set(1, pair, false) == CalibSet::Ok);
    StickCurve tmp[2] = {{kCurveAggressive, {}}, {kCurveSteady, {}}};
    CHECK(curves_set(1, tmp, true) == CalibSet::Ok);
    CHECK(curves_temporary());
    StickCurve got[2];
    CHECK(curves_get_active(1, got));
    CHECK_EQ(got[0].type, kCurveAggressive);
    curves_get(1, got);  // saved pair unchanged
    CHECK_EQ(got[0].type, kCurveDigital);
    curves_revert();
    CHECK(!curves_temporary());
    calib_load();  // survives a reload
    curves_get(1, got);
    CHECK_EQ(got[0].type, kCurveDigital);
    CHECK_EQ(got[1].type, kCurvePrecise);
    StickCurve bad[2] = {{0x77, {}}, {kCurveLinear, {}}};
    CHECK(curves_set(1, bad, false) == CalibSet::Invalid);
}

TEST(calib_old_save_without_curves_reads_linear) {
    fresh();
    CalibBody b = identity();
    b.trigger[0] = {10, 200};
    CHECK(set(2, b) == CalibSet::Ok);
    // An older firmware's save: the page past the four bodies is erased.
    const uint32_t curves_at = CALIB_FLASH_OFFSET + 5 + 4 * kCalibBodySize;
    std::memset(g_fake_flash + curves_at, 0xFF, 4 * 2 * sizeof(StickCurve));
    calib_load();
    StickCurve got[2];
    curves_get(2, got);
    CHECK_EQ(got[0].type, kCurveLinear);
    uint8_t out[kCalibBodySize];
    calib_get(2, out);
    CHECK(std::memcmp(out, &b, sizeof(b)) == 0);  // calibration kept
}
