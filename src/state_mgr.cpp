//
// Created by awalol on 2026/5/15.
//

#include <cstddef>
#include <cstring>

#include "utils.h"
#include "state_mgr.h"
#include "config.h"

// Set by the OLED lightbar service (src/oled.cpp). While true, the firmware
// owns the lightbar (an OLED mode or the charging pulse) and the host's
// AllowLedColor writes are suppressed below so they can't stomp it.
extern bool g_lightbar_override;

namespace {
    constexpr size_t kAudioControlOffset = offsetof(SetStateData, MuteLightMode) - sizeof(uint8_t);
    constexpr size_t kMotorPowerLevelOffset = offsetof(SetStateData, HostTimestamp) + sizeof(uint32_t);
    constexpr size_t kAudioControl2Offset = kMotorPowerLevelOffset + sizeof(uint8_t);
}

static constexpr uint8_t state_init_data[63] = {
    0xfd, 0xf7, 0x0, 0x0,
    0x7f, 0x64, // Headphones, Speaker
    0x40, 0x9, 0x0, 0x00, 0x0, 0x0, 0x0, 0x0, // VolumeMic=64, MuteControl all clear (no PowerSave)
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xa,
    0x7, 0x0, 0x0, 0x2, 0x1,
    0x00,
    0xff, 0xd7, 0x00 // RGB LED: R, G, B (Nijika Color!)✨
};

uint8_t state[63]{};

void state_init() {
    memcpy(state, state_init_data, sizeof(state));
}

void state_set(uint8_t *data, const uint8_t size) {
    if (size > 63) {
        printf("[StateMgr] Warning: State Set over 63 bytes\n");
    }
    memcpy(data, state, size);
}

void state_set_led(uint8_t r, uint8_t g, uint8_t b) {
    state[offsetof(SetStateData, LedRed) + 0] = r;
    state[offsetof(SetStateData, LedRed) + 1] = g;
    state[offsetof(SetStateData, LedRed) + 2] = b;
}

void state_get_led(uint8_t *r, uint8_t *g, uint8_t *b) {
    *r = state[offsetof(SetStateData, LedRed) + 0];
    *g = state[offsetof(SetStateData, LedRed) + 1];
    *b = state[offsetof(SetStateData, LedRed) + 2];
}

void state_update(const uint8_t *data, const uint8_t size) {
    if (size < sizeof(SetStateData)) {
        printf(
            "[StateMgr] Error: SetStateData at least %u bytes\n",
            static_cast<unsigned>(sizeof(SetStateData))
        );
        return;
    }

    // Full host passthrough (upstream memcpy semantics): the 0x02 report
    // replaces the whole state block — allow bits included — so the controller
    // applies exactly what the host asked for.
    uint8_t saved_led[3];
    const bool lb_override = g_lightbar_override;
    if (lb_override) {
        memcpy(saved_led, state + offsetof(SetStateData, LedRed), sizeof(saved_led));
    }
    memcpy(state, data, sizeof(SetStateData));
    if (lb_override) {
        // OLED owns the lightbar: keep our color and clear AllowLedColor so the
        // controller ignores the host's LED bytes.
        memcpy(state + offsetof(SetStateData, LedRed), saved_led, sizeof(saved_led));
        state[1] &= ~(1 << 2); // AllowLedColor = 0
    }

    // Host-passthrough overrides (upstream a7824d9 parity): only applied when
    // the matching OLED config is non-auto (0 = auto = host value wins).
    const auto &cfg = get_config();
    auto set_bit = [](uint8_t &byte, const int bit, const bool value) {
        byte = (byte & ~(1 << bit)) | (value << bit);
    };
    if (cfg.trigger_reduce > 0) {
        set_bit(state[1], 6, true);  // AllowMotorPowerLevel
        state[kMotorPowerLevelOffset] = (state[kMotorPowerLevelOffset] & 0x0F) |
                                        ((cfg.trigger_reduce & 0x0F) << 4);
    }
    if (cfg.speaker_gain > 0) {
        set_bit(state[1], 7, true);  // AllowAudioControl2
        state[kAudioControl2Offset] = (state[kAudioControl2Offset] & ~0x07) |
                                      (cfg.speaker_gain & 0x07);
    }
    if (cfg.mic_select != 0) {
        set_bit(state[0], 7, true);  // AllowAudioControl
        state[kAudioControlOffset] = (state[kAudioControlOffset] & ~0x03) |
                                     (cfg.mic_select & 0x03);
        set_bit(state[kAudioControlOffset], 3, true);  // NoiseCancelEnable
    }
    if (cfg.lock_volume) {
        set_bit(state[0], 4, false);  // AllowHeadphoneVolume
        set_bit(state[0], 5, false);  // AllowSpeakerVolume
        set_bit(state[0], 6, false);  // AllowMicVolume
        set_bit(state[1], 0, false);  // AllowMuteLight
        set_bit(state[1], 1, false);  // AllowAudioMute
    }
}
