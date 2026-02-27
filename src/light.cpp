#include "light.hpp"

#include <gpiod.h>
#include <iostream>

static gpiod_chip* g_chip = nullptr;
static gpiod_line_request* g_led_req = nullptr;
static gpiod_line_request* g_relay_req = nullptr;
static unsigned int g_led = 4;
static unsigned int g_relay = 26;

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

void light_deinit() {
    if (g_led_req) {
        gpiod_line_request_release(g_led_req);
        g_led_req = nullptr;
    }
    if (g_relay_req) {
        gpiod_line_request_release(g_relay_req);
        g_relay_req = nullptr;
    }
    if (g_chip) {
        gpiod_chip_close(g_chip);
        g_chip = nullptr;
    }
}

bool light_init(unsigned int led_gpio, unsigned int relay_gpio) {
    g_led = led_gpio;
    g_relay = relay_gpio;

    g_chip = gpiod_chip_open("/dev/gpiochip0");
    if (!g_chip) {
        std::cerr << "gpiod_chip_open failed\n";
        return false;
    }

    // LED：高电平亮
    g_led_req = request_out(g_led, false, "fill-led");
    if (!g_led_req) {
        std::cerr << "request LED failed (maybe busy)\n";
        light_deinit();
        return false;
    }

    // Relay：低电平触发 => active_low=true
    g_relay_req = request_out(g_relay, true, "fill-relay");
    if (!g_relay_req) {
        std::cerr << "request RELAY failed (maybe busy)\n";
        light_deinit();
        return false;
    }

    // 默认关闭
    set_line(g_led_req, g_led, false);
    set_line(g_relay_req, g_relay, false);
    return true;
}

void light_set_led(bool on) {
    set_line(g_led_req, g_led, on);
}

void light_set_relay(bool on) {
    set_line(g_relay_req, g_relay, on);
}

void light_set_fill(bool on) {
    light_set_led(on);
    light_set_relay(on);
}

