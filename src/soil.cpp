#include "soil.hpp"

#include "mcp3008.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_channel = 0;
int g_adc_dry = 850; // Typical dry-point raw ADC (sensor in air).
int g_adc_wet = 350; // Typical wet-point raw ADC (sensor in water).
bool g_inited = false;

int env_int_or(const char* key, int fallback) {
    const char* v = std::getenv(key);
    if (!v || !*v) return fallback;
    char* endp = nullptr;
    const long n = std::strtol(v, &endp, 10);
    if (endp == v || *endp != '\0') return fallback;
    return static_cast<int>(n);
}

double raw_to_pct(int raw, int adc_dry, int adc_wet) {
    if (adc_dry == adc_wet) return 0.0;
    // Convert raw ADC into 0..100 moisture using two-point calibration.
    const double pct = (static_cast<double>(adc_dry - raw) * 100.0) /
                       static_cast<double>(adc_dry - adc_wet);
    return std::clamp(pct, 0.0, 100.0);
}

}  // namespace

bool soil_init(int channel) {
    g_channel = std::clamp(channel, 0, 7);

    g_channel = std::clamp(env_int_or("SOIL_ADC_CHANNEL", g_channel), 0, 7);
    // Optional runtime calibration overrides from environment.
    g_adc_dry = env_int_or("SOIL_ADC_DRY", g_adc_dry);
    g_adc_wet = env_int_or("SOIL_ADC_WET", g_adc_wet);

    const std::string spi_dev = std::getenv("MCP3008_SPI_DEV") ? std::getenv("MCP3008_SPI_DEV") : "/dev/spidev0.0";
    const int spi_speed = env_int_or("MCP3008_SPI_SPEED", 1350000);

    if (!mcp3008_init(spi_dev, static_cast<uint32_t>(std::max(100000, spi_speed)))) {
        std::cerr << "[soil] mcp3008_init failed\n";
        g_inited = false;
        return false;
    }

    g_inited = true;
    std::cout << "[soil] init ok, channel=" << g_channel
              << ", dry=" << g_adc_dry
              << ", wet=" << g_adc_wet << "\n";
    return true;
}

void soil_deinit() {
    mcp3008_deinit();
    g_inited = false;
}

bool soil_read_snapshot(SoilSnapshot& out) {
    out = SoilSnapshot{};
    if (!g_inited) return false;

    int raw = 0;
    if (!mcp3008_read_channel(g_channel, raw)) {
        return false;
    }

    out.valid = true;
    out.raw = raw;
    out.moisture_pct = raw_to_pct(raw, g_adc_dry, g_adc_wet);
    return true;
}
