//
// Created by awalol on 2026/3/5.
//

#ifndef DS5_BRIDGE_AUDIO_H
#define DS5_BRIDGE_AUDIO_H

#include <cstdint>

void audio_init();
void audio_loop();
void core1_entry();
void set_headset(bool state);

// Host-gate for the mic IN stream. Called from tud_audio_set_itf_cb (main.cpp)
// when the host opens/closes the mic IN interface (alt != 0). Mirrors upstream
// PR #160: the DS5 only streams mic audio while something is recording.
void set_mic_active(bool active);
bool audio_mic_active();
// Upstream PR #160 parity: send a 0x32 status report to the controller so it
// starts/stops streaming its mic immediately when the host opens/closes the
// mic IN interface. Called by set_mic_active().
void update_mic_status();

// Accessors used by the optional OLED add-on (diag + VU meter screens).
uint32_t audio_fifo_drops();
uint32_t opus_fifo_drops();
uint8_t  audio_peak_speaker();   // 0..255; time-based release, ~256 ms to zero (pure getter)
uint8_t  audio_peak_haptic();    // 0..255; time-based release, ~256 ms to zero (pure getter)

// Byte-flow counters for the Diagnostics screen + web emulator.
uint32_t audio_usb_frames();
uint32_t audio_bt_packets();
uint32_t audio_mic_frames();   // count of mic Opus frames decoded + written
int32_t  audio_mic_last_decoded(); // last opus_decode return — neg = error, 480 = OK
uint16_t audio_mic_last_want();    // bytes asked of tud_audio_write
uint16_t audio_mic_last_wrote();   // bytes TinyUSB FIFO actually accepted
uint8_t  audio_mic_last_toc();     // first byte of last Opus packet (frame config)
uint32_t audio_mic_decode_failures(); // opus_decode <= 0 count (bad/missing packets)
uint16_t audio_ah_out_peak();   // auto-haptics actual-output peak (0-127), 0 = not contributing

// Called from on_bt_data() in main.cpp when the DS5 sends a mic-tagged
// 0x31 input report. data points at the Opus payload, len is the bytes
// available there (upstream PR #160 signature: the function validates
// len >= MIC_OPUS_SIZE itself).
void mic_add_queue(uint8_t *data, uint16_t len);

#endif //DS5_BRIDGE_AUDIO_H
