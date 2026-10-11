#include "calib.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "flash_layout.h"
#include "flash_safe.h"
#include "hardware/flash.h"

namespace {

constexpr uint32_t CALIB_MAGIC = 0x44533504u; // "DS5\x04"

struct __attribute__((packed)) CalibData {
    uint32_t  magic;
    uint8_t   ver;
    CalibBody body;
};
static_assert(sizeof(CalibData) <= FLASH_PAGE_SIZE);
static_assert(CALIB_FLASH_OFFSET % FLASH_SECTOR_SIZE == 0);

CalibData g_cal{};
bool g_active = false;

const CalibData *flash_cal() { return reinterpret_cast<const CalibData *>(XIP_BASE + CALIB_FLASH_OFFSET); }

void set_identity(CalibBody &b) {
    for (auto &s : b.stick) s = {0, 0, 0, 128};
    for (auto &t : b.trigger) t = {0, 255};
    for (int i = 0; i < 3; i++) b.gyro[i] = 0;
}

bool valid(const CalibBody &b) {
    for (const auto &s : b.stick) {
        if (s.deadzone > 64 || s.outer < 64 || s.outer > 128 || s.deadzone >= s.outer) return false;
        if (s.cx < -40 || s.cx > 40 || s.cy < -40 || s.cy > 40) return false;
    }
    for (const auto &t : b.trigger) {
        if (t.lo >= t.hi || t.hi - t.lo < 64) return false;
    }
    return true;
}

void recompute_active() {
    CalibBody id{};
    set_identity(id);
    g_active = memcmp(&id, &g_cal.body, sizeof(id)) != 0;
}

bool save() {
    alignas(4) uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xff, sizeof(page));
    memcpy(page, &g_cal, sizeof(g_cal));
    if (!flash_sector_write(CALIB_FLASH_OFFSET, page)) return false;
    return memcmp(flash_cal(), &g_cal, sizeof(g_cal)) == 0;
}

uint8_t clamp_u8(float v) { return (uint8_t) (v < 0 ? 0 : v > 255 ? 255 : lroundf(v)); }

void stick(uint8_t *xy, const CalibStick &c) {
    const float x = (float) xy[0] - 128.0f - c.cx;
    const float y = (float) xy[1] - 128.0f - c.cy;
    const float mag = sqrtf(x * x + y * y);
    if (mag <= c.deadzone) {
        xy[0] = xy[1] = 128;
        return;
    }
    // Radial: deadzone..outer maps to 0..127, direction kept.
    float scaled = (mag - c.deadzone) / (float) (c.outer - c.deadzone) * 127.0f;
    if (scaled > 127.0f) scaled = 127.0f;
    xy[0] = clamp_u8(128.0f + x / mag * scaled);
    xy[1] = clamp_u8(128.0f + y / mag * scaled);
}

} // namespace

void calib_load() {
    memcpy(&g_cal, flash_cal(), sizeof(g_cal));
    if (g_cal.magic != CALIB_MAGIC || g_cal.ver != kCalibProtoVer || !valid(g_cal.body)) {
        g_cal.magic = CALIB_MAGIC;
        g_cal.ver = kCalibProtoVer;
        set_identity(g_cal.body);
    }
    recompute_active();
    printf("[Calib] loaded (active=%d)\n", g_active);
}

void calib_apply(uint8_t *r) {
    if (!g_active) return;
    stick(r + 0, g_cal.body.stick[0]);
    stick(r + 2, g_cal.body.stick[1]);
    for (int i = 0; i < 2; i++) {
        const auto &t = g_cal.body.trigger[i];
        r[4 + i] = clamp_u8(((float) r[4 + i] - t.lo) * 255.0f / (float) (t.hi - t.lo));
    }
    for (int i = 0; i < 3; i++) {
        int16_t v;
        memcpy(&v, r + 15 + 2 * i, 2);
        int32_t c = (int32_t) v - g_cal.body.gyro[i];
        if (c > 32767) c = 32767;
        if (c < -32768) c = -32768;
        v = (int16_t) c;
        memcpy(r + 15 + 2 * i, &v, 2);
    }
}

CalibSet calib_set(const uint8_t *body) {
    CalibBody b;
    memcpy(&b, body, sizeof(b));
    if (!valid(b)) return CalibSet::Invalid;
    g_cal.body = b;
    recompute_active();
    return save() ? CalibSet::Ok : CalibSet::NotSaved;
}

void calib_get(uint8_t out[kCalibBodySize]) { memcpy(out, &g_cal.body, kCalibBodySize); }
