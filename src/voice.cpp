// The narrator's firmware lines (see voice.h).
//
// The lines live in flash as src/voice_bank.bin (tools/build_voice_bank.py):
// Opus packets already encoded the way the DualSense's speaker takes them
// (48 kHz stereo, 10 ms, CELT), stored compact and padded to the 0x39
// report's fixed frame size as they go out (opus_packet_pad). Nothing is
// decoded or encoded here, so core1's speaker path is untouched: while a line
// plays, its frames simply take the place of the host's speaker frames in the
// audio report (game audio pauses for the second or two), and when the host
// isn't streaming audio at all, voice_task() sends the reports itself.
#include "voice.h"

#include <cstring>
#include "audio.h"
#include "bt.h"
#include "config.h"
#include "opus.h"
#include "pico/time.h"
#include "usb.h"

extern uint8_t interrupt_in_data[63];

__asm__(
    ".section .rodata.voice_bank,\"a\"\n"
    ".balign 4\n"
    ".global voice_bank\n"
    "voice_bank:\n"
    ".incbin \"" VOICE_BANK_FILE "\"\n"
    ".global voice_bank_end\n"
    "voice_bank_end:\n"
    ".previous\n");
extern "C" const uint8_t voice_bank[];
extern "C" const uint8_t voice_bank_end[];

namespace {
constexpr uint32_t kHeader = 8;
// After the controller connects: long enough for the host to have set the
// speaker volume and for the controller's own connect chime to finish.
constexpr uint64_t kConnectDelayUs = 2500 * 1000;
// A slot switch drops the controller after its line; never wait longer.
constexpr uint64_t kSlotLineMaxUs = 4000 * 1000;
// Two frames (20 ms) per report, as the host path sends them. The first few
// go out at once, so the controller's buffer has a head start and a busy
// moment in the main loop can't run it dry (a hitch mid-word); then a fixed
// 20 ms clock that catches up after a late report instead of drifting.
constexpr uint64_t kReportUs = 20 * 1000;
constexpr uint8_t kLeadReports = 3;
uint8_t s_lead = 0;

const uint8_t *s_pos = nullptr;      // next frame (length byte), null = idle
uint16_t s_left = 0;                 // frames left
uint64_t s_next_report_us = 0;
uint64_t s_connected_at_us = 0;      // 0 = no pending connect cue
bool s_was_connected = false;
bool s_battery_warned = false;
uint8_t s_battery_low_reads = 0;
uint64_t s_next_battery_us = 0;
bool s_disconnect_after = false;
uint64_t s_disconnect_by_us = 0;

bool bank_ok() {
    return voice_bank_end - voice_bank > (ptrdiff_t) kHeader && memcmp(voice_bank, "DS5V", 4) == 0 &&
           voice_bank[4] == 1 && voice_bank[5] == kVoiceNumLangs && voice_bank[6] == VOICE_NUM_LINES;
}

// The configured language (0..6), or -1 with the narrator off.
int configured_lang() {
    const uint8_t v = get_config().narrator_voice;
    return v >= 1 && v <= kVoiceNumLangs ? v - 1 : -1;
}
} // namespace

void voice_say_lang(VoiceLine line, uint8_t lang) {
    if (line >= VOICE_NUM_LINES || lang >= kVoiceNumLangs || !bt_is_connected() || !bank_ok()) return;
    const uint8_t *entry = voice_bank + kHeader + (lang * VOICE_NUM_LINES + line) * 8;
    uint32_t offset;
    uint16_t frames;
    memcpy(&offset, entry, 4);
    memcpy(&frames, entry + 4, 2);
    if (offset >= (uint32_t) (voice_bank_end - voice_bank)) return;
    s_pos = voice_bank + offset;
    s_left = frames;
    s_next_report_us = time_us_64();
    s_lead = kLeadReports;
}

void voice_say(VoiceLine line) {
    const int lang = configured_lang();
    // The host muting the controller's speaker mutes her too.
    if (lang >= 0 && !mute[0]) voice_say_lang(line, (uint8_t) lang);
}

bool voice_active() { return s_pos != nullptr; }

void voice_disconnect_after_line() {
    s_disconnect_after = true;
    s_disconnect_by_us = time_us_64() + kSlotLineMaxUs;
}

static bool take_one(uint8_t *frame, uint16_t frame_size) {
    memset(frame, 0, frame_size);
    if (!s_pos || s_left == 0) return false;
    const uint8_t len = s_pos[0];
    if (len == 0 || len > frame_size || s_pos + 1 + len > voice_bank_end) {
        s_pos = nullptr;
        return false;
    }
    memcpy(frame, s_pos + 1, len);
    s_pos += 1 + len;
    s_left--;
    // A valid Opus packet of exactly frame_size bytes (padding, not trailing
    // garbage the decoder would read as audio).
    if (len < frame_size && opus_packet_pad(frame, len, frame_size) != OPUS_OK) memset(frame, 0, frame_size);
    return true;
}

// The bank's lines have an even number of frames, so a report is never half
// voice, half nothing.
bool voice_take_frames(uint8_t *frame0, uint8_t *frame1, uint16_t frame_size) {
    if (!s_pos) return false;
    take_one(frame0, frame_size);
    take_one(frame1, frame_size);
    if (s_left == 0) s_pos = nullptr;
    return true;
}

void voice_task() {
    const uint64_t now = time_us_64();
    const bool connected = bt_is_connected();
    if (connected && !s_was_connected) {
        s_connected_at_us = now;
        s_battery_warned = false;
        s_battery_low_reads = 0;
    }
    if (!connected) {
        s_pos = nullptr;
        s_connected_at_us = 0;
        s_disconnect_after = false;
    }
    s_was_connected = connected;
    if (!connected) return;

    if (s_connected_at_us && now - s_connected_at_us >= kConnectDelayUs) {
        s_connected_at_us = 0;
        voice_say(VOICE_CONNECTED);
    }

    // Battery low, once per connection: discharging at 10 % or less (the
    // Pico LED's threshold), three reads in a row so a dip doesn't trigger it.
    // Charging, or back above 30 %, re-arms it.
    if (now >= s_next_battery_us) {
        s_next_battery_us = now + 1000 * 1000;
        const uint8_t level = interrupt_in_data[52] & 0x0F;
        const uint8_t power = interrupt_in_data[52] >> 4;
        if (power != 0 || level >= 3) {
            s_battery_warned = false;
            s_battery_low_reads = 0;
        } else if (level <= 1 && !s_battery_warned && ++s_battery_low_reads >= 3 && !s_connected_at_us) {
            s_battery_warned = true;
            voice_say(VOICE_BATTERY_LOW);
        }
    }

    if (s_disconnect_after && (!s_pos || now >= s_disconnect_by_us)) {
        s_disconnect_after = false;
        s_pos = nullptr;
        bt_disconnect();
        return;
    }

    // No host audio flowing: the line's reports go out on their own clock.
    if (s_pos && !audio_host_streaming() && now >= s_next_report_us) {
        audio_send_voice_report();
        if (s_lead > 0) {
            s_lead--;
            if (s_lead == 0) s_next_report_us = now + kReportUs;
        } else {
            s_next_report_us += kReportUs;
            if (now > s_next_report_us + 5 * kReportUs) s_next_report_us = now + kReportUs; // stalled: resync
        }
    }
}
