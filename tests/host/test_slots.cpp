// src/slots.cpp: every change to the slot table reports whether it reached
// flash (error pass: a failed save used to be dropped, so a forget or wipe
// looked done in the app and on the OLED but came back at power-off).
#include <cstring>
#include "fakes.h"
#include "slots.h"
#include "slots_footer.h"
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

// One controller, one slot: a controller already paired takes the dongle to
// its own slot instead of being paired into the current empty one (how one
// controller ended up in slots 0 and 3), and forgetting one copy of an older
// duplicate leaves the other slot holding it (bt_forget_slot keeps the
// shared link key while slot_owner_of still finds it).
TEST(slots_one_controller_one_slot) {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    slots_load();
    const uint8_t a[6] = {1, 2, 3, 4, 5, 6};
    const uint8_t b[6] = {9, 8, 7, 6, 5, 4};
    CHECK(slot_assign(0, a));
    CHECK(slot_for_controller(3, a) == 0);  // slot 3 current + empty: go to slot 0
    CHECK(slot_for_controller(0, a) == 0);
    CHECK(slot_for_controller(3, b) == 3);  // new controller: the current slot
    CHECK(slot_for_controller(0, b) == 0);  // unowned, current taken: stays (as before)
    // An older firmware's duplicate: the current copy wins, the other is found after a forget.
    CHECK(slot_assign(3, a));
    CHECK(slot_for_controller(3, a) == 3);
    CHECK(slot_forget(3));
    CHECK(slot_owner_of(a) == 0);
    CHECK(slot_for_controller(3, a) == 0);
    CHECK(slot_forget(0));
    CHECK(slot_owner_of(a) == -1);
}

// The Slots screen footer: with every slot taken and no controller connected
// it says why a new controller can't pair; a free slot, a connected
// controller or a live status message keep the old lines.
TEST(slots_footer_says_when_all_slots_are_full) {
    using namespace slots_footer;
    CHECK(std::strcmp(pick(nullptr, false, true), kFull) == 0);
    CHECK(std::strcmp(pick(nullptr, false, false), kConnect) == 0);
    CHECK(std::strcmp(pick(nullptr, true, true), kHelp) == 0);
    CHECK(std::strcmp(pick("Wiped!", false, true), "Wiped!") == 0);
    CHECK(std::strlen(kFull) <= static_cast<size_t>(kMaxChars));
    // Real occupancy: full only once all kNumSlots slots hold a controller.
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    slots_load();
    // loop: NOT-WORTH — 4 slots, host test
    for (int i = 0; i < kNumSlots; i++) {
        CHECK(slots_any_empty());
        const uint8_t addr[6] = {1, 2, 3, 4, 5, static_cast<uint8_t>(i)};
        CHECK(slot_assign(i, addr));
    }
    CHECK(!slots_any_empty());
    CHECK(slot_forget(2));
    CHECK(slots_any_empty());
}
