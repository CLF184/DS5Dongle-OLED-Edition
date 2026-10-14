//
// Mono speaker audio on a headset: both ears, not just the right one.
//
// Linux's DualSense UCM profile (alsa-ucm-conf USB-Audio/Sony/DualSense-PS5-
// HiFi.conf) has two output devices on the same 4-channel USB stream: the
// "Speaker" device writes its mono signal to USB channel 2 only (Channel0 1)
// and leaves channel 1 digitally silent, the "Headphones" device writes stereo
// to channels 1/2. The dongle encodes channels 1/2 as the stereo speaker
// stream. With the 0x39 speaker block tagged for the speaker (0x13) that's
// fine, but when Audio output routes it to the headset (0x16: Auto with a
// headset plugged in, or Headset), the Speaker device's mono plays in the
// right ear only.
//
// The USB stream doesn't say which device the host uses, so this watches the
// samples: channel 2 carrying signal while channel 1 stays exactly zero for
// kSpeakerMonoFrames in a row means the host plays the mono Speaker device;
// audio_loop then copies channel 2 into channel 1 while routing to the
// headset. The first non-zero sample on channel 1 ends it at once (stereo
// from the Headphones device or a game stays stereo); silence on both keeps
// the current state, so the gaps of a tune or a narrator line don't restart
// the count.
//
// Pure, no Pico SDK: it is inlined into audio_loop (RAM-resident, core0).
//

#ifndef DS5_BRIDGE_SPEAKER_MONO_H
#define DS5_BRIDGE_SPEAKER_MONO_H

#include <cstdint>

// 100 ms at 48 kHz: right-only audio this long with an exactly silent left is
// the Speaker device, not a hard-panned sound in a stereo mix.
constexpr uint32_t kSpeakerMonoFrames = 4800;

struct SpeakerMonoDetect {
    uint32_t right_only = 0;  // frames of right-only signal since the left was last non-zero
    bool mono = false;

    // Once per USB read: whether channel 1 / 2 had any non-zero sample in its
    // `frames` frames. Returns true while the stream is the Speaker device's mono.
    __attribute__((always_inline)) inline bool update(bool left, bool right, uint32_t frames) {
        if (left) {
            right_only = 0;
            mono = false;
        } else if (right && !mono) {
            right_only += frames;
            if (right_only >= kSpeakerMonoFrames) mono = true;
        }
        return mono;
    }
};

#endif
