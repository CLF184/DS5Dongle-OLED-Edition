// The settings the dongle puts on the controller state it sends (protocol
// 1.13, upstream's batch 3): weaker adaptive triggers, the speaker amp's
// pre-gain, the microphone choice and the volume lock; (protocol 1.16) the
// controller's microphone level; and (protocol 1.17) its echo cancellation.
// Pure, so the host tests check it byte for byte.
//
// Upstream's v0.7.2 latched the whole controller state and re-sent it, every
// allow bit set, on each game report; adaptive triggers came out weak or gone
// (#228). Its fix (a7824d9) forwards the game's own report and changes only
// the fields a setting forces, which is what this does. Unlike upstream, a
// forced section keeps the rest of that byte as the game last set it (the
// rumble reduction next to the trigger one, the bits next to the pre-gain), so
// a game report that doesn't carry the section can't reset them.
#ifndef DS5_BRIDGE_STATE_OVERRIDE_H
#define DS5_BRIDGE_STATE_OVERRIDE_H

#include <cstdint>

struct StateOverrides {
    uint8_t trigger_reduce;  // 0 = the game's own, 1..10
    uint8_t speaker_gain;    // 0 = the game's own, 1..7
    uint8_t lock_volume;     // bool
    uint8_t mic_select;      // 0 auto, 1 built-in, 2 headset
    uint8_t mic_level;       // 0 = the PC's level, 1..kMicLevelMax (1.16)
    uint8_t echo_cancel;     // 1 = EchoCancelEnable forced on; 0 = the game's own (1.17)
};

// The controller's VolumeMic tops out at 0x40 (SetStateData [6]); the PC's
// 48 dB maps onto it (usb.cpp), and a fixed mic level (1.16) is 1..this.
constexpr uint8_t kMicLevelMax = 0x40;

// The sections the game last set, in the controller's own bytes.
struct HostAudioBytes {
    uint8_t audio_control;   // SetStateData [7] (NoiseCancelEnable etc.; MicSelect = bits 0..1)
    uint8_t motor_power;     // [36]: rumble reduction low nibble, trigger reduction high nibble
    uint8_t audio_control2;  // [37]: SpeakerCompPreGain = bits 0..2
};

// What the controller holds before any game report (state_init_data in
// state_mgr.cpp: NoiseCancelEnable on, which the speaker needs; upstream
// b821623).
constexpr HostAudioBytes kHostAudioDefaults = {0x08, 0x00, 0x00};

namespace state_bits {
constexpr uint8_t kF0AllowHeadphoneVolume = 0x10;
constexpr uint8_t kF0AllowSpeakerVolume = 0x20;
constexpr uint8_t kF0AllowMicVolume = 0x40;
constexpr uint8_t kF0AllowAudioControl = 0x80;
constexpr uint8_t kF1AllowMuteLight = 0x01;
constexpr uint8_t kF1AllowAudioMute = 0x02;
constexpr uint8_t kF1AllowMotorPowerLevel = 0x40;
constexpr uint8_t kF1AllowAudioControl2 = 0x80;
constexpr int kVolumeMic = 6;
constexpr int kAudioControl = 7;
constexpr int kMotorPower = 36;
constexpr int kAudioControl2 = 37;
// AudioControl [7] bit 2: the controller's own echo cancellation for its
// microphone (EchoCancelEnable; Apple's DualSense driver: echoCancelEnable).
constexpr uint8_t kAudioEchoCancel = 0x04;
// AudioControl [7] bit 3: NoiseCancelEnable. Without it the speaker plays at
// about half level (upstream awalol b821623, #263).
constexpr uint8_t kAudioNoiseCancel = 0x08;
}  // namespace state_bits

// Remember the sections a game report sets (ss = its SetStateData bytes).
inline void record_host_audio(HostAudioBytes &h, const uint8_t *ss) {
    using namespace state_bits;
    if (ss[0] & kF0AllowAudioControl) h.audio_control = ss[kAudioControl];
    if (ss[1] & kF1AllowMotorPowerLevel) h.motor_power = ss[kMotorPower];
    if (ss[1] & kF1AllowAudioControl2) h.audio_control2 = ss[kAudioControl2];
}

