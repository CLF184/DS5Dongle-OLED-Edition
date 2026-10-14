// src/state_override.h: the protocol 1.13 settings (and 1.16's mic level, 1.17's echo cancellation) on the controller state.
// With every setting at "the game's own" a game report passes untouched (the
// upstream #228 regression was the dongle changing the game's trigger state).
#include <cstring>
#include "state_override.h"
#include "test.h"

namespace {
using namespace state_bits;
constexpr StateOverrides kNone{0, 0, 0, 0};
void game_report(uint8_t ss[47]) {
    for (int i = 0; i < 47; i++) ss[i] = (uint8_t) (i * 37 + 11);
}
}  // namespace

TEST(override_none_leaves_a_game_report_alone) {
    uint8_t ss[47], before[47];
    game_report(ss);
    std::memcpy(before, ss, sizeof(ss));
    stamp_overrides(ss, kNone, kHostAudioDefaults, true);
    CHECK(std::memcmp(ss, before, sizeof(ss)) == 0);
}

TEST(override_trigger_reduction_keeps_the_game_rumble_nibble) {
    // The game set MotorPowerLevel in this report: rumble 3, trigger 1.
    uint8_t ss[47]{};
    ss[1] = kF1AllowMotorPowerLevel;
    ss[kMotorPower] = 0x13;
    stamp_overrides(ss, {6, 0, 0, 0}, kHostAudioDefaults, true);
    CHECK_EQ(ss[kMotorPower], 0x63);
    // A report without the section: the game's last one is the base.
    uint8_t next[47]{};
    HostAudioBytes h = kHostAudioDefaults;
    record_host_audio(h, ss);
    stamp_overrides(next, {6, 0, 0, 0}, h, true);
    CHECK(next[1] & kF1AllowMotorPowerLevel);
    CHECK_EQ(next[kMotorPower], 0x63);
    // Trigger effects themselves are the game's (no FFB bytes touched).
    for (int i = 10; i < 32; i++) CHECK_EQ(next[i], 0);
}

TEST(override_speaker_gain_and_mic_keep_the_other_bits) {
    uint8_t ss[47]{};
    HostAudioBytes h = {0x08 | 0x01, 0, 0x08};  // game: NoiseCancel + built-in mic; beamforming bit
    stamp_overrides(ss, {0, 5, 0, 2}, h, true);
    CHECK(ss[1] & kF1AllowAudioControl2);
    CHECK_EQ(ss[kAudioControl2], 0x0D);
    CHECK(ss[0] & kF0AllowAudioControl);
    CHECK_EQ(ss[kAudioControl], 0x0A);  // NoiseCancelEnable stays (upstream b821623)
}

TEST(override_mic_auto_passes_the_game_choice_but_sets_ours_at_connect) {
    uint8_t ss[47]{};
    ss[0] = kF0AllowAudioControl;
    ss[kAudioControl] = 0x09;
    stamp_overrides(ss, kNone, kHostAudioDefaults, true);
    CHECK_EQ(ss[kAudioControl], 0x09);  // the game's report: its choice
    stamp_overrides(ss, kNone, kHostAudioDefaults, false);
    CHECK_EQ(ss[kAudioControl], 0x08);  // the dongle's connect state: auto
}

TEST(override_volume_lock_drops_only_the_game_volume_bits) {
    uint8_t ss[47];
    game_report(ss);
    ss[0] = 0xFF;
    ss[1] = 0xFF;
    uint8_t before[47];
    std::memcpy(before, ss, sizeof(ss));
    stamp_overrides(ss, {0, 0, 1, 0}, kHostAudioDefaults, true);
    CHECK_EQ(ss[0], 0x8F);  // headphone, speaker, mic volume cleared; triggers stay
    CHECK_EQ(ss[1], 0xFC);  // mute light, audio mute cleared
    CHECK(std::memcmp(ss + 2, before + 2, sizeof(ss) - 2) == 0);
    // The dongle's own reports keep theirs.
    std::memcpy(ss, before, sizeof(ss));
    stamp_overrides(ss, {0, 0, 1, 0}, kHostAudioDefaults, false);
    CHECK_EQ(ss[0], 0xFF);
    CHECK_EQ(ss[1], 0xFF);
}

