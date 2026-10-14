// src/remap.cpp: table validation, apply on a report copy, the Edge extras,
// temporary tables and what ends them, persistence.
#include <cstring>
#include "fakes.h"
#include "remap.h"
#include "test.h"

namespace {
// Index order of RemapButton (remap.cpp).
enum { L2, L1, Create, Up, Left, Down, Right, L3, R2, R1, Options, Tri, Circle, Cross, Square, R3 };
void identity(uint8_t t[16]) { for (int i = 0; i < 16; i++) t[i] = (uint8_t) i; }
void fresh() {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    remap_load();
}
}  // namespace

TEST(remap_identity_leaves_report_alone) {
    fresh();
    uint8_t r[63];
    for (int i = 0; i < 63; i++) r[i] = (uint8_t) (i * 13);
    uint8_t before[63];
    std::memcpy(before, r, 63);
    remap_apply(r);
    CHECK(std::memcmp(r, before, 63) == 0);
}

TEST(remap_swap_cross_and_circle) {
    fresh();
    uint8_t t[16];
    identity(t);
    t[Cross] = Circle;
    t[Circle] = Cross;
    CHECK(remap_set(t) == RemapSet::Ok);
    uint8_t r[63]{};
    r[7] = 0x08 | 0x20;  // D-pad neutral + Cross
    remap_apply(r);
    CHECK_EQ(r[7], 0x08 | 0x40);  // Circle
    remap_load();  // persisted
    uint8_t got[16];
    remap_get(got);
    CHECK(std::memcmp(got, t, 16) == 0);
}

TEST(remap_rejects_bad_entries) {
    fresh();
    uint8_t t[16];
    identity(t);
    t[3] = 16;
    CHECK(remap_set(t) == RemapSet::Invalid);
    CHECK(!remap_set_temporary(t));
}

TEST(remap_disabled_and_analog_triggers) {
    fresh();
    uint8_t t[16];
    identity(t);
    t[L2] = R2;    // L2 acts as R2
    t[R2] = 0xFF;  // R2 does nothing
    CHECK(remap_set(t) == RemapSet::Ok);
    uint8_t r[63]{};
    r[7] = 0x08;
    r[4] = 200;  // L2 analog
    r[5] = 90;   // R2 analog
    r[8] = 0x04 | 0x08;
    remap_apply(r);
    CHECK_EQ(r[4], 0);
    CHECK_EQ(r[5], 200);
    CHECK_EQ(r[8], 0x08);
}

TEST(remap_edge_extras) {
    fresh();
    const uint8_t extra[4] = {Tri, kRemapExtraOff, kRemapExtraSelf, Up};
    CHECK(remap_set_extra(extra, false) == RemapSet::Ok);
    uint8_t r[63]{};
    r[7] = 0x08;
    r[9] = 0x10 | 0x20 | 0x40 | 0x80;
    remap_apply(r);
    CHECK_EQ(r[9], 0x40);              // only "itself" stays
    CHECK_EQ(r[7], 0x80 | 0x00);       // Triangle + D-pad up (hat 0)
    const uint8_t bad[4] = {16, 0, 0, 0};
    CHECK(remap_set_extra(bad, false) == RemapSet::Invalid);
}

TEST(remap_temporary_and_revert) {
    fresh();
    uint8_t saved[16];
    identity(saved);
    saved[Square] = Tri;
    CHECK(remap_set(saved) == RemapSet::Ok);
    uint8_t game[16];
    identity(game);
    game[Cross] = Square;
    CHECK(remap_set_temporary(game));
    CHECK(remap_temporary());
    // A saved extras write while a game's table is on keeps the saved table in flash.
    const uint8_t extra[4] = {kRemapExtraOff, kRemapExtraSelf, kRemapExtraSelf, kRemapExtraSelf};
    CHECK(remap_set_extra(extra, false) == RemapSet::Ok);
    remap_revert();
    CHECK(!remap_temporary());
    uint8_t got[16];
    remap_get(got);
    CHECK(std::memcmp(got, saved, 16) == 0);
    uint8_t ex[4];
    remap_get_extra(ex);
    CHECK_EQ(ex[0], kRemapExtraOff);
}

// Error pass: a failed flash write is NotSaved (the app says "failed"), not
// Invalid (the app says "rejected"); the entries still apply for now.
TEST(remap_edge_extras_not_saved) {
    fresh();
    const uint8_t extra[4] = {Tri, kRemapExtraSelf, kRemapExtraSelf, kRemapExtraSelf};
    g_fake_flash_fail = true;
    CHECK(remap_set_extra(extra, false) == RemapSet::NotSaved);
    CHECK(remap_set_extra(extra, true) == RemapSet::Ok);  // temporary: nothing to save
    g_fake_flash_fail = false;
    uint8_t ex[4];
    remap_get_extra(ex);
    CHECK_EQ(ex[0], Tri);
}
