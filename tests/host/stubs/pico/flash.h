#pragma once
#include "pico.h"
int flash_safe_execute(void (*fn)(void *), void *param, uint32_t timeout_ms);
extern bool g_fake_flash_fail;  // make the next writes fail (flash_safe_execute != PICO_OK)
