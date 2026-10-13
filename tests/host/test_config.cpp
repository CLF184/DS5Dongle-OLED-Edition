// src/config.cpp: defaults and clamping (config_valid), the flash round trip
// with CRC, the legacy-sector migration, and set_config keeping the narrator.
#include <cmath>
#include <cstddef>
#include <cstring>
#include "config.h"
#include "fakes.h"
#include "flash_layout.h"
#include "hardware/flash.h"
#include "test.h"

TEST(config_erased_flash_gives_documented_defaults) {
    fake_flash_erase_all();
    config_load();
    const Config_body &c = get_config();
    CHECK(c.haptics_gain == 1.0f);
    CHECK(c.haptics_gain_right == 1.0f);
    CHECK(c.speaker_volume == 0.0f);
    CHECK_EQ(c.inactive_time, 30);
    CHECK_EQ(c.lightbar_mode, 8);
    CHECK_EQ(c.current_slot, 0);
    CHECK_EQ(c.narrator_voice, 0xFF);
    CHECK_EQ(c.screen_dim_timeout, 2);
    CHECK_EQ(c.screen_off_timeout, 15);
    CHECK(config_unsaved());  // nothing valid saved yet
}

TEST(config_save_load_round_trip) {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    config_load();
    Config_body b = get_config();
    b.haptics_gain = 2.5f;
    b.inactive_time = 45;
    b.lb_fav_r[2] = 7;
    set_config(b);
    CHECK(config_unsaved());
    CHECK(config_save());
    CHECK(!config_unsaved());
    config_default();
    config_load();
    CHECK(get_config().haptics_gain == 2.5f);
    CHECK_EQ(get_config().inactive_time, 45);
    CHECK_EQ(get_config().lb_fav_r[2], 7);
}

TEST(config_clamps_bad_values) {
    fake_flash_erase_all();
    config_load();
    Config_body b = get_config();
    b.haptics_gain = NAN;
    b.haptics_gain_right = 9.0f;
    b.speaker_volume = 50.0f;
    b.inactive_time = 2;
    b.polling_rate_mode = 9;
    b.current_slot = 7;
    set_config(b);
    const Config_body &c = get_config();
    CHECK(c.haptics_gain == 1.0f);
    CHECK(c.haptics_gain_right == 1.0f);  // falls back to the left grip's
    CHECK(c.speaker_volume == 0.0f);
    CHECK_EQ(c.inactive_time, 30);
    CHECK_EQ(c.polling_rate_mode, 0);
    CHECK_EQ(c.current_slot, 0);
}

TEST(config_host_write_keeps_narrator) {
    fake_flash_erase_all();
    config_load();
    CHECK(config_save_narrator(3));
    Config_body b = get_config();
    b.narrator_voice = 6;
    set_config(reinterpret_cast<const uint8_t *>(&b), sizeof(b));
    CHECK_EQ(get_config().narrator_voice, 3);
    // A short (older app) body: only its bytes change.
    uint8_t shortb[5];
    std::memcpy(shortb, &b, sizeof(shortb));
    set_config(shortb, sizeof(shortb));
    CHECK_EQ(get_config().narrator_voice, 3);
}

TEST(config_migrates_legacy_sector) {
    fake_flash_erase_all();
    config_load();
    Config_body b = get_config();
    b.inactive_time = 12;
    set_config(b);
    CHECK(config_save());
    // Move the saved page to the old last-sector location, erase the new one.
    std::memcpy(g_fake_flash + LEGACY_CONFIG_FLASH_OFFSET, g_fake_flash + CONFIG_FLASH_OFFSET, FLASH_PAGE_SIZE);
    std::memset(g_fake_flash + CONFIG_FLASH_OFFSET, 0xFF, FLASH_SECTOR_SIZE);
    config_default();
    config_load();
    CHECK_EQ(get_config().inactive_time, 12);
    CHECK(!config_unsaved());  // written to the new sector
}

// PC sleep options (Config::power_flags, after the body, protocol 1.13).
TEST(config_power_flags_defaults_and_round_trip) {
    fake_flash_erase_all();
    g_fake_flash_fail = false;
    config_load();
    CHECK_EQ(config_power_flags(), POWER_FLAGS_DEFAULT);  // erased flash
    CHECK_EQ(POWER_FLAGS_DEFAULT, POWER_SLEEP_CONTROLLER_OFF);  // upstream: power-off on, wake off
    CHECK(config_save());
    // A page older firmware wrote: the byte after its Config is 0xFF.
    CHECK_EQ(g_fake_flash[CONFIG_FLASH_OFFSET + offsetof(Config, power_flags)], POWER_FLAGS_DEFAULT);
    g_fake_flash[CONFIG_FLASH_OFFSET + offsetof(Config, power_flags)] = 0xFF;  // (the fake flash is RAM)
    config_load();
    CHECK_EQ(config_power_flags(), POWER_FLAGS_DEFAULT);
    CHECK_EQ(sizeof(Config_body), 42u);  // the wire body (0xF7) doesn't grow

    // Saved on their own: an unsaved body edit stays unsaved.
    CHECK(config_save());
    Config_body b = get_config();
    b.inactive_time = 44;
    set_config(b);
    CHECK(config_save_power_flags(POWER_PS_WAKES_PC));
    CHECK(config_unsaved());
    config_load();
    CHECK_EQ(config_power_flags(), POWER_PS_WAKES_PC);
    CHECK_EQ(get_config().inactive_time, 30);
    CHECK(!config_save_power_flags(0x80));  // invalid bits
    CHECK_EQ(config_power_flags(), POWER_PS_WAKES_PC);

    // The narrator's own save keeps them, and they count as unsaved changes.
    CHECK(config_save_narrator(2));
    config_load();
    CHECK_EQ(config_power_flags(), POWER_PS_WAKES_PC);
    config_set_power_flags(0x03);
    CHECK(config_unsaved());
    CHECK(config_save());
    CHECK(!config_unsaved());
    config_load();
    CHECK_EQ(config_power_flags(), 0x03);
    config_set_power_flags(0x40);  // out of range -> default
    CHECK_EQ(config_power_flags(), POWER_FLAGS_DEFAULT);

    // A factory reset brings back the default.
    config_set_power_flags(0x02);
    config_default();
    CHECK_EQ(config_power_flags(), POWER_FLAGS_DEFAULT);
}
