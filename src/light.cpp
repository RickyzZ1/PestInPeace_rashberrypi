#include "light.hpp"

#include <cstdlib>
#include <gpiod.h>
#include <iostream>
#include <string>
#include <unistd.h>

static gpiod_chip* g_chip = nullptr;
static gpiod_line_request* g_led_req = nullptr;
static unsigned int g_led = 4;
static unsigned int g_strip = 18;
static bool g_strip_state_on = false;

static gpiod_line_request* request_out(unsigned int offset, bool active_low, const char* consumer) {
    gpiod_line_request* req = nullptr;
    gpiod_line_settings* settings = gpiod_line_settings_new();
    gpiod_line_config* config = nullptr;
    gpiod_request_config* rconf = nullptr;

    if (!settings) goto cleanup;

    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_active_low(settings, active_low);
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

static void set_line(gpiod_line_request* req, unsigned int offset, bool on) {
    if (!req) return;
    gpiod_line_value v = on ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE;
    (void)gpiod_line_request_set_value(req, offset, v);
}

static void set_strip_ws2812(bool on) {
    // Avoid repeated Python calls when state does not change.
    if (on == g_strip_state_on) return;

    const char* app_root = "/home/fiveguys/project/PestInPeace_rashberrypi-main";
    std::string cmd = std::string("cd ") + app_root +
                      " && .venv/bin/python3 scripts/ws2812_set.py --pin " +
                      std::to_string(g_strip) + (on ? " --on" : " --off");
    if (geteuid() != 0) {
        // rpi_ws281x needs /dev/mem access; non-root runs should use passwordless sudo.
        cmd = std::string("sudo -n /bin/bash -lc '") + cmd + "'";
    }
    // Delegate WS2812 timing-sensitive protocol to Python rpi_ws281x helper.
    const int rc = std::system(cmd.c_str());
    if (rc != 0) {
        std::cerr << "[light] ws2812 command failed, rc=" << rc << "\n";
        if (geteuid() != 0) {
            std::cerr << "[light] hint: this is usually sudo permission (/dev/mem), not wiring.\n";
        }
        return;
    }
    g_strip_state_on = on;
}

void light_deinit() {
    if (g_led_req) {
        gpiod_line_request_release(g_led_req);
        g_led_req = nullptr;
    }
    set_strip_ws2812(false);
    if (g_chip) {
        gpiod_chip_close(g_chip);
        g_chip = nullptr;
    }
}

bool light_init(unsigned int led_gpio, unsigned int strip_gpio) {
    g_led = led_gpio;
    g_strip = strip_gpio;

    g_chip = gpiod_chip_open("/dev/gpiochip0");
    if (!g_chip) {
        std::cerr << "gpiod_chip_open failed\n";
        return false;
    }

    // Board indicator LED: active-high GPIO line.
    g_led_req = request_out(g_led, false, "fill-led");
    if (!g_led_req) {
        std::cerr << "request LED failed (maybe busy)\n";
        light_deinit();
        return false;
    }

    // Default state: all fill lights off.
    set_line(g_led_req, g_led, false);
    set_strip_ws2812(false);
    return true;
}

void light_set_led(bool on) {
    set_line(g_led_req, g_led, on);
}

void light_set_relay(bool on) {
    // Backward-compatible API name; this now controls WS2812 strip state.
    set_strip_ws2812(on);
}

void light_set_fill(bool on) {
    light_set_led(on);
    light_set_relay(on);
}
