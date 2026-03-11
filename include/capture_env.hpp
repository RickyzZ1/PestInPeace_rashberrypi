#pragma once

struct EnvSnapshot {
    bool valid = false;
    double temperature_c = 0.0;
    double pressure_hpa = 0.0;
    double humidity_pct = 0.0;
};

bool capture_read_env_data(EnvSnapshot& out);
void capture_print_env_data(const EnvSnapshot& env);
