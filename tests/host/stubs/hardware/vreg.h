#pragma once
#include "pico.h"
enum vreg_voltage { VREG_VOLTAGE_1_20 = 0x0f };
vreg_voltage vreg_get_voltage();