TEST(override_push_puts_back_the_game_values) {
    const HostAudioBytes h = {0x09, 0x24, 0x03};
    uint8_t ss[47]{};
    build_override_push(ss, {7, 0, 0, 0}, h, true, false, false);
    CHECK_EQ(ss[0], 0);
    CHECK_EQ(ss[1], kF1AllowMotorPowerLevel);
    CHECK_EQ(ss[kMotorPower], 0x74);
    std::memset(ss, 0, sizeof(ss));
    build_override_push(ss, kNone, h, true, true, true);  // all back to the game's own
    CHECK_EQ(ss[kMotorPower], 0x24);
    CHECK_EQ(ss[kAudioControl2], 0x03);
    CHECK_EQ(ss[kAudioControl], 0x09);
    CHECK_EQ(ss[1], kF1AllowMotorPowerLevel | kF1AllowAudioControl2);
    CHECK_EQ(ss[0], kF0AllowAudioControl);
}

// Protocol 1.16: a fixed mic level replaces the level in a report that sets
// one and adds nothing to one that doesn't; at 0 the report keeps its own.
TEST(override_mic_level_replaces_only_a_set_level) {
    uint8_t ss[47];
    game_report(ss);
    ss[0] = kF0AllowMicVolume;
    uint8_t before[47];
    std::memcpy(before, ss, sizeof(ss));
    stamp_overrides(ss, {0, 0, 0, 0, 24}, kHostAudioDefaults, true);
    CHECK_EQ(ss[kVolumeMic], 24);
    CHECK_EQ(ss[0], kF0AllowMicVolume);
    before[kVolumeMic] = 24;
    CHECK(std::memcmp(ss, before, sizeof(ss)) == 0);  // nothing else changed
    // A report without the mic volume: untouched.
    game_report(ss);
    ss[0] = 0;
    std::memcpy(before, ss, sizeof(ss));
    stamp_overrides(ss, {0, 0, 0, 0, 24}, kHostAudioDefaults, true);
    CHECK(std::memcmp(ss, before, sizeof(ss)) == 0);
    // The connect-time state (its allow bit set) carries the level too.
    ss[0] = kF0AllowMicVolume;
    ss[kVolumeMic] = kMicLevelMax;
    stamp_overrides(ss, {0, 0, 0, 0, 9}, kHostAudioDefaults, false);
    CHECK_EQ(ss[kVolumeMic], 9);
    // At 0 the game's level passes.
    ss[kVolumeMic] = 33;
    stamp_overrides(ss, kNone, kHostAudioDefaults, true);
    CHECK_EQ(ss[kVolumeMic], 33);
}

TEST(override_mic_level_with_volume_lock_still_drops_the_game_bit) {
    uint8_t ss[47]{};
    ss[0] = kF0AllowMicVolume;
    ss[kVolumeMic] = 5;
    stamp_overrides(ss, {0, 0, 1, 0, 24}, kHostAudioDefaults, true);
    CHECK_EQ(ss[0] & kF0AllowMicVolume, 0);  // the lock wins: the controller keeps our pushed level
}

TEST(override_push_mic_level_and_back_to_the_pc) {
    uint8_t ss[47]{};
    build_override_push(ss, {0, 0, 0, 0, 48}, kHostAudioDefaults, false, false, false, true, 30);
    CHECK_EQ(ss[0], kF0AllowMicVolume);
    CHECK_EQ(ss[1], 0);
    CHECK_EQ(ss[kVolumeMic], 48);
    std::memset(ss, 0, sizeof(ss));
    build_override_push(ss, kNone, kHostAudioDefaults, false, false, false, true, 30);  // Auto: the PC's level
    CHECK_EQ(ss[0], kF0AllowMicVolume);
    CHECK_EQ(ss[kVolumeMic], 30);
    // Not changed: no mic bytes.
    std::memset(ss, 0, sizeof(ss));
    build_override_push(ss, {0, 0, 0, 0, 48}, kHostAudioDefaults, false, false, false);
    CHECK_EQ(ss[0], 0);
    CHECK_EQ(ss[kVolumeMic], 0);
}

