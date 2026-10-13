//
// What the dongle does when the PC sleeps and wakes (upstream awalol 9923ce3,
// caaff10, 9ddb181, edec7f7, c67c7f6, reworked so a misbehaving host can't
// trap the dongle or the PC):
//
//   * Controller off when the PC sleeps: once the USB bus has stayed suspended
//     for 3 s, the controller's Bluetooth link is dropped and the DualSense
//     turns itself off. The 3 s ride out USB hubs that idle the bus while the
//     PC is awake (upstream #148, #174), and a deliberate USB reconnect doesn't
//     count as a sleep.
//   * PS button wakes the PC (off by default): with no controller connected
//     the dongle presents a wake-only boot keyboard (its own VID/PID, nothing
//     else on it), so the PC arms USB remote wakeup on it while it sleeps. The
//     controller reconnecting (PS) or a button press then signals remote
//     wakeup, and after the resume the keyboard presses F15 twice so Windows
//     turns the screen on and stays awake. When a controller is connected the
//     dongle is the DualSense exactly as without this feature; the only change
//     there is the remote-wakeup bit in the configuration descriptor.
//
// Never: touch the bus (attach, detach, switch) while it is suspended (that is
// a hot-plug, which wakes a sleeping PC or confuses it), signal remote wakeup
// the host didn't enable (TinyUSB's tud_remote_wakeup() checks) or sooner than
// 50 ms after the bus went idle, signal more than 3 times per sleep, or wait
// forever on the host. Every wait here has a time limit.
//
// This file is pure logic, no SDK: the main loop feeds Machine the TinyUSB and
// Bluetooth events and the time and carries out the one action tick() returns
// (src/usb_power.cpp). tests/host/test_usb_power.cpp checks it on the PC.
//

#ifndef DS5_BRIDGE_USB_POWER_H
#define DS5_BRIDGE_USB_POWER_H

#include <cstdint>

namespace usbpm {

// What the dongle presents on USB.
enum class Mode : uint8_t {
    Off,       // detached (no controller, wake off): the host sees nothing
    Full,      // the DualSense (audio + HID + configurator)
    Keyboard,  // only the wake keyboard (no controller, wake on)
};

// The one thing tick() asks the caller to do.
enum class Act : uint8_t {
    None,
    PowerOff,      // drop the controller's Bluetooth link (bt_disconnect)
    RemoteWakeup,  // tud_remote_wakeup(); report its result with wake_result()
    KeyDown,       // F15 down on the wake keyboard
    KeyUp,         // all keys up
    Present,       // detach, then attach as target() (the caller reports presented())
    Hide,          // detach (the caller reports presented(Mode::Off))
};

struct Settings {
    bool power_off = true;   // controller off when the PC sleeps
    bool wake = false;       // PS button wakes the PC
    bool always_on = false;  // ENABLE_SERIAL builds: always the full device, never detached
};

constexpr uint64_t kPowerOffDebounceUs = 3000000;  // sustained suspend before the controller goes off
constexpr uint64_t kReconnectGraceUs = 5000000;    // a suspend right after our own re-attach isn't a sleep
constexpr uint64_t kResumeGraceUs = 2000000;       // after a resume/mount, wait for a returning controller before hiding
constexpr uint64_t kBootGraceUs = 3000000;         // at boot, let a controller reconnect before showing the keyboard
constexpr uint64_t kWakeMinIdleUs = 50000;         // USB 2.0 7.1.7.7: no remote wakeup in the first 5 ms of idle
constexpr uint64_t kWakeCooldownUs = 1000000;      // between remote-wakeup signals
constexpr uint8_t kWakeMaxSignals = 3;             // per suspend
constexpr uint64_t kWakeResumeTimeoutUs = 15000000;  // a woken PC resumes the bus within this (S3 resume is slow)
constexpr uint64_t kKeySettleUs = 150000;          // after the resume, before F15 (upstream WAKE_SETTLE_US)
constexpr uint64_t kKeyHoldUs = 80000;             // F15 held
constexpr uint64_t kKeyGapUs = 200000;             // between presses
constexpr uint64_t kKeyStepTimeoutUs = 2000000;    // the keyboard endpoint must be ready within this
constexpr uint8_t kKeyPresses = 2;                 // upstream WAKE_KEY_ATTEMPTS

class Machine {
public:
    explicit Machine(Mode initial = Mode::Off) : mode_(initial), target_(initial) {}

