// The OLED Slots screen's footer line (oled.cpp render_screen_slots). With
// every pairing slot taken and no controller connected, the dongle is not
// discoverable and only accepts the current slot's controller (bt.cpp
// update_discoverable / inquiry filter), so a new controller doing Create + PS
// just blinks until it gives up: the footer says why instead of "Press PS".
// Pure, no Pico SDK, for the host tests (tests/host).
#ifndef DS5_BRIDGE_SLOTS_FOOTER_H
#define DS5_BRIDGE_SLOTS_FOOTER_H

namespace slots_footer {

// The display fits 20 characters after kContentX (6 + 20 * 6 - 1 = 125 < 128).
constexpr int kMaxChars = 20;
constexpr const char kHelp[] = "Tri=sw Sq hold=wipe";      // controller connected
constexpr const char kConnect[] = "Press PS to connect";   // none connected, a slot free
constexpr const char kFull[] = "All full: forget one";     // none connected, all slots taken
static_assert(sizeof(kHelp) - 1 <= kMaxChars, "Slots footer too wide");
static_assert(sizeof(kConnect) - 1 <= kMaxChars, "Slots footer too wide");
static_assert(sizeof(kFull) - 1 <= kMaxChars, "Slots footer too wide");

// `status`: a live "Switched!" / "Wiped!" message, or nullptr. With a
// controller connected the help line stays: Square-hold is how to forget one.
inline const char *pick(const char *status, bool connected, bool all_full) {
    if (status) return status;
    if (connected) return kHelp;
    return all_full ? kFull : kConnect;
}

}  // namespace slots_footer

#endif  // DS5_BRIDGE_SLOTS_FOOTER_H