// Protocol 1.17: echo cancellation on sets EchoCancelEnable in any report that
// sets AudioControl, keeping the rest of the byte; a report without the
// section is untouched; off leaves the game's own bit. (NoiseCancelEnable,
// 0x08, is set in every such report whatever the setting.)
TEST(override_echo_cancel_on_sets_only_the_bit_in_reports_with_audio_control) {
    constexpr StateOverrides kAec{0, 0, 0, 0, 0, 1};
    // Linux hid-playstation on a headset unplug: AudioControl 0x30 (speaker path).
    uint8_t ss[47]{};
    ss[0] = kF0AllowAudioControl;
    ss[kAudioControl] = 0x30;
    stamp_overrides(ss, kAec, kHostAudioDefaults, true);
    CHECK_EQ(ss[kAudioControl], 0x3C);  // NoiseCancelEnable too (always)
    CHECK_EQ(ss[0], kF0AllowAudioControl);
    // A game report without AudioControl: byte for byte the same.
    uint8_t before[47];
    game_report(ss);
    ss[0] = 0x0C;
    std::memcpy(before, ss, sizeof(ss));
    stamp_overrides(ss, kAec, kHostAudioDefaults, true);
    CHECK(std::memcmp(ss, before, sizeof(ss)) == 0);
    // The connect-time state: auto mic, NoiseCancel kept, echo cancellation on.
    std::memset(ss, 0, sizeof(ss));
    ss[0] = 0xFD;
    ss[kAudioControl] = 0x09;
    stamp_overrides(ss, kAec, kHostAudioDefaults, false);
    CHECK_EQ(ss[kAudioControl], 0x0C);
    // With a mic choice on a game report that lacks the section.
    std::memset(ss, 0, sizeof(ss));
    stamp_overrides(ss, {0, 0, 0, 2, 0, 1}, kHostAudioDefaults, true);
    CHECK(ss[0] & kF0AllowAudioControl);
    CHECK_EQ(ss[kAudioControl], 0x0E);
    // Off: a game that turns it on keeps it; one that doesn't stays off.
    std::memset(ss, 0, sizeof(ss));
    ss[0] = kF0AllowAudioControl;
    ss[kAudioControl] = 0x0C;
    stamp_overrides(ss, kNone, kHostAudioDefaults, true);
    CHECK_EQ(ss[kAudioControl], 0x0C);
    ss[kAudioControl] = 0x08;
    stamp_overrides(ss, kNone, kHostAudioDefaults, false);
    CHECK_EQ(ss[kAudioControl], 0x08);
}

TEST(override_push_echo_cancel_on_and_back_to_the_game) {
    const HostAudioBytes h = {0x31, 0, 0};  // the game: speaker path, built-in mic, no NoiseCancel
    uint8_t ss[47]{};
    build_override_push(ss, {0, 0, 0, 0, 0, 1}, h, false, false, true);
    CHECK_EQ(ss[0], kF0AllowAudioControl);
    CHECK_EQ(ss[kAudioControl], 0x3D);  // NoiseCancelEnable put back
    std::memset(ss, 0, sizeof(ss));
    build_override_push(ss, {0, 0, 0, 2, 0, 1}, h, false, false, true);  // with a mic choice
    CHECK_EQ(ss[kAudioControl], 0x3E);
    std::memset(ss, 0, sizeof(ss));
    build_override_push(ss, kNone, h, false, false, true);  // off: the game's byte, NoiseCancel on
    CHECK_EQ(ss[kAudioControl], 0x39);
}

