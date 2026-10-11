#ifndef DS5_BRIDGE_OLED_H
#define DS5_BRIDGE_OLED_H

#include <stdint.h>

void oled_init();
void oled_loop();
void oled_send_trigger_preset(int preset); // 0..6, the Trigger Test presets

// Screen mirror for the app (vendor 0xEF, protocol 1.4): a 4-byte header
// (power state 0 on / 1 dimmed / 2 off, current screen, contrast, frame
// counter) and the 128x64 image exactly as last sent to the panel, row-major,
// 16 bytes per row, MSB = leftmost pixel. Returns 0 if `cap` is too small.
constexpr uint16_t kOledMirrorBytes = 4 + 128 * 64 / 8;
uint16_t oled_mirror(uint8_t *out, uint16_t cap);

#endif // DS5_BRIDGE_OLED_H
