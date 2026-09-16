//
// Created by awalol on 2026/5/4.
//

#include "config.h"

#include <cmath>
#include <cstring>

#include "utils.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/btstack_flash_bank.h"
#include "pico/cyw43_arch.h"
#include "pico/flash.h"

constexpr uint32_t CONFIG_MAGIC = 0x66ccff00;
constexpr uint16_t CONFIG_VERSION = 1;
// Upstream dbf2a71 parity: keep config one sector *below* the btstack flash
// bank. On RP2350 the very last flash sector is reserved for the RP2350-E10
// errata workaround — the old position (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
// sat exactly there and could collide with chip-errata flash handling.
constexpr uint32_t CONFIG_FLASH_OFFSET = PICO_FLASH_BANK_STORAGE_OFFSET - FLASH_SECTOR_SIZE;
static Config config{};
bool is_dse = false;

// 编译期保护
// 判断Config结构体是否能放进flash 256bytes
static_assert(sizeof(Config) <= FLASH_PAGE_SIZE);
// 配置区起始地址必须按 flash sector 对齐。
static_assert(CONFIG_FLASH_OFFSET % FLASH_SECTOR_SIZE == 0);

static uint32_t calc_config_crc(const Config &con) {
    return crc32(reinterpret_cast<const uint8_t *>(&con.body), sizeof(Config_body));
}

static const Config *get_xip_addr(uint32_t offset) {
    return reinterpret_cast<const Config *>(XIP_BASE + offset);
}

void config_valid() {
    // valid config and set default value
    if (config.magic != CONFIG_MAGIC) {
        config.magic = CONFIG_MAGIC;
        printf("[Config] Config Magic Header is invalid\n");
    }
    if (config.version != CONFIG_VERSION) {
        config.version = CONFIG_VERSION;
        printf("[Config] Config Version is invalid\n");
    }
    if (config.size != sizeof(Config_body)) {
        config.size = sizeof(Config_body);
        printf("[Config] Config Body size is invalid\n");
    }
    auto body = &config.body;
    if (std::isnan(body->haptics_gain) || body->haptics_gain < 1.0f || body->haptics_gain > 2.0f) {
        body->haptics_gain = 1.0f;
        printf("[Config] Haptics Gain value is invalid\n");
    }
    if (std::isnan(body->speaker_volume) || body->speaker_volume < -100 || body->speaker_volume > 0) {
        body->speaker_volume = 0;  // OLED Edition: 0 dB default (unity); -100 would be silent
        printf("[Config] Speaker Volume is invalid, defaulting to 0 dB\n");
    }
    if (body->inactive_time < 5 || body->inactive_time > 60) {
        body->inactive_time = 30;
        printf("[Config] Inactive time is invalid\n");
    }
    if (body->disable_inactive_disconnect > 1) {
        body->disable_inactive_disconnect = 0;
        printf("[Config] disable_auto_disconnect is invalid\n");
    }
    if (body->disable_pico_led > 1) {
        body->disable_pico_led = 0;
        printf("[Config] disable_pico_led is invalid\n");
    }
    if (body->polling_rate_mode > 2) {
        body->polling_rate_mode = 0;
        printf("[Config] polling_rate_mode is invalid\n");
    }
    if (body->audio_buffer_length < 16 || body->audio_buffer_length > 128) {
        body->audio_buffer_length = 64;
        printf("[Config] haptics_buffer_length is invalid\n");
    }
    if (body->controller_mode > 2) {
        body->controller_mode = 2;
        printf("[Config] controller_mode is invalid\n");
    }
    if (body->current_slot >= 4) {
        body->current_slot = 0;
        printf("[Config] current_slot is invalid\n");
    }
    if (body->auto_haptics_enable > 3) {
        body->auto_haptics_enable = 1; // Fallback default
        printf("[Config] auto_haptics_enable invalid, defaulting to 1 (Fallback)\n");
    }
    if (body->auto_haptics_gain > 200) {
        body->auto_haptics_gain = 100;
        printf("[Config] auto_haptics_gain invalid, defaulting to 100\n");
    }
    if (body->auto_haptics_lowpass > 3) {
        body->auto_haptics_lowpass = 1; // 160 Hz
        printf("[Config] auto_haptics_lowpass invalid, defaulting to 1 (160 Hz)\n");
    }
    if (body->lightbar_mode > 8) { // 0..7 OLED modes + 8 = HOST passthrough (default)
        body->lightbar_mode = 8;
        printf("[Config] lightbar_mode invalid, defaulting to 8 (HOST passthrough)\n");
    }
    // lb_fav_{r,g,b} need no validation — any 0..255 is a legal color, and an
    // erased flash sector (0xFF) yields 4 white favorites, a usable default.
    if (body->screen_dim_timeout > 250) { // 0xFF erased / out of range → default
        body->screen_dim_timeout = 2;     // mirrors the original 2-min dim tier
        printf("[Config] screen_dim_timeout invalid, defaulting to 2 min\n");
    }
    if (body->screen_off_timeout > 250) {
        body->screen_off_timeout = 15;    // mirrors the original 15-min off tier
        printf("[Config] screen_off_timeout invalid, defaulting to 15 min\n");
    }
    if (body->bt_mic_enable > 1) {        // 0xFF erased / upgrade → default ON
        body->bt_mic_enable = 1;
        printf("[Config] bt_mic_enable invalid, defaulting to 1 (on)\n");
    }
    if (body->screen_brightness > 3) {    // kBrightLevels has 4 entries (0..3)
        body->screen_brightness = 0;      // full brightness
        printf("[Config] screen_brightness invalid, defaulting to 0 (full)\n");
    }
    if (body->controller_wakes_display > 1) { // 0xFF erased / upgrade → default ON
        body->controller_wakes_display = 1;
        printf("[Config] controller_wakes_display invalid, defaulting to 1 (on)\n");
    }
    // Host 0x02 passthrough overrides: 0 = auto (no override). Erased flash
    // (0xFF) and any out-of-range value fall back to auto.
    if (body->trigger_reduce > 10) {
        body->trigger_reduce = 0; // auto
        printf("[Config] trigger_reduce invalid, defaulting to 0 (auto)\n");
    }
    if (body->speaker_gain > 7) {
        body->speaker_gain = 0; // auto
        printf("[Config] speaker_gain invalid, defaulting to 0 (auto)\n");
    }
    if (body->mic_select > 3) {
        body->mic_select = 0; // auto
        printf("[Config] mic_select invalid, defaulting to 0 (auto)\n");
    }
    if (body->lock_volume > 1) {
        body->lock_volume = 0; // unlocked
        printf("[Config] lock_volume invalid, defaulting to 0 (off)\n");
    }
    if (body->config_version != CONFIG_VERSION) {
        body->config_version = CONFIG_VERSION;
        printf("[Config] Warning: Config may breaking change\n");
    }
}

