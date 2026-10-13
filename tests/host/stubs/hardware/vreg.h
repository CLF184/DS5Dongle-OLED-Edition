#pragma once
#include "pico.h"
enum vreg_voltage { VREG_VOLTAGE_1_10 = 0x0b, VREG_VOLTAGE_1_20 = 0x0d, VREG_VOLTAGE_DEFAULT = VREG_VOLTAGE_1_10 };
vreg_voltage vreg_get_voltage();
