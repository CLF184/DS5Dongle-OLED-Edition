// Include the actual OLED translation unit so tests exercise its internal
// menu descriptors, sampling and framebuffer rather than a second codec.
#include "test.h"
#include "fakes.h"
#include "../../src/oled.cpp"

extern uint32_t g_fake_audio_counters[4];

TEST(oled_settings_keep_upstream_ranges_and_linked_grips) {
    config_default();
    settings_local = get_config();
    CHECK_EQ(kNumSettingsItems, 17);
    settings_sel = 0;
    settings_local.haptics_gain = 1;
    settings_local.haptics_gain_right = 3;
    settings_adjust(1);
    CHECK_EQ(settings_local.haptics_gain * 10, 15);
    CHECK_EQ(settings_local.haptics_gain_right * 10, 35);
    char text[28];
    format_settings_item(0, text, sizeof(text));
    CHECK(std::strstr(text, "Hap L1.5 R3.5"));
    for (int i = 0; i < 20; ++i) settings_adjust(1);
    CHECK_EQ(settings_local.haptics_gain, 6);
    CHECK_EQ(settings_local.haptics_gain_right, 6);
    settings_sel = 1;
    settings_local.speaker_volume = 12;
    settings_adjust(-1);
    CHECK_EQ(settings_local.speaker_volume, 10);
    settings_adjust(-1);
    CHECK_EQ(settings_local.speaker_volume, 5);
    settings_local.speaker_volume = -12;
    settings_adjust(1);
    CHECK_EQ(settings_local.speaker_volume, -10);
    settings_adjust(-1);
    CHECK_EQ(settings_local.speaker_volume, -15);
    settings_sel = 5;
    settings_local.polling_rate_mode = 0;
    settings_adjust(-1);
    CHECK_EQ(settings_local.polling_rate_mode, 2);
    settings_sel = 11;
    settings_local.screen_dim_timeout = 0;
    settings_adjust(-1);
    CHECK_EQ(settings_local.screen_dim_timeout, 0);
    format_settings_item(11, text, sizeof(text));
    CHECK(std::strstr(text, "ScrDim off"));
    settings_sel = kSettingsResetIdx;
    settings_dirty = false;
    settings_adjust(1);
    CHECK(!settings_dirty);
}

TEST(oled_save_failure_keeps_dirty_feedback) {
    fake_flash_erase_all();
    config_default();
    settings_local = get_config();
    settings_sel = 1;
    settings_dirty = true;
    settings_last_face = 0x80; // Triangle release saves
    interrupt_in_data[7] = 8;
    g_fake_bt_connected = true;
    g_fake_flash_fail = true;
    settings_handle_input();
    CHECK(settings_dirty);
    CHECK_EQ(std::strcmp(settings_save_status, "Save FAIL"), 0);
    g_fake_flash_fail = false;
    settings_last_face = 0x80;
    settings_handle_input();
    CHECK(!settings_dirty);
    g_fake_bt_connected = false;
}

TEST(oled_rates_hold_one_second_windows_and_rebaseline_on_entry) {
    const auto saved_time = g_fake_time_us;
    const uint32_t saved[4] = {g_fake_audio_counters[0], g_fake_audio_counters[1], g_fake_audio_counters[2], g_fake_audio_counters[3]};
    g_fake_time_us = 100;
    for (auto &counter : g_fake_audio_counters) counter = 0;
    sample_diag_rates(true);
    g_fake_time_us += 200000;
    g_fake_audio_counters[0] = 9600;
    sample_diag_rates(false);
    CHECK_EQ(g_diag_rates.usb_rate, 48000);
    g_fake_time_us += 100000;
    g_fake_audio_counters[0] += 4000;
    sample_diag_rates(false);
    CHECK_EQ(g_diag_rates.usb_rate, 48000);  // not render-rate-dependent
    g_fake_time_us += 900000;
    g_fake_audio_counters[0] += 44000;
    sample_diag_rates(false);
    CHECK_EQ(g_diag_rates.usb_rate, 48000);
    g_fake_time_us += 10000000;
    sample_diag_rates(true);
    CHECK_EQ(g_diag_rates.usb_rate, 0);  // no off-page average or stale value
    g_fake_time_us += 200000;
    sample_diag_rates(false);
    CHECK_EQ(g_diag_rates.usb_rate, 0);
    for (int i = 0; i < 4; ++i) g_fake_audio_counters[i] = saved[i];
    g_fake_time_us = saved_time;
}

TEST(oled_byte_spans_match_pixel_operations) {
    for (int x = -9; x < 138; x += 7) {
        for (int w = 0; w < 140; w += 9) {
            for (int op = SPAN_SET; op <= SPAN_XOR; ++op) {
                memset(fb, 0xA5, sizeof(fb));
                uint8_t expected[kFbBytes];
                memcpy(expected, fb, sizeof(fb));
                for (int xx = x; xx < x + w; ++xx) {
                    if (xx < 0 || xx >= kW) continue;
                    uint8_t &v = expected[11 * kRowBytes + xx / 8];
                    const uint8_t mask = 0x80u >> (xx & 7);
                    if (op == SPAN_SET) v |= mask;
                    else if (op == SPAN_CLEAR) v &= ~mask;
                    else v ^= mask;
                }
                span(x, 11, w, static_cast<SpanOp>(op));
                CHECK_EQ(memcmp(fb, expected, sizeof(fb)), 0);
            }
        }
    }
    memset(fb, 0xA5, sizeof(fb));
    span(-20, -1, 200, SPAN_CLEAR);
    span(-20, kH, 200, SPAN_CLEAR);
    for (auto value : fb) CHECK_EQ(value, 0xA5);
}

TEST(oled_diagnostics_cache_invalidates_after_other_pages) {
    g_fake_bt_connected = false;
    diag_scroll = 0;
    g_fake_time_us = 1000000;
    render_screen_diag(true);
    uint8_t first[kFbBytes];
    memcpy(first, fb, sizeof(fb));
    const uint32_t generation = fb_generation;
    render_screen_diag(false);
    CHECK_EQ(fb_generation, generation); // identical rows need no full redraw
    CHECK_EQ(memcmp(first, fb, sizeof(fb)), 0);
    fb_clear(); // another page or the dim pulse cleared the framebuffer
    render_screen_diag(false);
    CHECK_EQ(memcmp(first, fb, sizeof(fb)), 0);
    diag_scroll = 1;
    render_screen_diag(false);
    CHECK(memcmp(first, fb, sizeof(fb)) != 0);
    diag_scroll = 0;
    render_screen_diag(false);
    CHECK_EQ(memcmp(first, fb, sizeof(fb)), 0);
}
