#pragma once
#include <cstdint>
void fake_flash_erase_all();
extern bool g_fake_flash_fail;
extern uint64_t g_fake_time_us;
extern bool g_fake_bt_connected;
extern int g_fake_slot;
extern int g_fake_reboots;
