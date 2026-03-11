#pragma once

#include <cstdint>

bool bme280_init();
void bme280_deinit();

// Returns temperature in C, humidity in %RH, pressure in hPa.
bool bme280_read_env(double& temperature_c, double& humidity_percent, double& pressure_hpa);
