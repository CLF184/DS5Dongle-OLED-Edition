#include "calib.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "bt.h"
#include "flash_layout.h"
#include "flash_safe.h"
#include "hardware/flash.h"
#include "slots.h"

namespace {

constexpr uint32_t CALIB_MAGIC = 0x44533504u; // "DS5\x04"

struct __attribute__((packed)) CalibData {
    uint32_t  magic;
    uint8_t   ver;
    CalibBody body[kNumSlots];
};
static_assert(sizeof(CalibData) <= FLASH_PAGE_SIZE);
static_assert(CALIB_FLASH_OFFSET % FLASH_SECTOR_SIZE == 0);

CalibData g_cal{};

// Which parts of each slot's calibration differ from identity (skipped otherwise).
struct Active { bool stick[2], trigger[2], gyro; };
Active g_active[kNumSlots]{};

// The connected controller's factory gyro bias (feature report 0x05, words at
// 1, 3, 5: pitch, yaw, roll; same layout and sanity limit as SDL and oled.cpp).
int16_t g_bias[3]{};
bool g_bias_known = false; // 0x05 seen for this connection (bias 0 if implausible)

const CalibData *flash_cal() { return reinterpret_cast<const CalibData *>(XIP_BASE + CALIB_FLASH_OFFSET); }

constexpr CalibStick kStickId{0, 0, 0, 128};

void set_identity(CalibBody &b) {
    for (auto &s : b.stick) s = kStickId;
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

void recompute_active(int slot) {
    const CalibBody &b = g_cal.body[slot];
    Active &a = g_active[slot];
    for (int i = 0; i < 2; i++) {
        a.stick[i] = memcmp(&b.stick[i], &kStickId, sizeof(CalibStick)) != 0;
        a.trigger[i] = b.trigger[i].lo != 0 || b.trigger[i].hi != 255;
    }
    a.gyro = b.gyro[0] != 0 || b.gyro[1] != 0 || b.gyro[2] != 0;
}

bool save() {
    alignas(4) uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xff, sizeof(page));
    memcpy(page, &g_cal, sizeof(g_cal));
    if (!flash_sector_write(CALIB_FLASH_OFFSET, page)) return false;
    return memcmp(flash_cal(), &g_cal, sizeof(g_cal)) == 0;
}

uint8_t clamp_u8(float v) { return (uint8_t) (v < 0 ? 0 : v > 255 ? 255 : lroundf(v)); }

// Center offset, then each half-axis normalized to its own reach (the side the
// center leans toward has less travel), a radial deadzone, and full tilt mapped
// to 0 and 255. The app's correctStick (src/protocol/calibration.ts) matches.
void stick(uint8_t *xy, const CalibStick &c) {
    const float x = (float) xy[0] - 128.0f - c.cx;
    const float y = (float) xy[1] - 128.0f - c.cy;
    const float outer = (float) c.outer;
    const float nx = x / fminf(outer, x >= 0 ? 127.0f - c.cx : 128.0f + c.cx);
    const float ny = y / fminf(outer, y >= 0 ? 127.0f - c.cy : 128.0f + c.cy);
    const float mag = sqrtf(nx * nx + ny * ny);
    const float dz = (float) c.deadzone / outer;
    if (mag <= dz) {
        xy[0] = xy[1] = 128;
        return;
    }
    const float s = fminf(1.0f, (mag - dz) / (1.0f - dz)) / mag;
    const float ox = nx * s, oy = ny * s;
    xy[0] = clamp_u8(128.0f + ox * (ox >= 0 ? 127.0f : 128.0f));
    xy[1] = clamp_u8(128.0f + oy * (oy >= 0 ? 127.0f : 128.0f));
}

int slot_now() {
    const int s = bt_get_slot();
    return s >= 0 && s < kNumSlots ? s : 0;
}

} // namespace

void calib_load() {
    memcpy(&g_cal, flash_cal(), sizeof(g_cal));
    bool ok = g_cal.magic == CALIB_MAGIC && g_cal.ver == kCalibProtoVer;
    for (int i = 0; ok && i < kNumSlots; i++) ok = valid(g_cal.body[i]);
    if (!ok) {
        g_cal.magic = CALIB_MAGIC;
        g_cal.ver = kCalibProtoVer;
        for (auto &b : g_cal.body) set_identity(b);
    }
    for (int i = 0; i < kNumSlots; i++) recompute_active(i);
    printf("[Calib] loaded (%s)\n", ok ? "saved" : "identity");
}

void calib_service() {
    if (!bt_is_connected()) {
        g_bias_known = false;
        return;
    }
    if (g_bias_known) return;
    const auto d = bt_peek_feature(0x05);
    if (d.size() < 35) return; // not arrived yet
    bool plausible = true;
    int16_t b[3];
    for (int i = 0; i < 3; i++) {
        b[i] = (int16_t) ((uint16_t) d[1 + 2 * i] | ((uint16_t) d[2 + 2 * i] << 8));
        if (b[i] > 1024 || b[i] < -1024) plausible = false;
    }
    // Hosts fall back to raw when the factory cal is implausible (SDL), so
    // then the whole rest reading is ours to remove.
    for (int i = 0; i < 3; i++) g_bias[i] = plausible ? b[i] : 0;
    g_bias_known = true;
}

void calib_apply(uint8_t *r) {
    const int slot = slot_now();
    const Active &a = g_active[slot];
    const CalibBody &b = g_cal.body[slot];
    if (a.stick[0]) stick(r + 0, b.stick[0]);
    if (a.stick[1]) stick(r + 2, b.stick[1]);
    for (int i = 0; i < 2; i++) {
        if (!a.trigger[i]) continue;
        const auto &t = b.trigger[i];
        r[4 + i] = clamp_u8(((float) r[4 + i] - t.lo) * 255.0f / (float) (t.hi - t.lo));
    }
    if (a.gyro && g_bias_known) {
        for (int i = 0; i < 3; i++) {
            int16_t v;
            memcpy(&v, r + 15 + 2 * i, 2);
            int32_t c = (int32_t) v - ((int32_t) b.gyro[i] - g_bias[i]);
            if (c > 32767) c = 32767;
            if (c < -32768) c = -32768;
            v = (int16_t) c;
            memcpy(r + 15 + 2 * i, &v, 2);
        }
    }
}

CalibSet calib_set(uint8_t slot, const uint8_t *body) {
    if (slot >= kNumSlots) return CalibSet::Invalid;
    CalibBody b;
    memcpy(&b, body, sizeof(b));
    if (!valid(b)) return CalibSet::Invalid;
    g_cal.body[slot] = b;
    recompute_active(slot);
    return save() ? CalibSet::Ok : CalibSet::NotSaved;
}

void calib_get(uint8_t slot, uint8_t out[kCalibBodySize]) {
    memcpy(out, &g_cal.body[slot < kNumSlots ? slot : 0], kCalibBodySize);
}
