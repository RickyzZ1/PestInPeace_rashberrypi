#include "capture_lux.hpp"

#include "ltr559.hpp"

#include <chrono>
#include <cstdint>
#include <numeric>
#include <thread>
#include <vector>

namespace {

bool read_lux_once(double& lux_out) {
    uint16_t ch0 = 0;
    uint16_t ch1 = 0;
    uint16_t ps = 0;
    uint8_t st = 0;
    if (!ltr559_read_raw(ch0, ch1, ps, st)) return false;
    lux_out = ltr559_estimate_lux(ch0, ch1);
    return true;
}

}  // namespace

bool capture_read_lux_avg(int sample_count, int gap_ms, double& avg_lux, int& ok_count) {
    std::vector<double> vals;
    vals.reserve(sample_count);

    for (int i = 0; i < sample_count; ++i) {
        double lux = 0.0;
        if (read_lux_once(lux)) {
            vals.push_back(lux);
        }
        if (i != sample_count - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(gap_ms));
        }
    }

    ok_count = static_cast<int>(vals.size());
    if (vals.empty()) return false;

    const double sum = std::accumulate(vals.begin(), vals.end(), 0.0);
    avg_lux = sum / vals.size();
    return true;
}
