#include "camera.hpp"
#include "uploader.hpp"
#include "ltr559.hpp"
#include "light.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <ctime>

static int g_retain_days = 7;
static std::size_t g_max_dir_bytes = 2ull * 1024ull * 1024ull * 1024ull;

using clk = std::chrono::steady_clock;

// setup
static std::string g_out_dir;
static int g_interval = 7200;
static int g_w = 2304, g_h = 1296;
static int g_shots_per_round = 5;
static int g_lux_samples = 5;
static int g_sample_gap_ms = 100;
static double g_lux_threshold = 0.8;
static int g_light_warmup_ms = 300;

static clk::time_point g_next;
static bool g_inited = false;

static std::string now_ts() {
    auto t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return oss.str();
}

static bool read_lux_once(double& lux_out) {
    uint16_t ch0 = 0, ch1 = 0, ps = 0;
    uint8_t st = 0;
    if (!ltr559_read_raw(ch0, ch1, ps, st)) return false;
    lux_out = ltr559_estimate_lux(ch0, ch1);
    return true;
}

static bool read_lux_avg(int n, int gap_ms, double& avg_lux, int& ok_count) {
    std::vector<double> vals;
    vals.reserve(n);

    for (int i = 0; i < n; ++i) {
        double lux = 0.0;
        if (read_lux_once(lux)) {
            vals.push_back(lux);
        }
        if (i != n - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(gap_ms));
        }
    }

    ok_count = static_cast<int>(vals.size());
    if (vals.empty()) return false;

    double sum = std::accumulate(vals.begin(), vals.end(), 0.0);
    avg_lux = sum / vals.size();
    return true;
}

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
                  std::size_t max_dir_bytes){
    g_retain_days = retain_days;
    g_max_dir_bytes = max_dir_bytes;
    g_out_dir = out_dir;
    g_interval = interval_sec;
    g_w = width;
    g_h = height;
    g_shots_per_round = shots_per_round;
    g_lux_samples = lux_samples;
    g_sample_gap_ms = sample_gap_ms;
    g_lux_threshold = lux_threshold;
    g_light_warmup_ms = light_warmup_ms;

    try {
        std::filesystem::create_directories(g_out_dir);
    } catch (const std::exception& e) {
        std::cerr << "[camera] create_directories failed: " << e.what() << "\n";
        return false;
    }

    g_next = clk::now(); // immediate first round
    g_inited = true;
    return true;
}

static bool is_image_file(const std::filesystem::path& p) {
    auto ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return (ext == ".jpg" || ext == ".jpeg" || ext == ".png");
}

struct FileInfo {
    std::filesystem::path path;
    std::filesystem::file_time_type mtime;
    std::uintmax_t size = 0;
};

