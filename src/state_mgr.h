//
// Created by awalol on 2026/5/15.
//

#ifndef DS5_BRIDGE_STATE_MGR_H
#define DS5_BRIDGE_STATE_MGR_H

#include <cstdint>

void state_init();
void state_set(uint8_t *data, const uint8_t size);
void state_update(const uint8_t *data, const uint8_t size);

// Lightbar RGB lives in the persistent state[] block (SetStateData LedRed/
// Green/Blue) that gets stamped into every outbound BT packet. The OLED
// lightbar service writes it directly so a firmware-chosen color rides every
// host/audio frame instead of only the transient send_lightbar_color() packet.
void state_set_led(uint8_t r, uint8_t g, uint8_t b);
void state_get_led(uint8_t *r, uint8_t *g, uint8_t *b);
// Reports the dongle sends on its own (lightbar, volume, trigger preview,
// connect state) carry the game's current rumble, so they never cancel it:
// the DualSense takes each output report as the whole rumble state.
// `ss` points at the SetStateData bytes of the report being built.
void state_stamp_host_rumble(uint8_t *ss);
// Forget the game's rumble (controller gone: a reconnect must not resume it).
void state_clear_host_rumble();
// Player indicator (the 5 white LEDs under the touchpad) as last set by the
// host: bits 0..4, one per LED left to right.
uint8_t state_get_player_leds();
// Controller amp volume (0..0x64 speaker, 0..0x7f headphones). Stored in
// state[] for the BT-connect init and pushed to the controller immediately.
void state_set_volume(uint8_t headphones, uint8_t speaker);

#endif //DS5_BRIDGE_STATE_MGR_H
