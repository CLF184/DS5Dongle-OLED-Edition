#include "test.h"
#include "audio_levels.h"

TEST(audio_fallback_does_not_depend_on_meter_readers) {
    for (int readers : {0, 1, 30}) {
        NativeHapticsPresence native;
        AudioPeak meter;
        native.record(32768);
        meter.record(32768, 0);
        for (int chunk = 1; chunk <= 100; ++chunk) {
            for (int i = 0; i < readers; ++i) (void) meter.read(chunk * 1000);
            native.record(0);
            CHECK_EQ(native.silent(), chunk >= 100);
        }
        native.record(257);
        CHECK(!native.silent());
        for (int chunk = 0; chunk < 100; ++chunk) native.record(256);
        CHECK(native.silent());
    }
}

TEST(audio_peaks_expire_without_reading_and_reads_are_const) {
    AudioPeak peak;
    peak.record(32768, 100);
    CHECK_EQ(peak.read(100), 255);  // -32768 magnitude must not wrap to zero
    CHECK_EQ(peak.read(100 + 128 * 1024), 128);
    for (int i = 0; i < 100; ++i) CHECK_EQ(peak.read(100 + 128 * 1024), 128);
    CHECK_EQ(peak.peak, 32768);
    CHECK_EQ(peak.at_us, 100);
    CHECK_EQ(peak.read(100 + 256 * 1024), 0);
    CHECK_EQ(peak.read((1ull << 32) + 100), 0);  // reopening after a full timer wrap
    peak.record(4096, (1ull << 32) + 100);
    CHECK_EQ(peak.read((1ull << 32) + 100), 32);  // smaller new peaks take over
    peak.record(0, (1ull << 32) + 200);
    CHECK_EQ(peak.at_us, (1ull << 32) + 100);   // silence does not renew old peaks
}
