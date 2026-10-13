// Core0's Bluetooth/USB hot path in RAM (ported from upstream awalol Phases
// B.1-B.4 and the rest of A.1): functions that run for every Bluetooth or USB
// packet are copied to SRAM at boot so they never wait on the flash (XIP)
// cache, which the OLED, BTstack and TinyUSB keep evicting. The SDK objects on
// that path are moved by relocate_to_ram() in CMakeLists.txt; our own are
// marked with CORE0_RAM_FUNC. tools/check_ram_residency.sh checks both.
//
// RP2350 only: the original Pico W (RP2040, 264 KB SRAM) has ~31 KB of heap
// left, which the mic decoder and BTstack's containers need, so there these
// stay in flash.
#ifndef DS5_BRIDGE_CORE0_RAM_H
#define DS5_BRIDGE_CORE0_RAM_H

#include "pico.h"

#if PICO_RP2350
#define CORE0_RAM_FUNC(func_name) __not_in_flash_func(func_name)
#else
#define CORE0_RAM_FUNC(func_name) func_name
#endif

#endif
