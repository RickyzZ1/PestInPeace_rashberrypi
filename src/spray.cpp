#include "spray.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <gpiod.h>
#include <iostream>
#include <thread>

namespace {

gpiod_chip* g_chip = nullptr;
gpiod_line_request* g_pump_req = nullptr;
gpiod_line_request* g_zone1_req = nullptr;
gpiod_line_request* g_zone2_req = nullptr;

unsigned int g_pump_gpio = 26;   // physical pin 37
unsigned int g_zone1_gpio = 20;  // physical pin 38
unsigned int g_zone2_gpio = 21;  // physical pin 40

bool g_active_low = true;        // common 3-channel relay boards are low-level trigger
bool g_inited = false;

// Initial dosing profile (zone-1 only): low/med/high spray durations.
int g_ms_low = 2000;
int g_ms_med = 5000;
int g_ms_high = 9000;

// Safety sequence delays:
// 1) open valve first, then start pump
// 2) stop pump first, then close valve
int g_valve_lead_ms = 400;
int g_valve_tail_ms = 400;

double g_thr_low = 1.0;
double g_thr_med = 3.0;
double g_thr_high = 6.0;

int env_int_or(const char* key, int fallback) {
    const char* v = std::getenv(key);
    if (!v || !*v) return fallback;
    char* endp = nullptr;
    const long n = std::strtol(v, &endp, 10);
    if (endp == v || *endp != '\0') return fallback;
    return static_cast<int>(n);
}

double env_double_or(const char* key, double fallback) {
    const char* v = std::getenv(key);
    if (!v || !*v) return fallback;
    char* endp = nullptr;
    const double n = std::strtod(v, &endp);
    if (endp == v || *endp != '\0') return fallback;
    return n;
}

gpiod_line_request* request_out(unsigned int offset, const char* consumer) {
    gpiod_line_request* req = nullptr;
    gpiod_line_settings* settings = gpiod_line_settings_new();
    gpiod_line_config* config = nullptr;
    gpiod_request_config* rconf = nullptr;

    if (!settings) goto cleanup;

    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_active_low(settings, g_active_low);
    gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE);

    config = gpiod_line_config_new();
    if (!config) goto cleanup;
    if (gpiod_line_config_add_line_settings(config, &offset, 1, settings) < 0) goto cleanup;

    rconf = gpiod_request_config_new();
    if (!rconf) goto cleanup;
    gpiod_request_config_set_consumer(rconf, consumer);
    req = gpiod_chip_request_lines(g_chip, rconf, config);

cleanup:
    if (rconf) gpiod_request_config_free(rconf);
    if (config) gpiod_line_config_free(config);
    if (settings) gpiod_line_settings_free(settings);
    return req;
}

void set_line(gpiod_line_request* req, unsigned int offset, bool on) {
    if (!req) return;
    const gpiod_line_value v = on ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE;
    (void)gpiod_line_request_set_value(req, offset, v);
}

void set_all(bool on) {
    set_line(g_pump_req, g_pump_gpio, on);
    set_line(g_zone1_req, g_zone1_gpio, on);
    set_line(g_zone2_req, g_zone2_gpio, on);
}

void set_pump(bool on) {
    set_line(g_pump_req, g_pump_gpio, on);
}

void set_zone1(bool on) {
    set_line(g_zone1_req, g_zone1_gpio, on);
}

void set_zone2(bool on) {
    set_line(g_zone2_req, g_zone2_gpio, on);
}

} // namespace

