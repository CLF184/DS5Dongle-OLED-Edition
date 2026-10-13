#pragma once
#include "pico.h"
constexpr int GPIO_FUNC_SPI = 1;
constexpr bool GPIO_OUT = true, GPIO_IN = false;
inline void gpio_init(uint) {}
inline void gpio_set_function(uint, int) {}
inline void gpio_set_dir(uint, bool) {}
inline void gpio_put(uint, bool) {}
inline bool gpio_get(uint) { return true; }
inline void gpio_pull_up(uint) {}
