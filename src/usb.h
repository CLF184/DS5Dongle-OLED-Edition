//
// Created by awalol on 2026/3/4.
//

#ifndef DS5_BRIDGE_USB_H
#define DS5_BRIDGE_USB_H

#include <cstdint>

extern uint8_t mute[2]; // 0: SPEAKER(0x02) 1: MIC(0x05)
extern float volume[2]; // 0: SPEAKER(0x02) 1: MIC(0x05)
// When the PC last set the speaker's mute or volume (time_us_64, 0 = never).
uint64_t usb_host_speaker_set_us();

#endif //DS5_BRIDGE_USB_H