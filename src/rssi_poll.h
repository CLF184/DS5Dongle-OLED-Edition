// One shared poll budget for the OLED and host diagnostics.
#pragma once
#include <cstdint>

struct RssiPoll {
    bool pending = false, have_request = false;
    uint64_t last_us = 0;

    bool ready(uint64_t now_us) {
        if (pending && now_us - last_us >= 1000000u) pending = false;
        return !pending && (!have_request || now_us - last_us >= 100000u);
    }
    void sent(uint64_t now_us) { pending = have_request = true; last_us = now_us; }
    void replied() { pending = false; }
    void reset() { *this = {}; }
};
