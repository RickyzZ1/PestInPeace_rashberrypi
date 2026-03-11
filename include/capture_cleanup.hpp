#pragma once

#include <cstddef>
#include <string>

void capture_cleanup_images(const std::string& out_dir, int retain_days, std::size_t max_dir_bytes);
