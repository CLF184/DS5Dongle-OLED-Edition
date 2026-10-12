#pragma once
#include "pico.h"
#define CYW43_WL_GPIO_LED_PIN 0
void cyw43_arch_gpio_put(uint32_t wl_gpio, bool value);
