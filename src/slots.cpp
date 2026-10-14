// Flash-backed slot table for 4-slot persistent BT pairing.
// Modeled on zurce/DS5Dongle-OLED (bt.cpp:29-115). Credit to zurce.

#include "slots.h"

#include <cstring>
#include <cstdio>

#include "hardware/flash.h"
#include "flash_safe.h"
#include "flash_layout.h"
#include "hardware/sync.h"

constexpr uint32_t SLOTS_MAGIC = 0x44533502u;  // "DS5\x02"

struct __attribute__((packed)) SlotsData {
    uint32_t magic;
    uint8_t  addrs[kNumSlots][6];
    uint8_t  occupied[kNumSlots];
};

static_assert(sizeof(SlotsData) <= FLASH_PAGE_SIZE);
static_assert(SLOTS_FLASH_OFFSET % FLASH_SECTOR_SIZE == 0);

static SlotsData g_slots{};

static const SlotsData *flash_slots() {
    return reinterpret_cast<const SlotsData *>(XIP_BASE + SLOTS_FLASH_OFFSET);
}

static bool save_slots_to_flash() {
    alignas(4) uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xff, sizeof(page));
    memcpy(page, &g_slots, sizeof(g_slots));

    // error: SHOWN — printf + returned to the slot changes, which report it (0xF1 CMD_RESULT_FAILED, the OLED's "Save FAIL"/"Wipe FAIL")
    if (!flash_sector_write(SLOTS_FLASH_OFFSET, page)) {
        printf("[Flash] core1 lockout timed out, write skipped\n");
        // error: SHOWN — printf + returned to the slot changes, which report it (0xF1 CMD_RESULT_FAILED, the OLED's "Save FAIL"/"Wipe FAIL")
        return false;
    }

    SlotsData verify{};
    memcpy(&verify, flash_slots(), sizeof(verify));
    if (memcmp(&verify, &g_slots, sizeof(g_slots)) == 0) {
        printf("[Slots] flash write verified\n");
        return true;
    }
    printf("[Slots] flash write VERIFY FAILED\n");
    // error: SHOWN — printf + returned to the slot changes, which report it (0xF1 CMD_RESULT_FAILED, the OLED's "Save FAIL"/"Wipe FAIL")
    return false;
}

void slots_load() {
    memcpy(&g_slots, flash_slots(), sizeof(g_slots));
    // error: LOGGED-OK — printf; first boot / erased sector: no paired controllers, which the app and OLED show as empty slots
    if (g_slots.magic != SLOTS_MAGIC) {
        printf("[Slots] flash sector empty/invalid, initializing\n");
        memset(&g_slots, 0, sizeof(g_slots));
        g_slots.magic = SLOTS_MAGIC;
        // error: LOGGED-OK — printf; boot-time init save of a blank sector: the RAM copy works now and the next boot retries the save
        save_slots_to_flash();
    }
    // loop: NOT-WORTH — boot-only print of 4 slots
    for (int i = 0; i < kNumSlots; i++) {
        if (g_slots.occupied[i]) {
            printf("[Slots] %d: %02X:%02X:%02X:%02X:%02X:%02X\n", i,
                   g_slots.addrs[i][0], g_slots.addrs[i][1], g_slots.addrs[i][2],
                   g_slots.addrs[i][3], g_slots.addrs[i][4], g_slots.addrs[i][5]);
        } else {
            printf("[Slots] %d: (empty)\n", i);
        }
    }
}

bool slot_occupied(int slot) {
    if (slot < 0 || slot >= kNumSlots) return false;
    return g_slots.occupied[slot] != 0;
}

void slot_get_addr(int slot, uint8_t out[6]) {
    if (slot < 0 || slot >= kNumSlots) {
        memset(out, 0, 6);
        return;
    }
    memcpy(out, g_slots.addrs[slot], 6);
}

int slot_owner_of(const uint8_t addr[6]) {
    // loop: NOT-WORTH — 4 slots, per BT inquiry result while pairing
    for (int i = 0; i < kNumSlots; i++) {
        if (g_slots.occupied[i] && memcmp(g_slots.addrs[i], addr, 6) == 0) return i;
    }
    // error: NOT-WORTH — nothing; -1 = no slot owns this address (a lookup), not an error
    return -1;
}

int slot_for_controller(int current, const uint8_t addr[6]) {
    if (slot_occupied(current) && memcmp(g_slots.addrs[current], addr, 6) == 0) return current;
    const int owner = slot_owner_of(addr);
    return owner >= 0 ? owner : current;
}

bool slot_assign(int slot, const uint8_t addr[6]) {
    // error: NOT-WORTH — false for a slot outside 0..3; every caller passes a checked slot
    if (slot < 0 || slot >= kNumSlots) return false;
    memcpy(g_slots.addrs[slot], addr, 6);
    g_slots.occupied[slot] = 1;
    // error: SHOWN — returned: the callers report a failed save (fixed in this pass: was dropped)
    return save_slots_to_flash();
}

bool slot_forget(int slot) {
    // error: NOT-WORTH — false for a slot outside 0..3; every caller passes a checked slot
    if (slot < 0 || slot >= kNumSlots) return false;
    memset(g_slots.addrs[slot], 0, 6);
    g_slots.occupied[slot] = 0;
    // error: SHOWN — returned: the callers report a failed save (fixed in this pass: was dropped)
    return save_slots_to_flash();
}

bool slots_wipe_all() {
    // loop: NOT-WORTH — 4 slots, on a user wipe (then one flash save)
    for (int i = 0; i < kNumSlots; i++) {
        memset(g_slots.addrs[i], 0, 6);
        g_slots.occupied[i] = 0;
    }
    // error: SHOWN — returned: the callers report a failed save (fixed in this pass: was dropped)
    return save_slots_to_flash();
}

bool slots_any_empty() {
    // loop: NOT-WORTH — 4 slots, on pairing / connection events
    for (int i = 0; i < kNumSlots; i++) {
        if (!g_slots.occupied[i]) return true;
    }
    return false;
}
