// src/title_scroll.h: the Status title's ping-pong scroll, incl. after a long
// time away (F16: a 32-bit time froze it for ~36 min after 35 min away).
#include "title_scroll.h"
#include "test.h"

TEST(title_scroll_ping_pong_with_pauses) {
    TitleScroll t;
    uint64_t now = 1000;
    CHECK_EQ(t.step(now, 3), 1);
    CHECK_EQ(t.step(now += 100000, 3), 2);
    CHECK_EQ(t.step(now += 100000, 3), 3);  // the end: 1.5 s pause
    CHECK_EQ(t.step(now += 100000, 3), 3);
    CHECK_EQ(t.step(now += 1500000, 3), 2);  // and back
    CHECK_EQ(t.step(now += 100000, 3), 1);
    CHECK_EQ(t.step(now += 100000, 3), 0);
    CHECK_EQ(t.step(now += 100000, 3), 0);
    CHECK_EQ(t.step(now += 1500000, 3), 1);
}

TEST(title_scroll_moves_on_after_a_long_time_away) {
    TitleScroll t;
    uint64_t now = 5'000'000;
    t.step(now, 10);
    for (uint64_t away : {35ull * 60'000'000, 40ull * 60'000'000, 72ull * 60'000'000}) {
        now += away;  // the Status screen was away, no step() calls
        const int before = t.offset;
        CHECK(t.step(now, 10) != before);
    }
}
