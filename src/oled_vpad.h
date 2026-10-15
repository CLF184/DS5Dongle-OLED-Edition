// The app's virtual controller buttons for the OLED screens (0xF6 0x19,
// protocol 1.19). Pure, no SDK, so the host tests run it
// (tests/host/test_oled_vpad.cpp).
//
// The OLED screens read the controller's D-pad, face buttons and R1 from the
// input report (Settings, Slots, Lightbar, Trigger Test, Diagnostics). A
// virtual press is held down for a while and let go, like a thumb, and the
// screens read "real bits OR virtual bits" through view(). The virtual bits
// live only here: interrupt_in_data and the report the PC gets are never
// written, so games never see these presses.
#ifndef DS5_BRIDGE_OLED_VPAD_H
#define DS5_BRIDGE_OLED_VPAD_H

#include <stdint.h>

namespace oledvpad {

// Wire ids (0xF6 0x19 [button, hold ms lo, hold ms hi]).
enum Button : uint8_t {
    kUp = 0, kDown = 1, kLeft = 2, kRight = 3,
    kTriangle = 4, kCircle = 5, kCross = 6, kSquare = 7,
    kR1 = 8,
    kNumButtons = 9,
};

constexpr uint32_t kShortHoldUs = 100000;   // a tap: one render frame (100 ms) sees it
constexpr uint16_t kMaxHoldMs = 5000;       // Settings Reset / Wipe need 2 s, Slots wipe 1.5 s
constexpr uint32_t kGapUs = 60000;          // released between queued presses, so each is an edge
constexpr uint32_t kWakeWaitUs = 1000000;   // longest wait for a dimmed / off screen to come on
constexpr uint8_t kQueue = 4;

// The input report bytes the OLED screens read, as they see them.
struct View {
    uint8_t b7;  // low nibble D-pad hat (0 N .. 7 NW, 8 released), high nibble □ ✕ ○ △ (0x10 .. 0x80)
    uint8_t b8;  // bit 1 = R1 (the only bit of byte 8 the screens use)
};

// Real input (connected = a controller is linked; its bytes are ignored
// otherwise) merged with the virtual button held now (kNumButtons = none).
// A virtual D-pad direction replaces the real hat, face buttons and R1 add.
inline View view(bool connected, uint8_t real7, uint8_t real8, uint8_t held) {
    View v{connected ? real7 : (uint8_t) 0x08, connected ? real8 : (uint8_t) 0};
    static constexpr uint8_t kHat[4] = {0, 4, 6, 2};  // up, down, left, right
    if (held < 4) v.b7 = (uint8_t) ((v.b7 & 0xF0) | kHat[held]);
    else if (held < 8) v.b7 |= (uint8_t) (0x80 >> (held - 4));  // △ 0x80, ○ 0x40, ✕ 0x20, □ 0x10
    else if (held == kR1) v.b8 |= 0x02;
    return v;
}

// Held time for a requested hold in ms (0 = a tap); false when out of range.
inline bool hold_us(uint16_t hold_ms, uint32_t &out) {
    // error: SHOWN — CMD_RESULT_INVALID in the 0xF1 result byte (cmd.cpp refuses a hold over kMaxHoldMs at the SET)
    if (hold_ms > kMaxHoldMs) return false;
    out = hold_ms * 1000u < kShortHoldUs ? kShortHoldUs : hold_ms * 1000u;
    return true;
}

struct Press { uint8_t button; uint32_t hold_us; };

struct Injector {
    Press q[kQueue] = {};
    uint8_t head = 0;
    uint8_t count = 0;
    enum Phase : uint8_t { Idle, Waking, Holding, Gap } phase = Idle;
    Press cur{};
    uint32_t t0_us = 0;
};

// Queues a press; false for a bad button / hold or when kQueue presses wait.
inline bool push(Injector &in, uint8_t button, uint16_t hold_ms) {
    uint32_t us = 0;
    // error: SHOWN — CMD_RESULT_INVALID in the 0xF1 result byte (cmd.cpp checks the same range first)
    if (button >= kNumButtons || !hold_us(hold_ms, us)) return false;
    // error: SHOWN — CMD_RESULT_BUSY in the 0xF1 result byte (cmd.cpp 0x19): four presses already waiting
    if (in.count >= kQueue) return false;
    in.q[(uint8_t) ((in.head + in.count) % kQueue)] = Press{button, us};
    in.count++;
    return true;
}

inline bool busy(const Injector &in) { return in.count != 0 || in.phase != Injector::Idle; }

enum Event : uint8_t {
    kWake = 0x01,      // keep / bring the screen on (a press waits or is held)
};

// One pass, every main-loop pass. screen_on = the panel is on and drawing
// (OLED_ACTIVE). A press that finds the screen dimmed or off asks for a wake
// first and starts once it's on (at most kWakeWaitUs later), so a tap on a
// dark screen does what it says instead of only waking it. held = the
// button down now, kNumButtons for none.
inline uint8_t tick(Injector &in, uint32_t now, bool screen_on, uint8_t &held) {
    held = kNumButtons;
    uint8_t ev = 0;
    if (in.phase == Injector::Gap && (now - in.t0_us) >= kGapUs) in.phase = Injector::Idle;
    if (in.phase == Injector::Idle && in.count != 0) {
        in.cur = in.q[in.head];
        in.head = (uint8_t) ((in.head + 1) % kQueue);
        in.count--;
        in.phase = Injector::Waking;
        in.t0_us = now;
    }
    if (in.phase == Injector::Waking) {
        ev |= kWake;
        if (!screen_on && (now - in.t0_us) < kWakeWaitUs) return ev;
        in.phase = Injector::Holding;
        in.t0_us = now;
    }
    if (in.phase == Injector::Holding) {
        ev |= kWake;
        if ((now - in.t0_us) >= in.cur.hold_us) {
            in.phase = Injector::Gap;  // released on this pass
            in.t0_us = now;
            return ev;
        }
        held = in.cur.button;
    }
    return ev;
}

}  // namespace oledvpad

#endif  // DS5_BRIDGE_OLED_VPAD_H
