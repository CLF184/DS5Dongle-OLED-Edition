#pragma once
#include "pico.h"
struct spi_inst_t {};
inline spi_inst_t s_fake_spi;
inline spi_inst_t *spi1 = &s_fake_spi;
inline uint spi_init(spi_inst_t*, uint baud) { return baud; }
inline int spi_write_blocking(spi_inst_t*, const uint8_t*, size_t n) { return static_cast<int>(n); }
