#pragma once

struct SoilSnapshot {
    bool valid = false;
    int raw = 0;               // 0..1023
    double moisture_pct = 0.0; // 0..100
};

bool soil_init(int channel = 0);
void soil_deinit();
bool soil_read_snapshot(SoilSnapshot& out);