    void boot(uint64_t now) { boot_until_ = now + kBootGraceUs; }

    // tud_suspend_cb. Ignored while detached: an idle bus then isn't the host.
    void suspend(uint64_t now) {
        if (mode_ == Mode::Off || suspended_) return;
        suspended_ = true;
        idle_since_ = now;
        debounce_from_ = now > grace_until_ ? now : grace_until_;
        powered_off_ = false;
        wake_ = Wake::Armed;  // a fresh sleep: re-arm, whatever the last one left
        trigger_ = false;
        signals_ = 0;
    }

    // tud_resume_cb, or the bus seen running again.
    void resume(uint64_t now) {
        if (!suspended_) return;
        suspended_ = false;
        hide_not_before_ = now + kResumeGraceUs;
        key_down_at_ = now;  // a key left down gets the full time to be released
        if (wake_ == Wake::Armed || wake_ == Wake::Refused || wake_ == Wake::Signalling) wake_ = Wake::Idle;
    }

    // tud_mount_cb: the host (re)configured us. Ends our reconnect grace.
    void mount(uint64_t now) {
        resume(now);
        grace_until_ = 0;
        hide_not_before_ = now + kResumeGraceUs;
    }

    // The controller is up (feature reports cached) or gone.
    void controller(bool ready, uint64_t now) {
        (void) now;
        if (ready && !ready_ && suspended_) trigger_ = true;
        ready_ = ready;
        have_buttons_ = false;
    }

    // Input report bytes 7..9 (D-pad + face, shoulders + sticks, PS / touchpad
    // / mute / Edge buttons), sampled every main-loop pass while connected.
    void buttons(uint8_t b7, uint8_t b8, uint8_t b9) {
        if (!ready_) return;
        const uint32_t v = b7 | (uint32_t) b8 << 8 | (uint32_t) b9 << 16;
        if (have_buttons_ && v != buttons_ && suspended_) trigger_ = true;
        buttons_ = v;
        have_buttons_ = true;
    }

    // Result of the RemoteWakeup action: false = the host didn't enable remote
    // wakeup (or the bus isn't suspended), so don't try again this sleep.
    void wake_result(bool signalled, uint64_t now) {
        if (wake_ != Wake::Signalling) return;
        if (signalled) {
            wake_ = Wake::Requested;
            at_ = now;
            signals_++;
            last_signal_ = now;
        } else {
            wake_ = Wake::Refused;
        }
    }

    // The caller re-attached as `m` (or detached, Off).
    void presented(Mode m, uint64_t now) {
        mode_ = m;
        target_ = m;
        suspended_ = false;
        grace_until_ = now + kReconnectGraceUs;
        wake_ = Wake::Idle;
        key_down_ = false;
    }

