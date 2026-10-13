// Status pin (upstream 86da98c). Upstream drives it from the Bluetooth
// connect / disconnect handlers and an alarm; here the main loop follows the
// connection and the setting instead, so a change from the app or the OLED,
// a factory reset and a pin that moves all take the same path, and no pin
// stays driven after it is turned off.
#include "status_gpio.h"

#include "bt.h"
#include "config.h"
#include "hardware/gpio.h"
#include "pico/time.h"

namespace {
uint8_t s_pin = kStatusGpioOff;  // the pin driven now
uint8_t s_mode = 0;
bool s_connected = false;
uint64_t s_pulse_end_us = 0;  // mode 1: when the connect pulse ends (0 = none)
}

void status_gpio_task() {
    const Config_body &c = get_config();
    const bool connected = bt_is_connected();
    const uint64_t now = time_us_64();
    if (c.status_gpio_pin != s_pin || c.status_gpio_mode != s_mode) {
        if (s_pin != kStatusGpioOff) gpio_deinit(s_pin);  // back to an input, not driven
        s_pin = c.status_gpio_pin;
        s_mode = c.status_gpio_mode;
        s_pulse_end_us = 0;
        s_connected = connected;
        if (s_pin == kStatusGpioOff) return;
        gpio_init(s_pin);
        gpio_set_dir(s_pin, GPIO_OUT);
        // A pulse means "a controller just connected", so changing the setting
        // never pulses.
        gpio_put(s_pin, s_mode == 0 && connected);
        return;
    }
    if (s_pin == kStatusGpioOff) {
        s_connected = connected;
        return;
    }
    if (connected != s_connected) {
        s_connected = connected;
        if (s_mode == 0) {
            gpio_put(s_pin, connected);
        } else if (connected) {
            gpio_put(s_pin, true);
            s_pulse_end_us = now + kStatusGpioPulseMs * 1000;
        }
    }
    if (s_pulse_end_us && now >= s_pulse_end_us) {
        gpio_put(s_pin, false);
        s_pulse_end_us = 0;
    }
}
