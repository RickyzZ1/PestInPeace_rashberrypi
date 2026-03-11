#pragma once

#include "capture_env.hpp"

#include <string>

void capture_append_round_csv(const std::string& csv_path,
                              const std::string& round_id,
                              const std::string& ts_utc,
                              double lux_avg,
                              bool lux_valid,
                              const EnvSnapshot& env,
                              bool fill_on,
                              int shots_planned);
