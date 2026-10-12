#pragma once
#include "pico.h"
#define CLOCKS_FC0_SRC_VALUE_CLK_SYS 0x0b
uint32_t frequency_count_khz(uint32_t src);
