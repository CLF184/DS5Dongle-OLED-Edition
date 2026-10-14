// src/amp_volume.h: the PC's speaker volume as the controller's amp volumes.
// 0 dB on the PC is full on both amps: 0x64 on the speaker, 0x7F on the
// headphones (it was 100/127 before).
#include "amp_volume.h"
#include "test.h"

TEST(amp_volume_full_scale_reaches_each_amp_top) {
    CHECK_EQ(speaker_amp_from_db(0.0f), 0x64);
    CHECK_EQ(headphone_amp_from_db(0.0f), 0x7F);
    CHECK_EQ(speaker_amp_from_db(6.0f), 0x64);  // above 0 dB clamps
    CHECK_EQ(headphone_amp_from_db(6.0f), 0x7F);
}

TEST(amp_volume_silent_and_between) {
    CHECK_EQ(speaker_amp_from_db(-100.0f), 0);
    CHECK_EQ(headphone_amp_from_db(-100.0f), 0);
    CHECK_EQ(speaker_amp_from_db(-120.0f), 0);
    CHECK_EQ(speaker_amp_from_db(-20.0f), 80);   // the speaker mapping is unchanged
    CHECK_EQ(headphone_amp_from_db(-20.0f), 102); // 80 / 100 of 127
    CHECK_EQ(headphone_amp_from_db(-50.0f), 64);
    CHECK_EQ(speaker_amp_from_db(-0.5f), 99);    // truncates like the old cast
}

TEST(amp_volume_headphones_never_below_the_speaker_number) {
    for (int db = -100; db <= 0; db++) {
        const uint8_t s = speaker_amp_from_db((float) db), h = headphone_amp_from_db((float) db);
        CHECK(h >= s);
        CHECK(h <= 0x7F);
    }
}
