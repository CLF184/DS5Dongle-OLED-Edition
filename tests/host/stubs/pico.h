// Host-test stubs for the Pico SDK: just what src/ needs to compile on a PC.
// Flash is a RAM array (fakes.cpp), time is a settable counter.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#define PICO_RP2350 0
#define PICO_OK 0
#define __force_inline inline __attribute__((always_inline))
#define __not_in_flash_func(f) f
#ifndef SYS_CLOCK_KHZ
#define SYS_CLOCK_KHZ 320000
#endif
typedef unsigned int uint;
