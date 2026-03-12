#pragma once

#include <cstdint>
#include <string>

bool mcp3008_init(const std::string& dev = "/dev/spidev0.0", uint32_t speed_hz = 1350000);
void mcp3008_deinit();
bool mcp3008_read_channel(int channel, int& value);