bool spray_init(unsigned int pump_gpio, unsigned int zone1_gpio, unsigned int zone2_gpio) {
    spray_deinit();

    g_pump_gpio = pump_gpio;
    g_zone1_gpio = zone1_gpio;
    g_zone2_gpio = zone2_gpio;

    g_active_low = env_int_or("SPRAY_ACTIVE_LOW", 1) != 0;
    g_ms_low = std::max(0, env_int_or("SPRAY_LOW_MS", g_ms_low));
    g_ms_med = std::max(0, env_int_or("SPRAY_MED_MS", g_ms_med));
    g_ms_high = std::max(0, env_int_or("SPRAY_HIGH_MS", g_ms_high));
    g_valve_lead_ms = std::max(0, env_int_or("SPRAY_VALVE_LEAD_MS", g_valve_lead_ms));
    g_valve_tail_ms = std::max(0, env_int_or("SPRAY_VALVE_TAIL_MS", g_valve_tail_ms));

    g_thr_low = env_double_or("SPRAY_THR_LOW", g_thr_low);
    g_thr_med = env_double_or("SPRAY_THR_MED", g_thr_med);
    g_thr_high = env_double_or("SPRAY_THR_HIGH", g_thr_high);

    g_chip = gpiod_chip_open("/dev/gpiochip0");
    if (!g_chip) {
        std::cerr << "[spray] gpiod_chip_open failed\n";
        return false;
    }

    g_pump_req = request_out(g_pump_gpio, "spray-pump");
    g_zone1_req = request_out(g_zone1_gpio, "spray-zone1");
    g_zone2_req = request_out(g_zone2_gpio, "spray-zone2");
    if (!g_pump_req || !g_zone1_req || !g_zone2_req) {
        std::cerr << "[spray] request lines failed\n";
        spray_deinit();
        return false;
    }

    set_all(false);
    g_inited = true;
    std::cout << "[spray] init ok, pump=" << g_pump_gpio
              << ", zone1=" << g_zone1_gpio
              << ", zone2=" << g_zone2_gpio
              << ", active_low=" << (g_active_low ? 1 : 0)
              << ", dose_ms(low/med/high)=" << g_ms_low << "/" << g_ms_med << "/" << g_ms_high
              << ", valve_lead_ms=" << g_valve_lead_ms
              << ", valve_tail_ms=" << g_valve_tail_ms << "\n";
    return true;
}

void spray_deinit() {
    set_all(false);
    if (g_pump_req) {
        gpiod_line_request_release(g_pump_req);
        g_pump_req = nullptr;
    }
    if (g_zone1_req) {
        gpiod_line_request_release(g_zone1_req);
        g_zone1_req = nullptr;
    }
    if (g_zone2_req) {
        gpiod_line_request_release(g_zone2_req);
        g_zone2_req = nullptr;
    }
    if (g_chip) {
        gpiod_chip_close(g_chip);
        g_chip = nullptr;
    }
    g_inited = false;
}

bool spray_apply_for_pressure(double pest_pressure) {
    if (!g_inited) return false;
    if (pest_pressure < g_thr_low) {
        std::cout << "[spray] skip, pressure=" << pest_pressure << "\n";
        return false;
    }

    int spray_ms = g_ms_low;
    const char* level = "LOW";
    if (pest_pressure >= g_thr_high) {
        spray_ms = g_ms_high;
        level = "HIGH";
    } else if (pest_pressure >= g_thr_med) {
        spray_ms = g_ms_med;
        level = "MED";
    }

    if (spray_ms <= 0) {
        std::cout << "[spray] disabled by duration config, level=" << level << "\n";
        return false;
    }

    std::cout << "[spray] start level=" << level
              << ", pressure=" << pest_pressure
              << ", duration_ms=" << spray_ms
              << ", target_zone=1\n";

    // Zone-1 only mode for now: keep zone-2 closed.
    set_zone2(false);

    // Safer sequence: valve first -> pump, then pump off -> valve off.
    set_zone1(true);
    if (g_valve_lead_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(g_valve_lead_ms));
    }

    set_pump(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(spray_ms));
    set_pump(false);

    if (g_valve_tail_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(g_valve_tail_ms));
    }
    set_zone1(false);
    set_zone2(false);

    std::cout << "[spray] done\n";
    return true;
}

bool spray_apply_for_scope(int scope_class) {
    if (!g_inited) return false;

    if (scope_class <= 0) {
        std::cout << "[spray] skip by decision scope_class=" << scope_class << "\n";
        return false;
    }

    int spray_ms = g_ms_med;
    const char* level = "SCOPE1";
    if (scope_class >= 2) {
        spray_ms = g_ms_high;
        level = "SCOPE2";
    }

    if (spray_ms <= 0) {
        std::cout << "[spray] disabled by duration config, level=" << level << "\n";
        return false;
    }

    std::cout << "[spray] start by decision level=" << level
              << ", scope_class=" << scope_class
              << ", duration_ms=" << spray_ms
              << ", target_zone=1\n";

    set_zone2(false);
    set_zone1(true);
    if (g_valve_lead_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(g_valve_lead_ms));
    }

    set_pump(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(spray_ms));
    set_pump(false);

    if (g_valve_tail_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(g_valve_tail_ms));
    }
    set_zone1(false);
    set_zone2(false);

    std::cout << "[spray] done\n";
    return true;
}
