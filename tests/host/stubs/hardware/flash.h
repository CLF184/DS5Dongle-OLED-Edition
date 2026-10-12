#pragma once
#include "pico.h"
#define FLASH_PAGE_SIZE 256u
#define FLASH_SECTOR_SIZE 4096u
#define PICO_FLASH_SIZE_BYTES (4u * 1024u * 1024u)
extern uint8_t g_fake_flash[PICO_FLASH_SIZE_BYTES];
#define XIP_BASE ((uintptr_t) g_fake_flash)
void flash_range_erase(uint32_t offset, size_t count);
void flash_range_program(uint32_t offset, const uint8_t *data, size_t count);
