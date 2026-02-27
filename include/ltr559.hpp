#pragma once
#include <cstdint>

bool ltr559_init();
void ltr559_deinit();

bool ltr559_read_raw(uint16_t& ch0, uint16_t& ch1, uint16_t& ps, uint8_t& status);
double ltr559_estimate_lux(uint16_t ch0, uint16_t ch1);
