// src/state_override.h: the protocol 1.13 settings on the controller state.
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
