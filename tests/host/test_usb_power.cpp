// src/usb_power.h: the suspend / resume state machine. Controller off only
// after a sustained suspend (hub blips and our own reconnects don't count),
// the bus never touched while suspended, remote wakeup only when the host
// allows it and at most 3 times a sleep, F15 on the wake keyboard after a wake,
// and no wait without a time limit.
#include <vector>
#include "test.h"
#include "usb_power.h"

using usbpm::Act;
using usbpm::Machine;
using usbpm::Mode;
using usbpm::Settings;

namespace {
constexpr uint64_t ms = 1000;

// Runs a Machine like usb_power_task does, 1 ms a tick, carrying out what it
// asks: Present/Hide take effect at once, RemoteWakeup answers `host_wake`.
struct Sim {
    Machine m;
    Settings s;
    uint64_t now = 1'000'000;
    bool hid_ready = true;
    bool host_wake = true;  // the host enabled remote wakeup
    std::vector<Act> acts;
    int count(Act a) const {
        int n = 0;
        for (Act x : acts) n += x == a;
        return n;
    }
    explicit Sim(Mode initial = Mode::Off, bool wake = false, bool power_off = true) : m(initial) {
        s.wake = wake;
        s.power_off = power_off;
        m.boot(now);
    }
    void run(uint64_t us) {
        for (uint64_t t = 0; t < us; t += ms) {
            now += ms;
            const Act a = m.tick(now, s, hid_ready);
            if (a == Act::None) continue;
            acts.push_back(a);
            if (a == Act::Present) m.presented(m.target(), now);
            if (a == Act::Hide) m.presented(Mode::Off, now);
            if (a == Act::RemoteWakeup) m.wake_result(host_wake, now);
        }
    }
    // The controller is up and the host has enumerated the DualSense.
    void connected() {
        m.controller(true, now);
        run(10 * ms);
        m.mount(now);
        run(10 * ms);
        acts.clear();
    }
};
}  // namespace

