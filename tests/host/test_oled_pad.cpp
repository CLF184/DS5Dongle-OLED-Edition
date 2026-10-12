// The Status screen's controller glyph (src/oled_pad.h) must stay mirror-
// symmetric around the screen's vertical center line (x <-> 127 - x), inside
// its area, and light every button it shows (Create/Options included).
#include <cstring>
#include "oled_pad.h"
#include "test.h"

namespace {
constexpr int W = 128, H = 64;

struct Frame {
    bool p[H][W] = {};
    void px(int x, int y, bool on) {
        CHECK(x >= 0 && x < W && y >= 0 && y < H);
        if (x >= 0 && x < W && y >= 0 && y < H) p[y][x] = on;
    }
    void outline(int x, int y, int w, int h) {
        for (int i = 0; i < w; i++) { px(x + i, y, true); px(x + i, y + h - 1, true); }
        for (int i = 0; i < h; i++) { px(x, y + i, true); px(x + w - 1, y + i, true); }
    }
    void filled(int x, int y, int w, int h) {
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) px(x + i, y + j, true);
    }
    void invert(int x, int y, int w, int h) {
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) px(x + i, y + j, !p[y + j][x + i]);
    }
};

struct Report {
    uint8_t d[63];
    Report() {
        std::memset(d, 0, sizeof(d));
        d[0] = d[1] = d[2] = d[3] = 128;
        d[7] = 0x08;  // d-pad released
    }
};

Frame render(const Report &r) {
    Frame f;
    oled_pad::draw(f, r.d);
    return f;
}

// The same controller state seen in a mirror: left and right parts swap,
// horizontal stick axes flip, the d-pad's E/W and Square/Circle swap.
Report mirrored(const Report &r) {
    Report m = r;
    m.d[0] = (uint8_t) (255 - r.d[2]);
    m.d[1] = r.d[3];
    m.d[2] = (uint8_t) (255 - r.d[0]);
    m.d[3] = r.d[1];
    m.d[4] = r.d[5];
    m.d[5] = r.d[4];
    const int hat = r.d[7] & 0x0F;
    uint8_t face = r.d[7] & 0xA0;            // Triangle, Cross stay
    if (r.d[7] & 0x10) face |= 0x40;         // Square <-> Circle
    if (r.d[7] & 0x40) face |= 0x10;
    m.d[7] = (uint8_t) (face | (hat < 8 ? (8 - hat) % 8 : hat));
    uint8_t b8 = 0;
    for (int bit = 0; bit < 8; bit += 2) {   // L1/R1, L2/R2, Create/Options, L3/R3
        if (r.d[8] & (1 << bit)) b8 |= (uint8_t) (2 << bit);
        if (r.d[8] & (2 << bit)) b8 |= (uint8_t) (1 << bit);
    }
    m.d[8] = b8;
    return m;
}

struct Box { int x0 = W, y0 = H, x1 = -1, y1 = -1; bool empty() const { return x1 < 0; } };
bool operator==(const Box &a, const Box &b) {
    return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}
Box mirror(Box b) {
    if (b.empty()) return b;
    return {W - 1 - b.x1, b.y0, W - 1 - b.x0, b.y1};
}

// The d-pad's area (and, mirrored, the face buttons').
constexpr int kZone = oled_pad::kBtnStep + oled_pad::kBtn / 2;
bool in_dpad(int x, int y) {
    return x >= oled_pad::kDpadCx - kZone && x <= oled_pad::kDpadCx + kZone &&
           y >= oled_pad::kClusterCy - kZone && y <= oled_pad::kClusterCy + kZone;
}
bool in_cluster(int x, int y) { return in_dpad(x, y) || in_dpad(W - 1 - x, y); }

// Pixels in `zone` that are on in a (and, with b, differ between a and b).
template <class Zone>
Box bbox(const Frame &a, const Frame *b, Zone zone) {
    Box r;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            if (!zone(x, y)) continue;
            if (b ? a.p[y][x] == b->p[y][x] : !a.p[y][x]) continue;
            if (x < r.x0) r.x0 = x;
            if (x > r.x1) r.x1 = x;
            if (y < r.y0) r.y0 = y;
            if (y > r.y1) r.y1 = y;
        }
    return r;
}

// Everything but the d-pad / face buttons: f must equal the mirror image of g.
int asymmetric_pixels(const Frame &f, const Frame &g) {
    int n = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (!in_cluster(x, y) && f.p[y][x] != g.p[y][W - 1 - x]) n++;
    return n;
}

const auto kDpadZone = [](int x, int y) { return in_dpad(x, y); };
const auto kFaceZone = [](int x, int y) { return in_dpad(W - 1 - x, y); };

Report with(int byte, uint8_t v) { Report r; r.d[byte] = v; return r; }
}  // namespace