void config_load() {
    memcpy(&config, get_xip_addr(CONFIG_FLASH_OFFSET), sizeof(Config));

    config_valid();
}

// Reset RAM-resident config body to firmware defaults. Caller must
// config_save() to persist. Filling with 0xFF mirrors the byte pattern
// of a freshly-erased flash sector, so every field fails validity and
// gets re-initialized to its documented default by config_valid().
void config_default() {
    memset(&config.body, 0xFF, sizeof(config.body));
    config_valid();
}

// Runs with core1 parked (flash_safe_execute) and core0 interrupts disabled, so
// neither core touches XIP flash while the sector is erased/programmed. Without
// the core1 park this races the audio core and corrupts audio (buzzing).
static void config_save_flash_op(void *param) {
    const uint8_t *page = static_cast<const uint8_t *>(param);
    const uint32_t interrupts = save_and_disable_interrupts();
    flash_range_erase(CONFIG_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(CONFIG_FLASH_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(interrupts);
}

bool config_save() {
    config.crc32 = calc_config_crc(config);
    alignas(4) uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xff, sizeof(page));
    memcpy(page, &config, sizeof(Config));

    const int rc = flash_safe_execute(config_save_flash_op, page, 1000);
    if (rc != PICO_OK) {
        printf("[Config] config_save flash_safe_execute failed: %d\n", rc);
        return false;
    }

    Config verify{};
    memcpy(&verify, get_xip_addr(CONFIG_FLASH_OFFSET), sizeof(verify));
    const auto verify_crc32 = calc_config_crc(verify);
    if (verify_crc32 == config.crc32) {
        printf("[Config] Config write flash verify success\n");
        return true;
    }
    printf("[Config] Config write flash verify failed\n");
    return false;
}

const Config_body& get_config() {
    return config.body;
}

void set_config(const uint8_t *new_config, const uint16_t len) {
    const auto copy_len = len < sizeof(Config_body) ? len : sizeof(Config_body);
    memcpy(&config.body, new_config, copy_len);
    config_valid();
    if (config.body.disable_pico_led) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
    }else {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
    }
}

void set_config(const Config_body &new_config) {
    config.body = new_config;
    config_valid();
}
