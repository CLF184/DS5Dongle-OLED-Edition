// When the narrator's "connected" line starts, and the silence around a line
// (voice.cpp). Pure, checked by tests/host/test_voice_cue.cpp.
//
// The line used to start a fixed 2.5 s after the link came up, straight into
// a speaker path that had carried no audio since the controller woke, with
// her first word only 100 ms into the clip, and its reports went out every
// 20 ms while the controller plays one per 21.33 ms. The owner heard the start
// cut off on every connect. Now it starts once the path is shown to be ready,
// a cold path gets ~300 ms of silent frames first (the controller's decoder,
// buffer and amp are running before her first word), and the reports keep
// the controller's own rate (report_clock.h).
#ifndef DS5_BRIDGE_VOICE_CUE_H
#define DS5_BRIDGE_VOICE_CUE_H

#include <cstdint>

// The connected line waits until all of these hold (or kLatestUs passes):
//   - kEarliestUs since the link came up;
//   - the controller's input reports have arrived for kSteadyUs with no gap
//     over kGapUs (the link carries traffic both ways, no stall);
//   - the connect-time state (volumes, AudioControl with NoiseCancelEnable)
//     went out kStateSettleUs ago (audio.cpp re-sends it once USB is up);
//   - the PC has set the speaker volume or mute since the link came up, and
//     not again for kHostQuietUs (Linux/WirePlumber, Windows and macOS all
//     restore it right after enumeration; a change mid-word jumps the level).
// The fallback is the old fixed start, so she is never later than before and
// always plays (the PC asleep, no USB host, a host that never sets volume).
struct ConnectCueGate {
    static constexpr uint64_t kEarliestUs = 800'000;
    static constexpr uint64_t kLatestUs = 2'500'000;
    static constexpr uint64_t kSteadyUs = 300'000;
    static constexpr uint64_t kGapUs = 40'000;
    static constexpr uint64_t kStateSettleUs = 150'000;
    static constexpr uint64_t kHostQuietUs = 300'000;

    enum Verdict : uint8_t { kWait, kReady, kFallback };

    bool armed = false;
    uint64_t link_us = 0;
    uint32_t input_count = 0;
    uint64_t last_input_us = 0;    // 0 = none yet
    uint64_t steady_since_us = 0;
    uint64_t state_us = 0;         // 0 = connect-time state not sent yet

    void arm(uint64_t now_us, uint32_t inputs) {
        armed = true;
        link_us = now_us;
        input_count = inputs;
        last_input_us = 0;
        steady_since_us = 0;
        state_us = 0;
    }
    void disarm() { armed = false; }

    // One main-loop pass. inputs: the controller's input report counter;
    // state_sent: the connect-time state went out on this link; host_spk_us:
    // when the PC last set the speaker volume or mute (0 = never).
    Verdict tick(uint64_t now_us, uint32_t inputs, bool state_sent, uint64_t host_spk_us) {
        if (!armed) return kWait;
        if (inputs != input_count) {
            input_count = inputs;
            if (last_input_us == 0 || now_us - last_input_us > kGapUs) steady_since_us = now_us;
            last_input_us = now_us;
        }
        if (state_sent && state_us == 0) state_us = now_us;
        const uint64_t since_link = now_us - link_us;
        const bool steady = last_input_us != 0 && now_us - last_input_us <= kGapUs &&
                            now_us - steady_since_us >= kSteadyUs;
        const bool state_ok = state_us != 0 && now_us - state_us >= kStateSettleUs;
        const bool host_ok = host_spk_us > link_us && now_us - host_spk_us >= kHostQuietUs;
        Verdict v = kWait;
        if (since_link >= kEarliestUs && steady && state_ok && host_ok) v = kReady;
        else if (since_link >= kLatestUs) v = kFallback;
        if (v != kWait) armed = false;
        return v;
    }
};

// Silence around a line on a cold speaker path (no host audio flowing):
// before it, 28 frames = 14 reports = 299 ms at the controller's 10.67 ms a
// frame; after it, 8 frames = 85 ms so her last syllable isn't left in the
// controller's buffer when the stream stops. Even counts: two frames a report.
constexpr uint16_t kVoicePreRollFrames = 28;
constexpr uint16_t kVoicePostRollFrames = 8;
static_assert(kVoicePreRollFrames % 2 == 0 && kVoicePostRollFrames % 2 == 0, "two frames a report");

// One 10 ms frame of digital silence as libopus encodes it for the bank's
// format (CELT fullband, 10 ms, stereo: TOC 0xF4, then CELT's silence flag);
// ffmpeg -f lavfi -i anullsrc=r=48000:cl=stereo with tools/build_voice_bank.py's
// encoder options gives exactly these three bytes for every frame.
constexpr uint8_t kOpusSilence10msStereo[3] = {0xF4, 0xFF, 0xFE};

#endif // DS5_BRIDGE_VOICE_CUE_H
