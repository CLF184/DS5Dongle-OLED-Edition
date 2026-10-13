// src/audio_levels.h: the Audio Auto Haptics native-haptics detector (PR #18).
#include "audio_levels.h"
#include "test.h"

TEST(native_haptics_starts_silent_and_wakes_on_one_loud_chunk) {
    NativeHapticsPresence d;
    CHECK(d.silent());
    d.record(NativeHapticsPresence::kThreshold);  // at the threshold: still silent
    CHECK(d.silent());
    d.record(NativeHapticsPresence::kThreshold + 1);
    CHECK(!d.silent());
}

TEST(native_haptics_goes_silent_after_the_timeout_of_quiet_chunks) {
    NativeHapticsPresence d;
    d.record(30000);
    for (int i = 0; i < NativeHapticsPresence::kTimeout - 1; i++) d.record(0);
    CHECK(!d.silent());
    d.record(0);
    CHECK(d.silent());
    // Saturates: a long silence doesn't delay the next detection.
    for (int i = 0; i < 100000; i++) d.record(0);
    d.record(1000);
    CHECK(!d.silent());
}

TEST(native_haptics_full_scale_negative_counts_as_loud) {
    // |-32768| as audio_loop computes it: (uint16_t)32768.
    NativeHapticsPresence d;
    const int16_t s = -32768;
    d.record((uint16_t)(s < 0 ? -s : s));
    CHECK(!d.silent());
}
