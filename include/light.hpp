#pragma once
#include <cstdint>

bool light_init(unsigned int led_gpio, unsigned int relay_gpio);
void light_deinit();

void light_set_led(bool on);
void light_set_relay(bool on);
void light_set_fill(bool on);

