// The controller glyph on the OLED Status screen (below the header, BT address
// and battery rows): two stick boxes, the L2/R2 bars beside them, L1/R1 and
// Create/Options at the top between the sticks, the d-pad and the face buttons.
// Pressed = filled, released = outline; L3/R3 invert their stick box.
//
// Laid out symmetric around the screen's vertical center line (x <-> 127 - x):
// every right-hand part is the left-hand one mirrored, and the d-pad / face
// buttons (squares vs. rounded) share mirrored centers and bounding boxes.
// Pure drawing on a canvas, no SDK, so the host tests render it
// (tests/host/test_oled_pad.cpp checks the mirror symmetry).
#pragma once
#include <cstdint>

namespace oled_pad {

constexpr int kW = 128;
constexpr int kTop = 30;                 // first row of the glyph (rows above: header)
constexpr int kLeft = 6;                 // first column (x 0..5 = button chrome)
constexpr int kRight = kW - 1 - kLeft;   // last column, mirrored from kLeft
constexpr int mirror_x(int x) { return kW - 1 - x; }
// The left-hand x of a w-wide part mirrored from one starting at x.
constexpr int mirror_left(int x, int w) { return kW - x - w; }

constexpr int kBox = 31;                 // stick box, odd so the dot can center
constexpr int kStickTravel = 24;         // dot center 3..27 px into the box (15 = center),
                                         // so a full tilt still leaves a gap to the frame
constexpr int kTrigX = kLeft + kBox + 2; // L2 bar, a gap right of the box
constexpr int kTrigW = 4;
constexpr int kTrigFill = kBox - 2;      // 29 rows of travel inside the outline
constexpr int kShoulderX = kTrigX + kTrigW + 2;  // L1 bar, over the d-pad
constexpr int kShoulderW = 12;
constexpr int kShoulderH = 3;
constexpr int kSmallX = kShoulderX + kShoulderW + 2;  // Create, next to L1
constexpr int kSmallW = 3;
constexpr int kSmallH = 5;
constexpr int kBtn = 5;                  // d-pad / face button size
constexpr int kBtnStep = 6;              // center to arm center
constexpr int kDpadCx = kShoulderX + kBtnStep + kBtn / 2;  // left edge = L1's
constexpr int kClusterCy = kTop + 17;  // centered between Create's bottom and the box's

static_assert(kSmallX + kSmallW <= mirror_left(kSmallX, kSmallW) - 2,
              "Create and Options must not touch");
static_assert(kDpadCx + kBtnStep + kBtn / 2 < mirror_x(kDpadCx) - kBtnStep - kBtn / 2,
              "the d-pad and face buttons must not touch");

// Stick axis 0..255 -> dot offset 0..kStickTravel, rounded, so v and 255 - v
// land on mirrored pixels (floor((24v+127)/255) == 24 - floor((24(255-v)+127)/255));
// 127 and 128 (rest) both give 12, the box's center.
constexpr int stick_offset(uint8_t v) { return (v * kStickTravel + 127) / 255; }
constexpr int trigger_fill(uint8_t v) { return (v * kTrigFill + 127) / 255; }

template <class Canvas>
void button(Canvas &c, int x, int y, int w, int h, bool on, bool round) {
    if (on) c.filled(x, y, w, h);
    else    c.outline(x, y, w, h);
    if (round) {
        c.px(x, y, false);
        c.px(x + w - 1, y, false);
        c.px(x, y + h - 1, false);
        c.px(x + w - 1, y + h - 1, false);
    }
}

template <class Canvas>
void stick(Canvas &c, int x, uint8_t sx, uint8_t sy, bool click) {
    c.outline(x, kTop, kBox, kBox);
    const int cx = x + 3 + stick_offset(sx);
    const int cy = kTop + 3 + stick_offset(sy);
    c.filled(cx - 1, cy - 1, 3, 3);
    if (click) c.invert(x, kTop, kBox, kBox);
}

template <class Canvas>
void trigger(Canvas &c, int x, uint8_t v) {
    c.outline(x, kTop, kTrigW, kBox);
    const int n = trigger_fill(v);
    if (n > 0) c.filled(x + 1, kTop + 1 + kTrigFill - n, kTrigW - 2, n);
}

// `in` = the controller's input report without the report id
// (interrupt_in_data): [0..3] sticks, [4,5] L2/R2, [7] d-pad + face,
// [8] L1 R1 L2 R2 Create Options L3 R3 (bits 0..7).
template <class Canvas>
void draw(Canvas &c, const uint8_t *in) {
    const uint8_t b7 = in[7];
    const uint8_t b8 = in[8];

    stick(c, kLeft, in[0], in[1], b8 & 0x40);
    stick(c, mirror_left(kLeft, kBox), in[2], in[3], b8 & 0x80);
    trigger(c, kTrigX, in[4]);
    trigger(c, mirror_left(kTrigX, kTrigW), in[5]);

    button(c, kShoulderX, kTop, kShoulderW, kShoulderH, b8 & 0x01, false);  // L1
    button(c, mirror_left(kShoulderX, kShoulderW), kTop, kShoulderW, kShoulderH,
           b8 & 0x02, false);                                                // R1
    button(c, kSmallX, kTop, kSmallW, kSmallH, b8 & 0x10, false);           // Create
    button(c, mirror_left(kSmallX, kSmallW), kTop, kSmallW, kSmallH,
           b8 & 0x20, false);                                                // Options

    // D-pad: hat value 0..7 clockwise from N, 8 = released; diagonals light both.
    const int dp = b7 & 0x0F;
    const bool n = dp == 7 || dp == 0 || dp == 1;
    const bool e = dp == 1 || dp == 2 || dp == 3;
    const bool s = dp == 3 || dp == 4 || dp == 5;
    const bool w = dp == 5 || dp == 6 || dp == 7;
    const int h = kBtn / 2;
    const int dx = kDpadCx - h, dy = kClusterCy - h;
    button(c, dx, dy - kBtnStep, kBtn, kBtn, n, false);
    button(c, dx + kBtnStep, dy, kBtn, kBtn, e, false);
    button(c, dx, dy + kBtnStep, kBtn, kBtn, s, false);
    button(c, dx - kBtnStep, dy, kBtn, kBtn, w, false);

    // Face buttons, rounded so they read apart from the d-pad.
    const int fx = mirror_x(kDpadCx) - h;
    button(c, fx, dy - kBtnStep, kBtn, kBtn, b7 & 0x80, true);   // Triangle
    button(c, fx + kBtnStep, dy, kBtn, kBtn, b7 & 0x40, true);   // Circle
    button(c, fx, dy + kBtnStep, kBtn, kBtn, b7 & 0x20, true);   // Cross
    button(c, fx - kBtnStep, dy, kBtn, kBtn, b7 & 0x10, true);   // Square
}

}  // namespace oled_pad
