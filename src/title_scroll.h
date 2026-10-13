// The ping-pong scroll of a title longer than its space (oled.cpp
// draw_title_scrolling): one pixel per frame, 1.5 s pauses at each end. Kept
// free of the Pico SDK for the host tests (tests/host).
#ifndef DS5_BRIDGE_TITLE_SCROLL_H
#define DS5_BRIDGE_TITLE_SCROLL_H

#include <stdint.h>

struct TitleScroll {
    int offset = 0, dir = 1;
    uint64_t next_us = 0;  // 64-bit: a 32-bit time froze the scroll for ~36 min after 35 min away

    // The offset to draw at now, for a title `overflow` pixels too wide.
    int step(uint64_t now_us, int overflow) {
        if (now_us >= next_us) {
            offset += dir;
            uint64_t wait = 0;  // the next frame (one pixel per frame)
            if (offset >= overflow || offset <= 0) {
                offset = offset >= overflow ? overflow : 0;
                dir = -dir;
                wait = 1500000;
            }
            next_us = now_us + wait;
        }
        return offset;
    }
};

#endif  // DS5_BRIDGE_TITLE_SCROLL_H
