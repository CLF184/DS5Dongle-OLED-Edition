// Flash-backed slot table for 4-slot persistent BT pairing.
// Modeled on zurce/DS5Dongle-OLED (bt.cpp:29-115). Credit to zurce.

#include "slots.h"

#include <cstring>
#include <cstdio>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include "pico/btstack_flash_bank.h"

constexpr uint32_t SLOTS_MAGIC = 0x44533502u;  // "DS5\x02"
// 扇区布局：SDK 的 BTstack TLV（= 经典蓝牙 link key 库）占用
// PICO_FLASH_BANK_STORAGE_OFFSET 起的 2 个扇区，双 bank 交替写
// （bank0 = SIZE-12K、bank1 = SIZE-8K）。自建存储必须从 bank 往下堆：
// config 在 bank 下方第一个扇区，slots 再往下让一个。
// 旧值曾是 PICO_FLASH_SIZE_BYTES - 2*FLASH_SECTOR_SIZE（正好 = bank1）：写槽位会
// 擦掉手柄的 link key 库（需重新配对），TLV 轮换也会反过来擦掉槽位。
// 改址后旧数据自然失效（magic 校验不过 → 槽位恢复默认，手柄配对不受影响）。
constexpr uint32_t SLOTS_FLASH_OFFSET = PICO_FLASH_BANK_STORAGE_OFFSET - 2u * FLASH_SECTOR_SIZE;

struct __attribute__((packed)) SlotsData {
    uint32_t magic;
    uint8_t  addrs[kNumSlots][6];
    uint8_t  occupied[kNumSlots];
};

static_assert(sizeof(SlotsData) <= FLASH_PAGE_SIZE);
static_assert(SLOTS_FLASH_OFFSET % FLASH_SECTOR_SIZE == 0);
// 编译期防呆：slots 必须整个落在 BTstack bank 下方（bank 从 STORAGE_OFFSET 起）。
static_assert(SLOTS_FLASH_OFFSET + FLASH_SECTOR_SIZE <= PICO_FLASH_BANK_STORAGE_OFFSET);

static SlotsData g_slots{};

static const SlotsData *flash_slots() {
    return reinterpret_cast<const SlotsData *>(XIP_BASE + SLOTS_FLASH_OFFSET);
}

// Runs with core1 parked (flash_safe_execute) and core0 interrupts disabled, so
// neither core touches XIP flash while the sector is erased/programmed. Without
// the core1 park this races the audio core and corrupts audio (buzzing). Same
// pattern as config.cpp:config_save_flash_op.
static void slots_save_flash_op(void *param) {
    const uint8_t *page = static_cast<const uint8_t *>(param);
    const uint32_t interrupts = save_and_disable_interrupts();
    flash_range_erase(SLOTS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(SLOTS_FLASH_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(interrupts);
}

static bool save_slots_to_flash() {
    alignas(4) uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xff, sizeof(page));
    memcpy(page, &g_slots, sizeof(g_slots));

    const int rc = flash_safe_execute(slots_save_flash_op, page, 1000);
    if (rc != PICO_OK) {
        printf("[Slots] save flash_safe_execute failed: %d\n", rc);
        return false;
    }

    SlotsData verify{};
    memcpy(&verify, flash_slots(), sizeof(verify));
    if (memcmp(&verify, &g_slots, sizeof(g_slots)) == 0) {
        printf("[Slots] flash write verified\n");
        return true;
    }
    printf("[Slots] flash write VERIFY FAILED\n");
    return false;
}

void slots_load() {
    memcpy(&g_slots, flash_slots(), sizeof(g_slots));
    if (g_slots.magic != SLOTS_MAGIC) {
        printf("[Slots] flash sector empty/invalid, initializing\n");
        memset(&g_slots, 0, sizeof(g_slots));
        g_slots.magic = SLOTS_MAGIC;
        save_slots_to_flash();
    }
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
    for (int i = 0; i < kNumSlots; i++) {
        if (g_slots.occupied[i] && memcmp(g_slots.addrs[i], addr, 6) == 0) return i;
    }
    return -1;
}

void slot_assign(int slot, const uint8_t addr[6]) {
    if (slot < 0 || slot >= kNumSlots) return;
    memcpy(g_slots.addrs[slot], addr, 6);
    g_slots.occupied[slot] = 1;
    save_slots_to_flash();
}

void slot_forget(int slot) {
    if (slot < 0 || slot >= kNumSlots) return;
    memset(g_slots.addrs[slot], 0, 6);
    g_slots.occupied[slot] = 0;
    save_slots_to_flash();
}

void slots_wipe_all() {
    for (int i = 0; i < kNumSlots; i++) {
        memset(g_slots.addrs[i], 0, 6);
        g_slots.occupied[i] = 0;
    }
    save_slots_to_flash();
}

bool slots_any_empty() {
    for (int i = 0; i < kNumSlots; i++) {
        if (!g_slots.occupied[i]) return true;
    }
    return false;
}
