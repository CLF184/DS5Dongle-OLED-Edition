//
// Firmware side of src/usb_power.h: feeds the state machine TinyUSB's suspend /
// resume / mount callbacks, the controller's state and the time, and carries
// out what it asks for. Main loop only (TinyUSB runs its callbacks from
// tud_task()), so nothing here needs a lock, and it never runs from flash-
// sensitive or interrupt context.
//

#include "usb_power.h"

#include <cstdio>

#include "audio.h"
#include "bsp/board_api.h"
#include "bt.h"
#include "config.h"
#include "pico/time.h"
#include "tusb.h"

#ifndef ENABLE_SERIAL
#define ENABLE_SERIAL 0
#endif

extern bool spk_active;                 // main.cpp
extern uint8_t interrupt_in_data[63];   // main.cpp

bool g_usb_wake_keyboard = false;

namespace {
usbpm::Machine s_pm;
// A Present waits here between the detach and the attach (150 ms, as the 0x03
// reconnect always did), without blocking the main loop.
bool s_attach_pending = false;
usbpm::Mode s_attach_mode = usbpm::Mode::Off;
uint64_t s_attach_at = 0;
constexpr uint64_t kDetachUs = 150000;
constexpr uint8_t kKeyF15 = 0x68;

usbpm::Settings settings() {
    usbpm::Settings s;
    const uint8_t f = config_power_flags();
    s.power_off = (f & POWER_SLEEP_CONTROLLER_OFF) != 0;
#if ENABLE_SERIAL
    s.wake = false;  // the serial build never leaves the bus (CDC), so no wake keyboard
    s.always_on = true;
#else
    s.wake = (f & POWER_PS_WAKES_PC) != 0;
#endif
    return s;
}

// Off the bus: whatever the host had open is gone.
void detach() {
    // error: NOT-WORTH — nothing; tud_connect/disconnect only fail on a port without D+ pull-up control, which the RP2350 has
    tud_disconnect();
    spk_active = false;
    audio_set_mic_active(false);
}

void attach(usbpm::Mode m, uint64_t now) {
    g_usb_wake_keyboard = m == usbpm::Mode::Keyboard;
    // error: NOT-WORTH — nothing; tud_connect/disconnect only fail on a port without D+ pull-up control, which the RP2350 has
    if (m != usbpm::Mode::Off) tud_connect();
    s_pm.presented(m, now);
    printf("[USB PM] presenting %s\n", m == usbpm::Mode::Full ? "the DualSense" : m == usbpm::Mode::Keyboard ? "the wake keyboard" : "nothing");
}

// Detach now and attach as `m` after kDetachUs (straight away when already off the bus).
void present(usbpm::Mode m, uint64_t now) {
    if (s_pm.mode() == usbpm::Mode::Off) {
        attach(m, now);
        return;
    }
    detach();
    s_attach_pending = true;
    s_attach_mode = m;
    s_attach_at = now + kDetachUs;
}

bool send_key(uint8_t key) {
    uint8_t rpt[8] = {0, 0, key, 0, 0, 0, 0, 0};
    // error: NOT-WORTH — nothing; the wake state machine (usb_power.h) only sends when the endpoint is ready (hid_ready) and presses the key kKeyPresses times
    return tud_hid_n_report(0, 0, rpt, sizeof(rpt));
}
}  // namespace

void usb_power_init() {
    s_pm = usbpm::Machine(ENABLE_SERIAL ? usbpm::Mode::Full : usbpm::Mode::Off);
    s_pm.boot(time_us_64());
}

bool usb_power_wake_enabled() {
#if ENABLE_SERIAL
    return false;
#else
    return (config_power_flags() & POWER_PS_WAKES_PC) != 0;
#endif
}

void usb_power_controller(bool ready) {
    s_pm.controller(ready, time_us_64());
}

void usb_power_reconnect() {
    // error: NOT-WORTH — nothing; a reconnect is already under way (0xF6 0x03 asked twice)
    if (s_attach_pending) return;
#if ENABLE_SERIAL
    // The serial build is always the full device; a reconnect is a plain bounce.
    detach();
    sleep_ms(150);
    // error: NOT-WORTH — nothing; tud_connect/disconnect only fail on a port without D+ pull-up control, which the RP2350 has
    tud_connect();
    s_pm.presented(usbpm::Mode::Full, time_us_64());
#else
    if (s_pm.mode() != usbpm::Mode::Off) present(s_pm.mode(), time_us_64());
#endif
}

void usb_power_task() {
    const uint64_t now = time_us_64();
    if (s_attach_pending) {
        if (now < s_attach_at) return;
        s_attach_pending = false;
        attach(s_attach_mode, now);
        return;
    }
    // A bus reset or a resume TinyUSB didn't report as one: running again.
    if (s_pm.suspended() && tud_connected() && !tud_suspended()) s_pm.resume(now);
    if (bt_is_connected()) s_pm.buttons(interrupt_in_data[7], interrupt_in_data[8], interrupt_in_data[9]);

    const bool kbd_ready = g_usb_wake_keyboard && tud_hid_n_ready(0);
    switch (s_pm.tick(now, settings(), kbd_ready)) {
        case usbpm::Act::None:
            break;
        case usbpm::Act::PowerOff:
            printf("[USB PM] suspended for 3 s: controller off\n");
            // error: NOT-WORTH — nothing; false only when there is no ACL, i.e. already disconnected
            bt_disconnect();
            break;
        case usbpm::Act::RemoteWakeup: {
            // error: LOGGED-OK — printf of the result; the PC didn't allow remote wakeup, the F15 key path follows
            const bool ok = tud_remote_wakeup();
            printf("[USB PM] remote wakeup -> %d\n", (int) ok);
            s_pm.wake_result(ok, now);
            break;
        }
        case usbpm::Act::KeyDown:
            // error: NOT-WORTH — nothing; the wake state machine (usb_power.h) only sends when the endpoint is ready (hid_ready) and presses the key kKeyPresses times
            send_key(kKeyF15);
            break;
        case usbpm::Act::KeyUp:
            // error: NOT-WORTH — nothing; the wake state machine (usb_power.h) only sends when the endpoint is ready (hid_ready) and presses the key kKeyPresses times
            send_key(0);
            break;
        case usbpm::Act::Present:
            present(s_pm.target(), now);
            break;
        case usbpm::Act::Hide:
            detach();
            attach(usbpm::Mode::Off, now);
            break;
    }
}

// TinyUSB device callbacks (from tud_task, main loop).
void tud_suspend_cb(bool remote_wakeup_en) {
    printf("[USB PM] suspend (remote wakeup %s)\n", remote_wakeup_en ? "enabled" : "disabled");
    s_pm.suspend(time_us_64());
}

void tud_resume_cb(void) {
    printf("[USB PM] resume\n");
    s_pm.resume(time_us_64());
}

void tud_mount_cb(void) {
    s_pm.mount(time_us_64());
}
