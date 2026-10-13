// src/rssi_poll.h: the RSSI refresh budget (<= 10 Hz, one request in flight).
#include "rssi_poll.h"
#include "test.h"

TEST(rssi_poll_first_read_asks_at_once) {
    RssiPoll p;
    CHECK(p.ready(0));
    CHECK(p.ready(123456));
}

TEST(rssi_poll_one_request_in_flight) {
    RssiPoll p;
    p.sent(1'000'000);
    CHECK(!p.ready(1'000'000 + 500'000));  // no reply yet: wait, even past 100 ms
    p.replied();
    CHECK(p.ready(1'000'000 + 500'000));
}

TEST(rssi_poll_at_most_10_hz) {
    RssiPoll p;
    uint64_t now = 2'000'000;
    int sent = 0;
    for (int i = 0; i < 1000; i++, now += 1000) {  // a reader every 1 ms for 1 s
        if (p.ready(now)) { p.sent(now); sent++; }
        p.replied();                               // replies arrive at once
    }
    CHECK_EQ(sent, 10);
}

TEST(rssi_poll_lost_reply_frees_the_slot_after_1_s) {
    RssiPoll p;
    p.sent(5'000'000);
    CHECK(!p.ready(5'000'000 + 999'999));
    CHECK(p.ready(5'000'000 + 1'000'000));
}

TEST(rssi_poll_reset_on_link_change) {
    RssiPoll p;
    p.sent(7'000'000);
    p.reset();
    CHECK(p.ready(7'000'001));
}
