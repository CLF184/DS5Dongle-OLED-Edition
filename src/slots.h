// Persistent 4-slot multi-controller pairing storage. Stores the 4 bonded
// DS5 bd_addrs in a custom flash sector (BTstack's NVM keeps the link_keys).
//
// Multi-slot pairing modeled on zurce/DS5Dongle-OLED. Credit to zurce.

#ifndef DS5_BRIDGE_SLOTS_H
#define DS5_BRIDGE_SLOTS_H

#include <cstdint>

constexpr int kNumSlots = 4;

void slots_load();
bool slot_occupied(int slot);
void slot_get_addr(int slot, uint8_t out[6]);
int  slot_owner_of(const uint8_t addr[6]);
// These three change the table in RAM, then save it: false = not saved
// (the flash write failed), so the change is lost at power-off.
bool slot_assign(int slot, const uint8_t addr[6]);
bool slot_forget(int slot);
bool slots_wipe_all();
bool slots_any_empty();

#endif // DS5_BRIDGE_SLOTS_H
