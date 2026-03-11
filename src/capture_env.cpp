#include "capture_env.hpp"

#include "bme280.hpp"

#include <iomanip>
#include <iostream>

bool capture_read_env_data(EnvSnapshot& out) {
    out = EnvSnapshot{};
    if (!bme280_read_env(out.temperature_c, out.humidity_pct, out.pressure_hpa)) {
        return false;
    }
    out.valid = true;
    return true;
}

void capture_print_env_data(const EnvSnapshot& env) {
    if (!env.valid) {
        std::cerr << "[env] bme280 read failed\n";
        return;
    }

    std::cout << std::fixed << std::setprecision(2)
              << "[BME280] Temperature: " << env.temperature_c << " C, "
              << "Pressure: " << env.pressure_hpa << " hPa, "
              << "Humidity: " << env.humidity_pct << " %\n"
              << std::defaultfloat;
}
