#pragma once
#include "pico.h"
extern uint64_t g_fake_time_us;
inline uint64_t time_us_64() { return g_fake_time_us; }
inline uint32_t time_us_32() { return (uint32_t) g_fake_time_us; }
inline void sleep_ms(uint32_t ms) { g_fake_time_us += (uint64_t) ms * 1000; }
inline void sleep_us(uint64_t us) { g_fake_time_us += us; }
