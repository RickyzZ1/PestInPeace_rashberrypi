#include "camera.hpp"
#include "uploader.hpp"
#include "light.hpp"
#include "capture_cleanup.hpp"
#include "capture_env.hpp"
#include "capture_lux.hpp"
#include "capture_round_log.hpp"
#include "soil.hpp"
#include "spray.hpp"
#include "upload_queue.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
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

static bool get_forced_pest_count(int& out_count) {
    const char* raw = std::getenv("FORCE_PEST_COUNT");
    if (!raw || !*raw) return false;

    char* end = nullptr;
    long v = std::strtol(raw, &end, 10);
    if (!end || *end != '\0' || v < 0) return false;

    out_count = static_cast<int>(v);
    return true;
}

static bool cloud_upload_enabled() {
    const char* raw = std::getenv("ENABLE_CLOUD_UPLOAD");
    if (!raw || !*raw) return true; // default: cloud mode
    return std::string(raw) != "0";
}

static int env_int_or(const char* key, int fallback) {
    const char* raw = std::getenv(key);
    if (!raw || !*raw) return fallback;
    char* end = nullptr;
    long v = std::strtol(raw, &end, 10);
    if (!end || *end != '\0') return fallback;
    return static_cast<int>(v);
}

static double env_double_or(const char* key, double fallback) {
    const char* raw = std::getenv(key);
    if (!raw || !*raw) return fallback;
    char* end = nullptr;
    double v = std::strtod(raw, &end);
    if (!end || *end != '\0') return fallback;
    return v;
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
    if (!upload_queue_init(g_out_dir + "/upload_queue.tsv")) {
        std::cerr << "[camera] upload_queue_init failed\n";
        g_inited = false;
        return false;
    }
    return true;
}

void capture_poll() {
    if (!g_inited) return;

    const auto now = clk::now();
    if (now < g_next) return;
    const bool cloud_on = cloud_upload_enabled();

    // Cloud mode backpressure:
    // Pause new capture rounds until upload queue is drained.
    if (cloud_on) {
        const std::size_t pending = upload_queue_pending_count();
        if (pending > 0) {
            std::cout << "[capture] pause, waiting upload queue drain, pending=" << pending << "\n";
            g_next = clk::now() + std::chrono::seconds(5);
            return;
        }
    }

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

    SoilSnapshot soil_snapshot{};
    soil_snapshot.valid = soil_read_snapshot(soil_snapshot);
    if (soil_snapshot.valid) {
        std::cout << std::fixed << std::setprecision(2)
                  << "[SOIL] raw=" << soil_snapshot.raw
                  << ", moisture=" << soil_snapshot.moisture_pct << " %\n"
                  << std::defaultfloat;
    } else {
        std::cerr << "[soil] read failed\n";
    }

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
    if (cloud_on) {
        upload_queue_enqueue_telemetry(telemetry);
        std::cout << "[telemetry] queued, pending=" << upload_queue_pending_count() << "\n";
    } else {
        std::cout << "[telemetry] upload skipped (local mode)\n";
    }

    const std::string round_csv = g_out_dir + "/round_env_log.csv";
    capture_append_round_csv(round_csv, round_id, round_ts_utc, avg_lux, lux_ok,
                             env_snapshot, soil_snapshot, need_fill, g_shots_per_round);

    int queued_count = 0;
    int infer_count_sum = 0;
    int infer_count_valid = 0;

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

        int shot_count = -1;
        bool ok = false;
        if (cloud_on) {
            upload_queue_enqueue_predict(file, meta);
            ok = true;
            ++queued_count;
            std::cout << "[shot " << (i + 1) << "] queued for /predict: " << file
                      << ", pending=" << upload_queue_pending_count() << "\n";
        } else {
            std::cout << "[shot " << (i + 1) << "] /predict skipped (local mode): " << file << "\n";
        }

        if (ok && shot_count >= 0) {
            infer_count_sum += shot_count;
            ++infer_count_valid;
            std::cout << "[shot " << (i + 1) << "] inferenced: " << file
                      << ", count=" << shot_count << "\n";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // 4) 本轮结束关灯
    if (light_opened_by_this_round) {
        light_set_fill(false);
    }

    std::cout << "=== Capture round done: queued " << queued_count
              << "/" << g_shots_per_round << " ===\n";

    int forced_pest_count = 0;
    if (get_forced_pest_count(forced_pest_count)) {
        infer_count_sum = forced_pest_count;
        infer_count_valid = 1;
        std::cout << "[pest] local override FORCE_PEST_COUNT=" << forced_pest_count << "\n";
    }

    if (cloud_on) {
        int decision_aphid_count = env_int_or("DECISION_APHID_COUNT", 0);
        if (infer_count_valid > 0) {
            const double pest_pressure =
                static_cast<double>(infer_count_sum) / static_cast<double>(infer_count_valid);
            decision_aphid_count = static_cast<int>(std::lround(pest_pressure));
        }
        if (forced_pest_count > 0) {
            decision_aphid_count = forced_pest_count;
        }

        WeeklyDecisionInput in{};
        in.aphid_count = std::max(0, decision_aphid_count);
        in.field_area_ha = env_double_or("DECISION_FIELD_AREA_HA", 1.0);
        in.exposure_days = env_int_or("DECISION_EXPOSURE_DAYS", 7);
        in.in_tepp_window = env_int_or("DECISION_IN_TEPP_WINDOW", -1);
        in.apps_so_far = env_int_or("DECISION_APPS_SO_FAR", 0);
        in.respect_compliance_gate = env_int_or("DECISION_RESPECT_GATE", 1) != 0;
        in.t_mean = env_snapshot.valid ? env_snapshot.temperature_c : env_double_or("DECISION_T_MEAN", 15.0);
        in.rh_mean = env_snapshot.valid ? env_snapshot.humidity_pct : env_double_or("DECISION_RH_MEAN", 70.0);

        WeeklyDecisionResult decision{};
        if (fetch_weekly_decision(in, decision)) {
            std::cout << "[decision] raw: " << decision.raw_json << "\n";
            (void)spray_apply_for_scope(decision.scope_class);
        } else {
            std::cerr << "[decision] fetch failed, fallback to local pressure logic\n";
            if (infer_count_valid > 0) {
                const double pest_pressure =
                    static_cast<double>(infer_count_sum) / static_cast<double>(infer_count_valid);
                std::cout << "[pest] avg_count=" << pest_pressure
                          << " (" << infer_count_valid << " valid shots)\n";
                (void)spray_apply_for_pressure(pest_pressure);
            } else {
                std::cout << "[pest] no valid infer counts, skip spraying\n";
            }
        }
    } else {
        if (infer_count_valid > 0) {
            const double pest_pressure =
                static_cast<double>(infer_count_sum) / static_cast<double>(infer_count_valid);
            std::cout << "[pest] avg_count=" << pest_pressure
                      << " (" << infer_count_valid << " valid shots)\n";
            (void)spray_apply_for_pressure(pest_pressure);
        } else {
            std::cout << "[pest] no valid infer counts, skip spraying\n";
        }
    }

    // 清理本地缓存（按天数+容量）
    capture_cleanup_images(g_out_dir, g_retain_days, g_max_dir_bytes);

    // 5) 下次触发时间（避免漂移）
    g_next += std::chrono::seconds(g_interval);
    if (g_next < clk::now()) {
        g_next = clk::now() + std::chrono::seconds(g_interval);
    }
}

void capture_deinit() {
    upload_queue_deinit();
    g_inited = false;
}
