//
// The RSSI refresh budget (bt.cpp): gap_read_rssi() answers asynchronously,
// so bt_get_signal_strength() returns the cached value and asks for a fresh
// one, at most every 100 ms (10 Hz) and with at most one request in flight
// (a lost reply frees the slot after 1 s). Only reads trigger it: the OLED BT
// Signal screen and the app's 0xF9, never a poll during gameplay. Pure, no
// Pico SDK, for the host tests. From CLF184's PR #18.
//

#ifndef DS5_BRIDGE_RSSI_POLL_H
#define DS5_BRIDGE_RSSI_POLL_H

#include <cstdint>

struct RssiPoll {
    static constexpr uint64_t kMinGapUs = 100000;    // 10 Hz
    static constexpr uint64_t kReplyTimeoutUs = 1000000;
    bool pending = false, have_request = false;
    uint64_t last_us = 0;

    // May a request go out now?
    bool ready(uint64_t now_us) {
        if (pending && now_us - last_us >= kReplyTimeoutUs) pending = false;
        return !pending && (!have_request || now_us - last_us >= kMinGapUs);
    }
    void sent(uint64_t now_us) { pending = have_request = true; last_us = now_us; }
    void replied() { pending = false; }
    void reset() { *this = {}; }
};

#endif  // DS5_BRIDGE_RSSI_POLL_H
