#include "test.h"
#include "rssi_poll.h"

TEST(rssi_readers_share_one_poll_budget) {
    RssiPoll poll;
    CHECK(poll.ready(0));
    poll.sent(0);
    for (int i = 0; i < 1000; ++i) CHECK(!poll.ready(i));
    poll.replied();
    CHECK(!poll.ready(99999));
    CHECK(poll.ready(100000));
    poll.sent(100000);
    CHECK(!poll.ready(1099999));
    CHECK(poll.ready(1100000));  // lost reply timeout
    poll.sent(1100000);
    poll.reset();
    CHECK(poll.ready(1100001));  // new connection has no old request
}