TEST(usbpm_controller_shows_dongle_at_once) {
    Sim sim;  // wake off: nothing on the bus without a controller
    sim.run(10'000 * ms);
    CHECK(sim.acts.empty());
    sim.m.controller(true, sim.now);
    sim.run(ms);
    CHECK_EQ(sim.count(Act::Present), 1);
    CHECK(sim.m.mode() == Mode::Full);
}

TEST(usbpm_hub_blip_never_powers_off) {
    Sim sim;
    sim.connected();
    sim.run(6000 * ms);  // past the reconnect grace
    for (int i = 0; i < 20; i++) {  // a hub idling the bus for up to 2.9 s at a time
        sim.m.suspend(sim.now);
        sim.run(2900 * ms);
        sim.m.resume(sim.now);
        sim.run(100 * ms);
    }
    CHECK_EQ(sim.count(Act::PowerOff), 0);
    CHECK(sim.acts.empty());  // and USB left alone
}

TEST(usbpm_sustained_suspend_powers_off_once) {
    Sim sim;
    sim.connected();
    sim.run(6000 * ms);
    sim.m.suspend(sim.now);
    sim.run(2990 * ms);
    CHECK_EQ(sim.count(Act::PowerOff), 0);
    sim.run(20 * ms);
    CHECK_EQ(sim.count(Act::PowerOff), 1);
    sim.run(60'000 * ms);
    CHECK_EQ(sim.count(Act::PowerOff), 1);  // once per sleep
    // The controller drops; the bus stays as it is while suspended.
    sim.m.controller(false, sim.now);
    sim.run(60'000 * ms);
    CHECK_EQ(sim.count(Act::Hide), 0);
    CHECK_EQ(sim.count(Act::Present), 0);
    // A later sleep powers it off again (it came back in between).
    sim.m.resume(sim.now);
    sim.m.controller(true, sim.now);
    sim.run(100 * ms);
    sim.m.suspend(sim.now);
    sim.run(3100 * ms);
    CHECK_EQ(sim.count(Act::PowerOff), 2);
}

TEST(usbpm_power_off_setting_off) {
    Sim sim(Mode::Off, false, false);
    sim.connected();
    sim.run(6000 * ms);
    sim.m.suspend(sim.now);
    sim.run(120'000 * ms);
    CHECK_EQ(sim.count(Act::PowerOff), 0);
}

TEST(usbpm_own_reconnect_is_not_a_sleep) {
    Sim sim;
    sim.connected();
    sim.run(6000 * ms);
    sim.m.presented(Mode::Full, sim.now);  // 0xF6 0x03 / a mode switch re-attached us
    sim.run(200 * ms);
    sim.m.suspend(sim.now);  // the idle bus while the host hasn't reset us yet
    sim.run(7000 * ms);      // grace (5 s) + 2 s
    CHECK_EQ(sim.count(Act::PowerOff), 0);
    sim.run(1100 * ms);      // grace + 3 s: a real sleep after all
    CHECK_EQ(sim.count(Act::PowerOff), 1);
}

TEST(usbpm_detached_ignores_suspend) {
    Sim sim;  // Off: an idle bus isn't the host
    sim.m.suspend(sim.now);
    CHECK(!sim.m.suspended());
    sim.m.controller(true, sim.now);
    sim.run(ms);
    CHECK_EQ(sim.count(Act::Present), 1);
}

TEST(usbpm_resume_waits_for_returning_controller) {
    Sim sim;
    sim.connected();
    sim.run(6000 * ms);
    sim.m.suspend(sim.now);
    sim.run(4000 * ms);  // controller off
    sim.m.controller(false, sim.now);
    sim.run(1000 * ms);
    sim.m.resume(sim.now);
    sim.run(1500 * ms);
    CHECK_EQ(sim.count(Act::Hide), 0);  // within the 2 s resume grace
    sim.m.controller(true, sim.now);    // PS pressed: back before the grace ran out
    sim.run(5000 * ms);
    CHECK_EQ(sim.count(Act::Hide), 0);
    CHECK_EQ(sim.count(Act::Present), 0);  // no flap
    // Not back: hidden after the grace (edec7f7's ghost device).
    sim.m.suspend(sim.now);
    sim.run(4000 * ms);
    sim.m.controller(false, sim.now);
    sim.m.resume(sim.now);
    sim.run(1900 * ms);
    CHECK_EQ(sim.count(Act::Hide), 0);
    sim.run(200 * ms);
    CHECK_EQ(sim.count(Act::Hide), 1);
}

TEST(usbpm_wake_off_never_signals) {
    Sim sim;
    sim.connected();
    sim.run(6000 * ms);
    sim.m.suspend(sim.now);
    for (int i = 0; i < 50; i++) {
        sim.m.buttons(0x08, 0, (uint8_t) (i & 1));  // PS pressed and released
        sim.run(100 * ms);
    }
    sim.m.controller(false, sim.now);
    sim.m.controller(true, sim.now);
    sim.run(1000 * ms);
    CHECK_EQ(sim.count(Act::RemoteWakeup), 0);
    CHECK_EQ(sim.count(Act::Present), 0);
}

TEST(usbpm_wake_keyboard_flow) {
    Sim sim(Mode::Off, true);
    sim.run(2900 * ms);
    CHECK(sim.acts.empty());  // boot grace: a controller may reconnect first
    sim.run(200 * ms);
    CHECK_EQ(sim.count(Act::Present), 1);
    CHECK(sim.m.mode() == Mode::Keyboard);
    sim.m.mount(sim.now);
    sim.run(10'000 * ms);
    sim.acts.clear();

    // The PC sleeps; PS brings the controller back 20 ms later.
    sim.m.suspend(sim.now);
    sim.run(20 * ms);
    sim.m.controller(true, sim.now);
    sim.run(20 * ms);
    CHECK_EQ(sim.count(Act::RemoteWakeup), 0);  // not in the first 50 ms of idle
    sim.run(20 * ms);
    CHECK_EQ(sim.count(Act::RemoteWakeup), 1);
    CHECK_EQ(sim.count(Act::Present), 0);  // never re-attach while suspended
    sim.run(3000 * ms);
    CHECK_EQ(sim.count(Act::PowerOff), 0);  // the press was the user waking the PC
    sim.m.resume(sim.now);
    sim.run(100 * ms);
    CHECK_EQ(sim.count(Act::KeyDown), 0);  // settle first
    sim.run(100 * ms);
    CHECK_EQ(sim.count(Act::KeyDown), 1);
    sim.run(2000 * ms);
    CHECK_EQ(sim.count(Act::KeyDown), 2);
    CHECK_EQ(sim.count(Act::KeyUp), 2);
    // Then the DualSense, once the keys are up.
    CHECK_EQ(sim.count(Act::Present), 1);
    CHECK(sim.m.mode() == Mode::Full);
    std::vector<Act> order;
    for (Act a : sim.acts) if (a != Act::None) order.push_back(a);
    const std::vector<Act> want{Act::RemoteWakeup, Act::KeyDown, Act::KeyUp, Act::KeyDown, Act::KeyUp, Act::Present};
    CHECK(order == want);
}

TEST(usbpm_wake_key_timing) {
    Sim sim(Mode::Keyboard, true);
    sim.m.mount(sim.now);
    sim.run(10'000 * ms);
    sim.m.suspend(sim.now);
    sim.run(100 * ms);
    sim.m.controller(true, sim.now);
    sim.run(ms);
    sim.m.resume(sim.now);
    uint64_t down = 0, up = 0;
    for (int i = 0; i < 1000 && !up; i++) {
        sim.now += ms;
        const Act a = sim.m.tick(sim.now, sim.s, true);
        if (a == Act::KeyDown) down = sim.now;
        if (a == Act::KeyUp) up = sim.now;
    }
    CHECK(down && up);
    CHECK(up - down >= usbpm::kKeyHoldUs);
}

TEST(usbpm_wake_refused_by_host) {
    Sim sim(Mode::Keyboard, true);
    sim.host_wake = false;  // remote wakeup not enabled: tud_remote_wakeup() says no
    sim.m.mount(sim.now);
    sim.run(10'000 * ms);
    sim.m.suspend(sim.now);
    sim.run(100 * ms);
    sim.m.controller(true, sim.now);
    for (int i = 0; i < 40; i++) {
        sim.m.buttons(0x08, 0, (uint8_t) (i & 1));
        sim.run(500 * ms);
    }
    CHECK_EQ(sim.count(Act::RemoteWakeup), 1);  // asked once, then left alone
    CHECK_EQ(sim.count(Act::Present), 0);
    // The PC wakes some other way: the DualSense, no keys.
    sim.m.resume(sim.now);
    sim.run(500 * ms);
    CHECK_EQ(sim.count(Act::KeyDown), 0);
    CHECK_EQ(sim.count(Act::Present), 1);
    CHECK(sim.m.mode() == Mode::Full);
}

TEST(usbpm_wake_signals_capped) {
    Sim sim(Mode::Keyboard, true);
    sim.m.mount(sim.now);
    sim.run(10'000 * ms);
    sim.m.suspend(sim.now);
    sim.run(100 * ms);
    sim.m.controller(true, sim.now);
    // The host accepts the signal but never resumes: re-armed after the
    // timeout, at most 3 signals this sleep, never closer than 1 s.
    uint64_t last = 0;
    bool spaced = true;
    for (int i = 0; i < 2000; i++) {
        sim.m.buttons(0x08, (uint8_t) (i & 1), 0);
        const size_t before = sim.acts.size();
        sim.run(50 * ms);
        for (size_t k = before; k < sim.acts.size(); k++) {
            if (sim.acts[k] != Act::RemoteWakeup) continue;
            if (last && sim.now - last < usbpm::kWakeCooldownUs) spaced = false;
            last = sim.now;
        }
    }
    CHECK_EQ(sim.count(Act::RemoteWakeup), (int) usbpm::kWakeMaxSignals);
    CHECK(spaced);
    // A new sleep re-arms.
    sim.m.resume(sim.now);
    sim.run(5000 * ms);
    sim.acts.clear();
    sim.m.suspend(sim.now);
    sim.run(100 * ms);
    sim.m.buttons(0x08, 0, 1);
    sim.run(100 * ms);
    CHECK_EQ(sim.count(Act::RemoteWakeup), 1);
}

TEST(usbpm_wake_from_full_mode_has_no_keys) {
    Sim sim(Mode::Off, true, false);  // controller stays on while the PC sleeps
    sim.connected();
    sim.run(6000 * ms);
    sim.m.suspend(sim.now);
    sim.run(5000 * ms);
    sim.m.buttons(0x08, 0, 0);
    sim.run(ms);
    sim.m.buttons(0x08, 0, 1);  // PS
    sim.run(100 * ms);
    CHECK_EQ(sim.count(Act::RemoteWakeup), 1);
    sim.m.resume(sim.now);
    sim.run(3000 * ms);
    CHECK_EQ(sim.count(Act::KeyDown), 0);
    CHECK_EQ(sim.count(Act::Present), 0);  // stays the DualSense
}

TEST(usbpm_keyboard_endpoint_never_ready) {
    Sim sim(Mode::Keyboard, true);
    sim.m.mount(sim.now);
    sim.run(10'000 * ms);
    sim.m.suspend(sim.now);
    sim.run(100 * ms);
    sim.m.controller(true, sim.now);
    sim.run(100 * ms);
    sim.hid_ready = false;  // the host never polls the keyboard
    sim.m.resume(sim.now);
    sim.run(5000 * ms);
    CHECK_EQ(sim.count(Act::KeyDown), 0);
    CHECK_EQ(sim.count(Act::Present), 1);  // no deadlock: the DualSense anyway
}

TEST(usbpm_key_left_down_is_released) {
    Sim sim(Mode::Keyboard, true);
    sim.m.mount(sim.now);
    sim.run(10'000 * ms);
    sim.m.suspend(sim.now);
    sim.run(100 * ms);
    sim.m.controller(true, sim.now);
    sim.run(100 * ms);
    sim.m.resume(sim.now);
    sim.run(160 * ms);
    CHECK_EQ(sim.count(Act::KeyDown), 1);
    CHECK(sim.m.key_down());
    sim.m.suspend(sim.now);  // the host dips back into suspend mid-press
    sim.run(2000 * ms);
    CHECK_EQ(sim.count(Act::KeyUp), 0);
    sim.m.resume(sim.now);
    sim.run(ms);
    CHECK_EQ(sim.count(Act::KeyUp), 1);  // released first thing
    CHECK(!sim.m.key_down());
}

TEST(usbpm_controller_leaves_while_awake) {
    Sim sim(Mode::Off, true);
    sim.connected();
    sim.run(3000 * ms);
    sim.m.controller(false, sim.now);  // inactivity disconnect, PC awake
    sim.run(ms);
    CHECK_EQ(sim.count(Act::Present), 1);
    CHECK(sim.m.mode() == Mode::Keyboard);
    // Wake off: hidden instead.
    Sim off;
    off.connected();
    off.run(3000 * ms);
    off.m.controller(false, off.now);
    off.run(ms);
    CHECK_EQ(off.count(Act::Hide), 1);
    CHECK(off.m.mode() == Mode::Off);
}

TEST(usbpm_serial_build_stays_attached) {
    Sim sim(Mode::Full, false);
    sim.s.always_on = true;
    sim.run(5000 * ms);
    sim.m.controller(true, sim.now);
    sim.run(6000 * ms);
    sim.m.suspend(sim.now);
    sim.run(4000 * ms);
    CHECK_EQ(sim.count(Act::PowerOff), 1);
    sim.m.controller(false, sim.now);
    sim.m.resume(sim.now);
    sim.run(10'000 * ms);
    CHECK_EQ(sim.count(Act::Hide), 0);
    CHECK_EQ(sim.count(Act::Present), 0);
}