    Act tick(uint64_t now, const Settings &s, bool hid_ready) {
        if (mode_ == Mode::Off) suspended_ = false;

        // 1. Controller off after a sustained suspend, once per sleep, and not
        // when a wake is under way (that press was the user waking the PC).
        if (s.power_off && suspended_ && ready_ && !powered_off_ && signals_ == 0 &&
            wake_ != Wake::Signalling && now >= debounce_from_ &&
            now - debounce_from_ >= kPowerOffDebounceUs) {
            powered_off_ = true;
            return Act::PowerOff;
        }

        // 2. Signal remote wakeup on a reconnect or a button while suspended.
        if (suspended_) {
            if (!s.wake) return Act::None;
            if (wake_ == Wake::Signalling) wake_ = Wake::Refused;  // no result came back
            if (wake_ == Wake::Requested && now - at_ >= kWakeResumeTimeoutUs) {
                wake_ = Wake::Armed;  // the host never resumed; allow another press
                trigger_ = false;
            }
            if (wake_ == Wake::Armed && trigger_ && signals_ < kWakeMaxSignals &&
                now - idle_since_ >= kWakeMinIdleUs &&
                (signals_ == 0 || now - last_signal_ >= kWakeCooldownUs)) {
                trigger_ = false;
                wake_ = Wake::Signalling;
                return Act::RemoteWakeup;
            }
            return Act::None;
        }

        // 3. Bus running. F15 on the wake keyboard after a wake we signalled.
        if (wake_ == Wake::Requested) {
            if (mode_ == Mode::Keyboard && s.wake) {
                wake_ = Wake::KeySettle;
                at_ = now;
                presses_ = 0;
            } else {
                wake_ = Wake::Idle;  // woken as the DualSense: nothing to press
            }
        }
        if (mode_ != Mode::Keyboard) {
            if (wake_ >= Wake::KeySettle) wake_ = Wake::Idle;
        } else if (key_down_) {
            // Release after the hold, or a key left down (the host suspended
            // mid-press) as soon as the endpoint takes it. A host that never
            // polls doesn't hold us here: past the limit the key is forgotten
            // (re-attaching resets the host's view of it anyway).
            const bool held = wake_ != Wake::KeyHeld || now - at_ >= kKeyHoldUs;
            if (held && hid_ready) {
                key_down_ = false;
                if (wake_ == Wake::KeyHeld) {
                    wake_ = Wake::KeyGap;
                    at_ = now;
                    presses_++;
                }
                return Act::KeyUp;
            }
            if (now - key_down_at_ < kKeyHoldUs + kKeyStepTimeoutUs) return Act::None;
            key_down_ = false;
            wake_ = Wake::Idle;
        } else if (wake_ == Wake::KeySettle) {
            if (now - at_ < kKeySettleUs) return Act::None;
            if (hid_ready) {
                wake_ = Wake::KeyHeld;
                at_ = now;
                key_down_ = true;
                key_down_at_ = now;
                return Act::KeyDown;
            }
            if (now - at_ < kKeySettleUs + kKeyStepTimeoutUs) return Act::None;
            wake_ = Wake::Idle;
        } else if (wake_ == Wake::KeyGap) {
            if (now - at_ < kKeyGapUs) return Act::None;
            if (presses_ < kKeyPresses) {
                wake_ = Wake::KeySettle;
                at_ = now - kKeySettleUs;  // the second press needs no settle
                return Act::None;
            }
            wake_ = Wake::Idle;
        }

        // 4. Present what fits: the DualSense with a controller, else the wake
        // keyboard (wake on) or nothing.
        const Mode want = s.always_on || ready_ ? Mode::Full : s.wake ? Mode::Keyboard : Mode::Off;
        if (want == mode_) return Act::None;
        if (want != Mode::Full) {
            if (now < hide_not_before_) return Act::None;  // a controller may be on its way back
            if (mode_ == Mode::Off && now < boot_until_) return Act::None;
        }
        target_ = want;
        return want == Mode::Off ? Act::Hide : Act::Present;
    }

    Mode mode() const { return mode_; }
    Mode target() const { return target_; }
    bool suspended() const { return suspended_; }
    bool key_down() const { return key_down_; }

private:
    enum class Wake : uint8_t { Idle, Armed, Signalling, Refused, Requested, KeySettle, KeyHeld, KeyGap };

    Mode mode_;
    Mode target_;
    bool suspended_ = false;
    bool ready_ = false;
    bool powered_off_ = false;
    bool trigger_ = false;
    bool have_buttons_ = false;
    bool key_down_ = false;
    Wake wake_ = Wake::Idle;
    uint8_t signals_ = 0;
    uint8_t presses_ = 0;
    uint32_t buttons_ = 0;
    uint64_t idle_since_ = 0;
    uint64_t debounce_from_ = 0;
    uint64_t grace_until_ = 0;
    uint64_t hide_not_before_ = 0;
    uint64_t boot_until_ = 0;
    uint64_t at_ = 0;
    uint64_t key_down_at_ = 0;
    uint64_t last_signal_ = 0;
};

}  // namespace usbpm

// ---- Firmware side (src/usb_power.cpp) --------------------------------------

// True while the dongle presents only the wake keyboard: HID instance 0 is
// then the keyboard, so nothing may send DualSense reports. A plain global, so
// the RAM-resident interrupt_loop can read it without a flash call.
extern bool g_usb_wake_keyboard;
inline bool usb_wake_keyboard() { return g_usb_wake_keyboard; }

void usb_power_init();                  // after tusb_init() and the boot detach
void usb_power_task();                  // every main-loop pass
void usb_power_controller(bool ready);  // bt.cpp: controller up (feature reports cached) / gone
void usb_power_reconnect();             // 0xF6 0x03: re-attach as we are
bool usb_power_wake_enabled();          // the full configuration advertises remote wakeup

#endif //DS5_BRIDGE_USB_POWER_H
