// The PC's speaker volume (the USB speaker feature unit, -100..0 dB) as the
// controller's two amp volumes (SetStateData VolumeSpeaker / VolumeHeadphones),
// linear in dB as upstream aa5e012: 0 dB = the top of each amp's range. The
// speaker amp's range is 0..0x64 (the PS5 uses up to 0x64), the headphone
// amp's 0..0x7F, so the headphones get the same position scaled to 127; they
// used to get the speaker's number and topped out at 100/127 (79 %). Pure, so
// tests/host checks it.
#ifndef DS5_BRIDGE_AMP_VOLUME_H
#define DS5_BRIDGE_AMP_VOLUME_H

#include <cstdint>

constexpr uint8_t kSpeakerAmpMax = 0x64;
constexpr uint8_t kHeadphoneAmpMax = 0x7F;

inline uint8_t speaker_amp_from_db(float db) {
    const float v = 100.0f + db;
    return v <= 0.0f ? 0 : v >= 100.0f ? kSpeakerAmpMax : (uint8_t) v;
}

inline uint8_t headphone_amp_from_db(float db) {
    return (uint8_t) ((speaker_amp_from_db(db) * kHeadphoneAmpMax + kSpeakerAmpMax / 2) / kSpeakerAmpMax);
}

#endif  // DS5_BRIDGE_AMP_VOLUME_H
