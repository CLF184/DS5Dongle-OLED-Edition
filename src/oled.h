#ifndef DS5_BRIDGE_OLED_H
#define DS5_BRIDGE_OLED_H

void oled_init();
void oled_loop();
void oled_send_trigger_preset(int preset); // 0..6, the Trigger Test presets

#endif // DS5_BRIDGE_OLED_H
