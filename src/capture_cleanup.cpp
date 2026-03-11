#include "capture_cleanup.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool is_image_file(const std::filesystem::path& p) {
    auto ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return (ext == ".jpg" || ext == ".jpeg" || ext == ".png");
}

struct FileInfo {
    std::filesystem::path path;
    std::filesystem::file_time_type mtime;
    std::uintmax_t size = 0;
};

}  // namespace

void capture_cleanup_images(const std::string& out_dir, int retain_days, std::size_t max_dir_bytes) {
    namespace fs = std::filesystem;

    std::vector<FileInfo> files;
    std::size_t total_bytes = 0;

    try {
        for (const auto& entry : fs::directory_iterator(out_dir)) {
            if (!entry.is_regular_file()) continue;
            const auto& p = entry.path();
            if (!is_image_file(p)) continue;

            std::error_code ec1;
            std::error_code ec2;
            const auto sz_raw = fs::file_size(p, ec1);
            const auto mt = fs::last_write_time(p, ec2);
            if (ec1 || ec2) continue;

            const std::size_t sz = static_cast<std::size_t>(sz_raw);
            files.push_back(FileInfo{p, mt, sz});
            total_bytes += sz;
        }
    } catch (const std::exception& e) {
        std::cerr << "[cleanup] scan failed: " << e.what() << "\n";
        return;
    }

    if (files.empty()) return;

    const auto now_fs = fs::file_time_type::clock::now();
    const auto age_limit = std::chrono::hours(24 * retain_days);

    int removed_by_age = 0;
    for (const auto& f : files) {
        const auto age = now_fs - f.mtime;
        if (age > age_limit) {
            std::error_code ec;
            if (fs::remove(f.path, ec) && !ec) {
                total_bytes = (total_bytes > f.size) ? (total_bytes - f.size) : 0;
                ++removed_by_age;
                std::cout << "[cleanup] removed old: " << f.path.filename().string() << "\n";
            }
        }
    }

    files.clear();
    try {
        for (const auto& entry : fs::directory_iterator(out_dir)) {
            if (!entry.is_regular_file()) continue;
            const auto& p = entry.path();
            if (!is_image_file(p)) continue;

            std::error_code ec1;
            std::error_code ec2;
            const auto sz = fs::file_size(p, ec1);
            const auto mt = fs::last_write_time(p, ec2);
            if (ec1 || ec2) continue;

            files.push_back(FileInfo{p, mt, sz});
        }
    } catch (...) {
        return;
    }

    std::sort(files.begin(), files.end(),
              [](const FileInfo& a, const FileInfo& b) { return a.mtime < b.mtime; });

    total_bytes = 0;
    for (const auto& f : files) total_bytes += f.size;

    int removed_by_size = 0;
    for (const auto& f : files) {
        if (total_bytes <= max_dir_bytes) break;

        std::error_code ec;
        if (fs::remove(f.path, ec) && !ec) {
            total_bytes = (total_bytes > f.size) ? (total_bytes - f.size) : 0;
            ++removed_by_size;
            std::cout << "[cleanup] removed for size: " << f.path.filename().string() << "\n";
        }
    }

    if (removed_by_age > 0 || removed_by_size > 0) {
        std::cout << "[cleanup] done. removed(age=" << removed_by_age
                  << ", size=" << removed_by_size
                  << "), remain_bytes=" << total_bytes << "\n";
    }
}