// NoiseCancelEnable (0x08) stays on in every report that sets AudioControl:
// without it the speaker plays at about half level (upstream b821623, #263).
// Linux hid-playstation sends AudioControl 0 on a headset plug and 0x30 on an
// unplug; neither may clear it. A game report that carries it passes as it is,
// one without AudioControl is untouched, and the board's own reports keep it.
TEST(override_noise_cancel_kept_through_headset_plug_and_unplug) {
    constexpr StateOverrides kAec{0, 0, 0, 0, 0, 1};
    for (const StateOverrides &o : {kNone, kAec}) {
        const uint8_t aec = o.echo_cancel ? kAudioEchoCancel : 0;
        // Plug: hid-playstation's 0x00 (headphone path).
        uint8_t ss[47]{};
        ss[0] = kF0AllowAudioControl;
        ss[kAudioControl] = 0x00;
        stamp_overrides(ss, o, kHostAudioDefaults, true);
        CHECK_EQ(ss[kAudioControl], 0x08 | aec);
        CHECK_EQ(ss[0], kF0AllowAudioControl);
        // Unplug: 0x30 (speaker path), with its speaker volume and pre-gain.
        std::memset(ss, 0, sizeof(ss));
        ss[0] = kF0AllowAudioControl | kF0AllowSpeakerVolume;
        ss[1] = kF1AllowAudioControl2;
        ss[5] = 0x64;
        ss[kAudioControl] = 0x30;
        ss[kAudioControl2] = 0x20;
        stamp_overrides(ss, o, kHostAudioDefaults, true);
        CHECK_EQ(ss[kAudioControl], 0x38 | aec);
        CHECK_EQ(ss[5], 0x64);
        CHECK_EQ(ss[kAudioControl2], 0x20);
        CHECK_EQ(ss[0], kF0AllowAudioControl | kF0AllowSpeakerVolume);
        CHECK_EQ(ss[1], kF1AllowAudioControl2);
        // A game report with NoiseCancel on: byte for byte the same (AEC off).
        uint8_t before[47];
        std::memset(ss, 0, sizeof(ss));
        ss[0] = kF0AllowAudioControl;
        ss[kAudioControl] = 0x39;
        std::memcpy(before, ss, sizeof(ss));
        stamp_overrides(ss, o, kHostAudioDefaults, true);
        if (!o.echo_cancel) CHECK(std::memcmp(ss, before, sizeof(ss)) == 0);
        CHECK_EQ(ss[kAudioControl], 0x39 | aec);
        // A game report without AudioControl: untouched, no section added.
        game_report(ss);
        ss[0] = 0x0C;
        std::memcpy(before, ss, sizeof(ss));
        stamp_overrides(ss, o, kHostAudioDefaults, true);
        CHECK(std::memcmp(ss, before, sizeof(ss)) == 0);
        // The connect-time state (0x09 in state_init_data): as before.
        std::memset(ss, 0, sizeof(ss));
        ss[0] = 0xFD;
        ss[kAudioControl] = 0x09;
        stamp_overrides(ss, o, kHostAudioDefaults, false);
        CHECK_EQ(ss[kAudioControl], 0x08 | aec);
    }
    // The board's pushes: one without the mic section adds no AudioControl;
    // the mic push after an unplug (the last byte 0x30) keeps NoiseCancel.
    uint8_t ss[47]{};
    build_override_push(ss, {0, 3, 0, 0}, {0x30, 0, 0}, false, true, false);
    CHECK_EQ(ss[0], 0);
    CHECK_EQ(ss[kAudioControl], 0);
    std::memset(ss, 0, sizeof(ss));
    build_override_push(ss, {0, 0, 0, 1}, {0x30, 0, 0}, false, false, true);
    CHECK_EQ(ss[kAudioControl], 0x39);
}
