#include "capture_round_log.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace {

bool ensure_header(std::ofstream& ofs, const std::string& csv_path) {
    bool write_header = false;
    std::error_code ec;
    if (!std::filesystem::exists(csv_path, ec)) {
        write_header = true;
    } else if (std::filesystem::is_regular_file(csv_path, ec) &&
               std::filesystem::file_size(csv_path, ec) == 0) {
        write_header = true;
    }

    if (write_header) {
        ofs << "ts_utc,round_id,lux_avg,lux_valid,env_valid,temperature_c,pressure_hpa,humidity_pct,fill_on,shots_planned\n";
    }
    return true;
}

}  // namespace

void capture_append_round_csv(const std::string& csv_path,
                              const std::string& round_id,
                              const std::string& ts_utc,
                              double lux_avg,
                              bool lux_valid,
                              const EnvSnapshot& env,
                              bool fill_on,
                              int shots_planned) {
    std::ofstream ofs(csv_path, std::ios::app);
    if (!ofs.is_open()) {
        std::cerr << "[csv] open failed: " << csv_path << "\n";
        return;
    }

    ensure_header(ofs, csv_path);

    ofs << ts_utc << ","
        << round_id << ","
        << std::fixed << std::setprecision(4) << lux_avg << ","
        << (lux_valid ? 1 : 0) << ","
        << (env.valid ? 1 : 0) << ","
        << std::fixed << std::setprecision(4) << env.temperature_c << ","
        << std::fixed << std::setprecision(4) << env.pressure_hpa << ","
        << std::fixed << std::setprecision(4) << env.humidity_pct << ","
        << (fill_on ? 1 : 0) << ","
        << shots_planned << "\n";
}
