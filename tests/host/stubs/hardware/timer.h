#pragma once
#include "pico/time.h"
struct fake_timer_hw_t { volatile uint32_t timerawl; };
extern fake_timer_hw_t *timer_hw;
