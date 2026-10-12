#pragma once
#include <cstdint>
#include <map>
#include <vector>
void fake_flash_erase_all();
extern bool g_fake_flash_fail;
extern uint64_t g_fake_time_us;
extern bool g_fake_bt_connected;
extern int g_fake_slot;
extern int g_fake_reboots;
extern std::map<uint8_t, std::vector<uint8_t>> g_fake_features;
extern std::vector<std::vector<uint8_t>> g_fake_feature_sets;
extern int g_fake_feature_gets;
extern void (*g_fake_feature_get_hook)(uint8_t id);
