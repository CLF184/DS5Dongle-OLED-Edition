// src/speaker_mono.h: Linux's mono Speaker device (USB channel 2 only) is
// copied to both headset ears; stereo stays stereo.
#include "speaker_mono.h"
#include "test.h"

// 1 ms USB reads, as the host sends them.
static bool feed(SpeakerMonoDetect &d, bool left, bool right, uint32_t ms) {
    bool mono = d.mono;
    for (uint32_t i = 0; i < ms; i++) mono = d.update(left, right, 48);
    return mono;
}

TEST(speaker_mono_right_only_turns_on_after_100ms) {
    SpeakerMonoDetect d;
    CHECK(!feed(d, false, true, 99));
    CHECK(feed(d, false, true, 1));
}

TEST(speaker_mono_stereo_never_turns_on) {
    SpeakerMonoDetect d;
    CHECK(!feed(d, true, true, 10000));
    CHECK(!feed(d, true, false, 1000));  // left-only: not the Speaker device
}

TEST(speaker_mono_one_left_sample_ends_it_at_once) {
    SpeakerMonoDetect d;
    CHECK(feed(d, false, true, 200));
    CHECK(!d.update(true, true, 48));
    // and the count starts over
    CHECK(!feed(d, false, true, 99));
    CHECK(feed(d, false, true, 1));
}

TEST(speaker_mono_silence_keeps_the_state) {
    SpeakerMonoDetect d;
    CHECK(feed(d, false, true, 100));
    CHECK(feed(d, false, false, 5000));  // gap between notes / lines
    CHECK(feed(d, false, true, 1));
    // a gap mid-count keeps the count too
    SpeakerMonoDetect e;
    CHECK(!feed(e, false, true, 60));
    CHECK(!feed(e, false, false, 500));
    CHECK(feed(e, false, true, 40));
}

TEST(speaker_mono_hard_right_pan_shorter_than_100ms_stays_stereo) {
    SpeakerMonoDetect d;
    CHECK(!feed(d, false, true, 80));
    CHECK(!feed(d, true, true, 1));
    CHECK(!feed(d, false, true, 80));
}

TEST(speaker_mono_count_saturates_while_on) {
    SpeakerMonoDetect d;
    feed(d, false, true, 100);
    const uint32_t at = d.right_only;
    feed(d, false, true, 1000000);  // hours of right-only audio: no overflow
    CHECK_EQ(d.right_only, at);
    CHECK(d.mono);
}
