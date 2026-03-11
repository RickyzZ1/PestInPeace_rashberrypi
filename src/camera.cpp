#include "camera.hpp"
#include "uploader.hpp"
#include "light.hpp"
#include "capture_cleanup.hpp"
#include "capture_env.hpp"
#include "capture_lux.hpp"
#include "capture_round_log.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
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

static std::string now_utc_iso() {
    auto t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

static std::string make_round_id() {
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "round_" + std::to_string(now_ms);
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

void capture_poll() {
    if (!g_inited) return;

    const auto now = clk::now();
    if (now < g_next) return;

    std::cout << "\n=== Capture round started ===\n";

    // 1) 采样lux平均
    double avg_lux = 0.0;
    int ok_count = 0;
    bool lux_ok = capture_read_lux_avg(g_lux_samples, g_sample_gap_ms, avg_lux, ok_count);

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

    // 3) 每轮拍照前采一次环境，5张共享同一份快照
    const std::string round_id = make_round_id();
    const std::string round_ts_utc = now_utc_iso();
    EnvSnapshot env_snapshot{};
    env_snapshot.valid = capture_read_env_data(env_snapshot);
    capture_print_env_data(env_snapshot);
    std::cout << "[round] id=" << round_id << "\n";

    TelemetryPayload telemetry{};
    const char* dev_id = std::getenv("DEVICE_ID");
    telemetry.device_id = (dev_id && *dev_id) ? dev_id : "pi-001";
    telemetry.ts_utc = round_ts_utc;
    telemetry.has_light = lux_ok;
    telemetry.light = avg_lux;
    telemetry.has_temperature = env_snapshot.valid;
    telemetry.temperature = env_snapshot.temperature_c;
    telemetry.has_humidity = env_snapshot.valid;
    telemetry.humidity = env_snapshot.humidity_pct;
    // Local-only mode: disable telemetry upload to save Azure resources.
    // (void)upload_telemetry(telemetry);
    std::cout << "[telemetry] upload skipped (local test)\n";

    const std::string round_csv = g_out_dir + "/round_env_log.csv";
    capture_append_round_csv(round_csv, round_id, round_ts_utc, avg_lux, lux_ok,
                             env_snapshot, need_fill, g_shots_per_round);

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

        UploadMetadata meta{};
        meta.round_id = round_id;
        meta.capture_id = round_id + "_shot_" + std::to_string(i + 1);
        meta.captured_at_utc = now_utc_iso();
        meta.lux_avg = avg_lux;
        meta.env_valid = env_snapshot.valid;
        meta.temperature_c = env_snapshot.temperature_c;
        meta.pressure_hpa = env_snapshot.pressure_hpa;
        meta.humidity_pct = env_snapshot.humidity_pct;

        // Local-only mode: disable image inference upload to save Azure resources.
        // bool ok = upload_to_predict(file, meta);
        // if (ok) {
        //     ++uploaded_count;
        //     std::cout << "[shot " << (i + 1) << "] inferenced: " << file << "\n";
        // } else {
        //     std::cerr << "[shot " << (i + 1) << "] /predict failed: " << file << "\n";
        // }
        std::cout << "[shot " << (i + 1) << "] upload skipped (local test)\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // 4) 本轮结束关灯
    if (light_opened_by_this_round) {
        light_set_fill(false);
    }

    std::cout << "=== Capture round done: uploaded " << uploaded_count
              << "/" << g_shots_per_round << " ===\n";

    // 清理本地缓存（按天数+容量）
    capture_cleanup_images(g_out_dir, g_retain_days, g_max_dir_bytes);

    // 5) 下次触发时间（避免漂移）
    g_next += std::chrono::seconds(g_interval);
    if (g_next < clk::now()) {
        g_next = clk::now() + std::chrono::seconds(g_interval);
    }
}

void capture_deinit() {
    g_inited = false;
}