TEST(oled_pad_stays_in_its_area) {
    Report all;
    all.d[4] = all.d[5] = 255;
    all.d[7] = 0xF1;
    all.d[8] = 0xFF;
    for (const Report &r : {Report(), all, with(0, 0), with(3, 255)}) {
        const Frame f = render(r);
        const Box b = bbox(f, nullptr, [](int, int) { return true; });
        CHECK(b.x0 >= oled_pad::kLeft);   // clear of the button chrome (x 0..5)
        CHECK(b.y0 >= oled_pad::kTop);    // clear of the header rows
        CHECK(b.x1 <= oled_pad::kRight);
        CHECK(b.y1 < H);
    }
}

TEST(oled_pad_mirror_symmetric) {
    // Idle, each mirrored part pressed, sticks and triggers at their extremes.
    std::vector<Report> states = {Report(), with(8, 0x01), with(8, 0x04), with(8, 0x10),
                                  with(8, 0x40), with(8, 0x30), with(4, 128), with(4, 255),
                                  with(4, 1), with(0, 0), with(0, 255), with(1, 0),
                                  with(1, 255), with(0, 127), with(0, 100)};
    Report corner;
    corner.d[0] = corner.d[1] = 0;
    corner.d[2] = 255;
    corner.d[3] = 0;
    states.push_back(corner);
    for (int v = 0; v < 256; v += 17) states.push_back(with(2, (uint8_t) v));
    for (const Report &r : states) {
        const Frame f = render(r), g = render(mirrored(r));
        CHECK_EQ(asymmetric_pixels(f, g), 0);
        // The two clusters' outlines: same box, mirrored.
        CHECK(bbox(f, nullptr, kDpadZone) == mirror(bbox(f, nullptr, kFaceZone)));
    }
    // The symmetric idle picture: also equal to its own mirror image.
    const Frame idle = render(Report());
    CHECK_EQ(asymmetric_pixels(idle, idle), 0);
}

TEST(oled_pad_dpad_mirrors_face_buttons) {
    // Each d-pad direction lights the spot mirrored from its face button:
    // up <-> Triangle, right <-> Square, down <-> Cross, left <-> Circle.
    const Frame idle = render(Report());
    const struct { uint8_t hat, face; } pairs[] = {{0, 0x80}, {2, 0x10}, {4, 0x20}, {6, 0x40}};
    for (const auto &p : pairs) {
        Report d, f;
        d.d[7] = p.hat;
        f.d[7] = (uint8_t) (p.face | 0x08);
        const Box bd = bbox(render(d), &idle, kDpadZone);
        const Box bf = bbox(render(f), &idle, kFaceZone);
        CHECK(!bd.empty());
        CHECK(!bf.empty());
        CHECK(bd == mirror(bf));
    }
}

TEST(oled_pad_every_button_lights) {
    const Frame idle = render(Report());
    const auto changes = [&](const Report &r) {
        const Frame f = render(r);
        return bbox(f, &idle, [](int, int) { return true; });
    };
    // L1 R1 Create Options L3 R3 (L2/R2's digital bits: the bars show the analog value).
    for (int bit : {0, 1, 4, 5, 6, 7}) CHECK(!changes(with(8, (uint8_t) (1 << bit))).empty());
    for (int hat = 0; hat < 8; hat++) CHECK(!changes(with(7, (uint8_t) hat)).empty());
    for (int bit = 4; bit < 8; bit++) CHECK(!changes(with(7, (uint8_t) (0x08 | 1 << bit))).empty());
    // Create / Options light between L1 and R1, in the top row.
    const Box c = changes(with(8, 0x10)), o = changes(with(8, 0x20));
    CHECK(c.y0 >= oled_pad::kTop && c.y1 < oled_pad::kTop + oled_pad::kSmallH);  // fills its outline
    CHECK(c.x0 > oled_pad::kShoulderX + oled_pad::kShoulderW);
    CHECK(c.x1 < W / 2);
    CHECK(o == mirror(c));
}

TEST(oled_pad_stick_rest_is_centered) {
    // 127 and 128 (a centered stick) both put the dot in the middle of the box.
    for (uint8_t v : {127, 128}) {
        Report r;
        r.d[0] = r.d[1] = v;
        const Frame f = render(r);
        const Box dot = bbox(f, nullptr, [](int x, int y) {
            return x > oled_pad::kLeft && x < oled_pad::kLeft + oled_pad::kBox - 1 &&
                   y > oled_pad::kTop && y < oled_pad::kTop + oled_pad::kBox - 1;
        });
        CHECK_EQ(dot.x0 + dot.x1, 2 * oled_pad::kLeft + oled_pad::kBox - 1);
        CHECK_EQ(dot.y0 + dot.y1, 2 * oled_pad::kTop + oled_pad::kBox - 1);
    }
}
