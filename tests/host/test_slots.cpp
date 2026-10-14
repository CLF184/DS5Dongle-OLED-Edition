// src/slots.cpp: every change to the slot table reports whether it reached
// flash (error pass: a failed save used to be dropped, so a forget or wipe
// looked done in the app and on the OLED but came back at power-off).
#include <cstring>
#include "fakes.h"
#include "slots.h"
#include "test.h"

TEST(slots_changes_report_a_failed_save) {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    slots_load();
    const uint8_t addr[6] = {1, 2, 3, 4, 5, 6};
    CHECK(slot_assign(1, addr));
    CHECK(slot_occupied(1));
    CHECK(slot_forget(1));
    CHECK(slots_wipe_all());
    CHECK(!slot_assign(kNumSlots, addr));  // no such slot
    CHECK(!slot_forget(-1));
    g_fake_flash_fail = true;
    CHECK(!slot_assign(2, addr));
    CHECK(slot_occupied(2));  // changed in RAM all the same
    CHECK(!slot_forget(2));
    CHECK(!slots_wipe_all());
    g_fake_flash_fail = false;
    slots_load();  // flash still has the last saved table: slot 2 was never saved
    CHECK(!slot_occupied(2));
}
