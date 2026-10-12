#pragma once
#include "pico.h"
void adc_init();
void adc_set_temp_sensor_enabled(bool enable);
void adc_select_input(uint32_t input);
uint16_t adc_read();