static void cleanup_captures() {
    namespace fs = std::filesystem;

    std::vector<FileInfo> files;
    std::size_t total_bytes = 0;


    // 收集文件信息
    try {
        for (const auto& entry : fs::directory_iterator(g_out_dir)) {
            if (!entry.is_regular_file()) continue;
            const auto& p = entry.path();
            if (!is_image_file(p)) continue;

            std::error_code ec1, ec2;
            auto sz_raw = fs::file_size(p, ec1);   // uintmax_t
            auto mt = fs::last_write_time(p, ec2);
            if (ec1 || ec2) continue;

            std::size_t sz = static_cast<std::size_t>(sz_raw);

            files.push_back(FileInfo{p, mt, sz});
            total_bytes += sz;
        }
    } catch (const std::exception& e) {
        std::cerr << "[cleanup] scan failed: " << e.what() << "\n";
        return;
    }


    if (files.empty()) return;

    // -------- A. 按保留天数删除 --------
    // 把 "现在-保留天数" 转成 filesystem 时钟近似阈值
    auto now_sys = std::chrono::system_clock::now();
    auto cutoff_sys = now_sys - std::chrono::hours(24 * g_retain_days);

    // C++17下 file_time_type 与 system_clock 转换麻烦，采用“相对比较”法：
    auto now_fs = fs::file_time_type::clock::now();
    auto age_limit = std::chrono::hours(24 * g_retain_days);

    int removed_by_age = 0;
    for (const auto& f : files) {
        auto age = now_fs - f.mtime;
        if (age > age_limit) {
            std::error_code ec;
            if (fs::remove(f.path, ec) && !ec) {
                total_bytes = (total_bytes > f.size) ? (total_bytes - f.size) : 0;
                ++removed_by_age;
                std::cout << "[cleanup] removed old: " << f.path.filename().string() << "\n";
            }
        }
    }

    // 重新收集一次（因为上面删过）
    files.clear();
    try {
        for (const auto& entry : fs::directory_iterator(g_out_dir)) {
            if (!entry.is_regular_file()) continue;
            const auto& p = entry.path();
            if (!is_image_file(p)) continue;

            std::error_code ec1, ec2;
            auto sz = fs::file_size(p, ec1);
            auto mt = fs::last_write_time(p, ec2);
            if (ec1 || ec2) continue;

            files.push_back(FileInfo{p, mt, sz});
        }
    } catch (...) {
        return;
    }

    // 按修改时间从旧到新排序
    std::sort(files.begin(), files.end(),
              [](const FileInfo& a, const FileInfo& b) { return a.mtime < b.mtime; });

    // 重算总大小
    total_bytes = 0;
    for (const auto& f : files) total_bytes += f.size;

    // -------- B. 按目录大小上限删除（从最老开始）--------
    int removed_by_size = 0;
    for (const auto& f : files) {
        if (total_bytes <= g_max_dir_bytes) break;

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


void capture_poll() {
    if (!g_inited) return;

    const auto now = clk::now();
    if (now < g_next) return;

    std::cout << "\n=== Capture round started ===\n";

    // 1) 采样lux平均
    double avg_lux = 0.0;
    int ok_count = 0;
    bool lux_ok = read_lux_avg(g_lux_samples, g_sample_gap_ms, avg_lux, ok_count);

    bool need_fill = false;
    if (lux_ok) {
        need_fill = (avg_lux < g_lux_threshold);
        std::cout << "[lux] avg=" << avg_lux
                  << " (" << ok_count << "/" << g_lux_samples << " valid)"
                  << ", threshold=" << g_lux_threshold
                  << ", fill=" << (need_fill ? "ON" : "OFF") << "\n";
    } else {
        // 传感器异常时兜底开灯，保证拍摄可见性
        std::cerr << "[lux] all samples failed, fallback fill=ON\n";
        need_fill = true;
    }

    // 2) 开灯（如需要）
    bool light_opened_by_this_round = false;
    if (need_fill) {
        light_set_fill(true);
        light_opened_by_this_round = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(g_light_warmup_ms));
    }

    // 3) 拍5张并上传
    int uploaded_count = 0;

    for (int i = 0; i < g_shots_per_round; ++i) {
        std::string file = g_out_dir + "/photo_" + now_ts() + "_" + std::to_string(i + 1) + ".jpg";

        std::string cmd = "rpicam-still --immediate "
                          "--width " + std::to_string(g_w) +
                          " --height " + std::to_string(g_h) +
                          " -o \"" + file + "\"";

        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            std::cerr << "[shot " << (i + 1) << "] capture failed, ret=" << ret << "\n";
            continue;
        }

        std::cout << "[shot " << (i + 1) << "] saved: " << file << "\n";

        std::string remote = std::filesystem::path(file).filename().string();
        bool ok = upload_to_azure(file, remote);
        if (ok) {
            ++uploaded_count;
            std::cout << "[shot " << (i + 1) << "] uploaded: " << remote << "\n";
        } else {
            std::cerr << "[shot " << (i + 1) << "] upload failed: " << remote << "\n";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // 4) 本轮结束关灯
    if (light_opened_by_this_round) {
        light_set_fill(false);
    }

    std::cout << "=== Capture round done: uploaded " << uploaded_count
              << "/" << g_shots_per_round << " ===\n";

    // 清理本地缓存（按天数+容量）
    cleanup_captures();

    // 5) 下次触发时间（避免漂移）
    g_next += std::chrono::seconds(g_interval);
    if (g_next < clk::now()) {
        g_next = clk::now() + std::chrono::seconds(g_interval);
    }
}

void capture_deinit() {
    g_inited = false;
}




