#pragma once
#include <string>
#include <cstddef>   // for std::size_t

bool capture_init(const std::string& out_dir,
                  int interval_sec,
                  int width,
                  int height,
                  int shots_per_round,
                  int lux_samples,
                  int sample_gap_ms,
                  double lux_threshold,
                  int light_warmup_ms,
                  int retain_days,
                  std::size_t max_dir_bytes);

void capture_poll();
void capture_deinit();
