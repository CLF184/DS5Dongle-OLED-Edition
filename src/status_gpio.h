// A GPIO that shows whether a controller is connected (upstream 86da98c, #225),
// for an LED or a PC's power-button header. Config_body status_gpio_pin /
// status_gpio_mode, set by the app (protocol 1.13).
#ifndef DS5_BRIDGE_STATUS_GPIO_H
#define DS5_BRIDGE_STATUS_GPIO_H

#include <cstdint>

constexpr uint8_t kStatusGpioOff = 0xFF;
constexpr uint32_t kStatusGpioPulseMs = 200;  // mode 1: high this long when a controller connects

// The header pins nothing else uses, on the Pico W and the Pico 2 W alike:
// not GP0/GP1 (the default UART), not the Waveshare Pico-OLED-1.3's pins
// (GP8-12 SPI + reset, GP15/GP17 keys; oled.cpp drives them with or without
// a display) and not GP23-25/29 (the CYW43 radio's, not on the header). The
// app offers the same list (src/protocol/config.ts STATUS_GPIO_PINS).
constexpr bool status_gpio_pin_valid(uint8_t pin) {
    if (pin == kStatusGpioOff) return true;
    switch (pin) {
        case 2: case 3: case 4: case 5: case 6: case 7:
        case 13: case 14: case 16:
        case 18: case 19: case 20: case 21: case 22:
        case 26: case 27: case 28:
            return true;
        default:
            return false;
    }
}

// Main loop: follows the setting (a pin that changes is released back to an
// input) and the connection.
void status_gpio_task();

#endif // DS5_BRIDGE_STATUS_GPIO_H