// Put the settings on a report's SetStateData bytes (47). from_host: a game's
// report being forwarded (the volume lock applies, and "auto" leaves its
// choices alone); else one of the dongle's own (the connect-time state), whose
// mic choice is always the setting's.
inline void stamp_overrides(uint8_t *ss, const StateOverrides &o, const HostAudioBytes &h, bool from_host) {
    using namespace state_bits;
    if (o.trigger_reduce) {
        if (!(ss[1] & kF1AllowMotorPowerLevel)) ss[kMotorPower] = h.motor_power;
        ss[1] |= kF1AllowMotorPowerLevel;
        ss[kMotorPower] = (uint8_t) ((ss[kMotorPower] & 0x0F) | (o.trigger_reduce << 4));
    }
    if (o.speaker_gain) {
        if (!(ss[1] & kF1AllowAudioControl2)) ss[kAudioControl2] = h.audio_control2;
        ss[1] |= kF1AllowAudioControl2;
        ss[kAudioControl2] = (uint8_t) ((ss[kAudioControl2] & ~0x07) | (o.speaker_gain & 0x07));
    }
    if (o.mic_select || (!from_host && (ss[0] & kF0AllowAudioControl))) {
        if (!(ss[0] & kF0AllowAudioControl)) ss[kAudioControl] = h.audio_control;
        ss[0] |= kF0AllowAudioControl;
        ss[kAudioControl] = (uint8_t) ((ss[kAudioControl] & ~0x03) | (o.mic_select & 0x03));
    }
    // Echo cancellation on (1.17, the default) goes into every report that
    // sets AudioControl: a game's, the connect-time state, and Linux's
    // hid-playstation, which sends AudioControl 0 or 0x30 (the output path
    // only) on each headset plug or unplug and would turn it off. The
    // controller keeps the byte between reports, so others need nothing.
    if (o.echo_cancel && (ss[0] & kF0AllowAudioControl)) ss[kAudioControl] |= kAudioEchoCancel;
    // NoiseCancelEnable stays on in every report that sets AudioControl, with
    // no setting: Linux's plug/unplug report (0 / 0x30) would clear it and
    // halve the speaker until the controller reconnects. A game's explicit
    // off can't be told from the driver's zero, and no known game sends one:
    // SDL's DualSense driver never sets AllowAudioControl, and upstream
    // b821623 forces the bit wherever the board sets AudioControl, a game's
    // report with a mic choice included. Off has no use a player would want.
    if (ss[0] & kF0AllowAudioControl) ss[kAudioControl] |= kAudioNoiseCancel;
    // A fixed mic level replaces the level in any report that sets one (a
    // game's, or the connect-time state); the controller keeps it otherwise.
    if (o.mic_level && (ss[0] & kF0AllowMicVolume)) ss[kVolumeMic] = o.mic_level;
    if (from_host && o.lock_volume) {
        ss[0] &= (uint8_t) ~(kF0AllowHeadphoneVolume | kF0AllowSpeakerVolume | kF0AllowMicVolume);
        ss[1] &= (uint8_t) ~(kF1AllowAudioMute | kF1AllowMuteLight);
    }
}

// A report of only the sections whose setting changed, to apply it now
// instead of at the next game report. A setting back at 0 ("the game's own")
// puts back what the game last set (or the controller's default).
// mic: the mic choice or the echo cancellation (1.17) changed; both live in
// AudioControl, so either sends the whole byte with both applied (and
// NoiseCancelEnable, which the game's last byte may lack).
// level: the mic level changed (1.16); back at 0 the controller gets pc_mic,
// the PC's mic volume as VolumeMic (state_set_mic_volume), again.
inline void build_override_push(uint8_t *ss, const StateOverrides &o, const HostAudioBytes &h, bool trigger, bool gain, bool mic,
                                bool level = false, uint8_t pc_mic = kMicLevelMax) {
    using namespace state_bits;
    if (trigger) {
        ss[1] |= kF1AllowMotorPowerLevel;
        ss[kMotorPower] = o.trigger_reduce ? (uint8_t) ((h.motor_power & 0x0F) | (o.trigger_reduce << 4)) : h.motor_power;
    }
    if (gain) {
        ss[1] |= kF1AllowAudioControl2;
        ss[kAudioControl2] = o.speaker_gain ? (uint8_t) ((h.audio_control2 & ~0x07) | o.speaker_gain) : h.audio_control2;
    }
    if (mic) {
        ss[0] |= kF0AllowAudioControl;
        ss[kAudioControl] = o.mic_select ? (uint8_t) ((h.audio_control & ~0x03) | (o.mic_select & 0x03)) : h.audio_control;
        if (o.echo_cancel) ss[kAudioControl] |= kAudioEchoCancel;
        ss[kAudioControl] |= kAudioNoiseCancel;  // as in stamp_overrides
    }
    if (level) {
        ss[0] |= kF0AllowMicVolume;
        ss[kVolumeMic] = o.mic_level ? o.mic_level : pc_mic;
    }
}

#endif // DS5_BRIDGE_STATE_OVERRIDE_H
