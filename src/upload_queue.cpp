#include "upload_queue.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

enum class TaskType {
    Telemetry,
    Predict,
};

struct QueueTask {
    std::uint64_t id = 0;
    TaskType type = TaskType::Telemetry;
    int attempts = 0;
    long long next_try_epoch = 0;

    TelemetryPayload telemetry{};
    std::string local_file;
    UploadMetadata metadata{};
};

std::mutex g_mu;
std::condition_variable g_cv;
std::vector<QueueTask> g_tasks;
std::thread g_worker;
std::atomic<bool> g_running{false};
std::string g_queue_file;
std::uint64_t g_next_id = 1;

long long now_epoch_sec() {
    return static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string sanitize_field(std::string s) {
    for (char& c : s) {
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    }
    return s;
}

std::vector<std::string> split_tab(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string part;
    while (std::getline(ss, part, '\t')) {
        out.push_back(part);
    }
    return out;
}

std::string type_to_string(TaskType t) {
    return (t == TaskType::Telemetry) ? "telemetry" : "predict";
}

bool parse_type(const std::string& s, TaskType& out) {
    if (s == "telemetry") {
        out = TaskType::Telemetry;
        return true;
    }
    if (s == "predict") {
        out = TaskType::Predict;
        return true;
    }
    return false;
}

void persist_locked() {
    if (g_queue_file.empty()) return;

    std::ofstream out(g_queue_file, std::ios::trunc);
    if (!out) {
        std::cerr << "[upload-queue] failed to open queue file for write: " << g_queue_file << "\n";
        return;
    }

    for (const auto& t : g_tasks) {
        out << t.id << "\t"
            << type_to_string(t.type) << "\t"
            << t.attempts << "\t"
            << t.next_try_epoch;

        if (t.type == TaskType::Telemetry) {
            out << "\t" << sanitize_field(t.telemetry.device_id)
                << "\t" << sanitize_field(t.telemetry.ts_utc)
                << "\t" << (t.telemetry.has_temperature ? 1 : 0)
                << "\t" << t.telemetry.temperature
                << "\t" << (t.telemetry.has_humidity ? 1 : 0)
                << "\t" << t.telemetry.humidity
                << "\t" << (t.telemetry.has_light ? 1 : 0)
                << "\t" << t.telemetry.light;
        } else {
            out << "\t" << sanitize_field(t.local_file)
                << "\t" << sanitize_field(t.metadata.round_id)
                << "\t" << sanitize_field(t.metadata.capture_id)
                << "\t" << sanitize_field(t.metadata.captured_at_utc)
                << "\t" << t.metadata.lux_avg
                << "\t" << (t.metadata.env_valid ? 1 : 0)
                << "\t" << t.metadata.temperature_c
                << "\t" << t.metadata.pressure_hpa
                << "\t" << t.metadata.humidity_pct;
        }

        out << "\n";
    }
}

void load_from_disk_locked() {
    g_tasks.clear();
    g_next_id = 1;

    if (g_queue_file.empty()) return;

    std::ifstream in(g_queue_file);
    if (!in) return;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const auto cols = split_tab(line);
        if (cols.size() < 4) continue;

        QueueTask t{};
        try {
            t.id = static_cast<std::uint64_t>(std::stoull(cols[0]));
            t.attempts = std::stoi(cols[2]);
            t.next_try_epoch = std::stoll(cols[3]);
        } catch (...) {
            continue;
        }

        if (!parse_type(cols[1], t.type)) continue;

        if (t.type == TaskType::Telemetry) {
            if (cols.size() < 12) continue;
            t.telemetry.device_id = cols[4];
            t.telemetry.ts_utc = cols[5];
            t.telemetry.has_temperature = (cols[6] == "1");
            t.telemetry.temperature = std::atof(cols[7].c_str());
            t.telemetry.has_humidity = (cols[8] == "1");
            t.telemetry.humidity = std::atof(cols[9].c_str());
            t.telemetry.has_light = (cols[10] == "1");
            t.telemetry.light = std::atof(cols[11].c_str());
        } else {
            if (cols.size() < 13) continue;
            t.local_file = cols[4];
            t.metadata.round_id = cols[5];
            t.metadata.capture_id = cols[6];
            t.metadata.captured_at_utc = cols[7];
            t.metadata.lux_avg = std::atof(cols[8].c_str());
            t.metadata.env_valid = (cols[9] == "1");
            t.metadata.temperature_c = std::atof(cols[10].c_str());
            t.metadata.pressure_hpa = std::atof(cols[11].c_str());
            t.metadata.humidity_pct = std::atof(cols[12].c_str());
        }

        g_next_id = std::max(g_next_id, t.id + 1);
        g_tasks.push_back(std::move(t));
    }
}

int backoff_seconds(int attempts) {
    int s = 5;
    for (int i = 0; i < attempts; ++i) {
        s = std::min(s * 2, 300);
    }
    return s;
}

void worker_loop() {
    while (g_running.load()) {
        QueueTask task{};
        bool has_task = false;

        {
            std::unique_lock<std::mutex> lk(g_mu);
            g_cv.wait_for(lk, std::chrono::seconds(2), [] {
                return !g_running.load() || !g_tasks.empty();
            });
            if (!g_running.load()) break;

            const long long now = now_epoch_sec();
            for (const auto& t : g_tasks) {
                if (t.next_try_epoch <= now) {
                    task = t;
                    has_task = true;
                    break;
                }
            }
        }

        if (!has_task) continue;

        bool ok = false;
        if (task.type == TaskType::Telemetry) {
            ok = upload_telemetry(task.telemetry);
        } else {
            int unused_count = -1;
            ok = upload_to_predict(task.local_file, task.metadata, &unused_count);
        }

        {
            std::lock_guard<std::mutex> lk(g_mu);
            auto it = std::find_if(g_tasks.begin(), g_tasks.end(), [&](const QueueTask& t) {
                return t.id == task.id;
            });
            if (it == g_tasks.end()) {
                continue;
            }

            if (ok) {
                std::cout << "[upload-queue] done id=" << it->id
                          << ", type=" << type_to_string(it->type)
                          << ", pending=" << (g_tasks.size() - 1) << "\n";
                g_tasks.erase(it);
            } else {
                it->attempts += 1;
                it->next_try_epoch = now_epoch_sec() + backoff_seconds(it->attempts);
                std::cerr << "[upload-queue] retry scheduled id=" << it->id
                          << ", type=" << type_to_string(it->type)
                          << ", attempts=" << it->attempts << "\n";
            }
            persist_locked();
        }
    }
}

} // namespace

bool upload_queue_init(const std::string& queue_file) {
    upload_queue_deinit();

    g_queue_file = queue_file;
    try {
        const std::filesystem::path p(queue_file);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
    } catch (const std::exception& e) {
        std::cerr << "[upload-queue] create_directories failed: " << e.what() << "\n";
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(g_mu);
        load_from_disk_locked();
        persist_locked();
    }

    g_running.store(true);
    g_worker = std::thread(worker_loop);
    std::cout << "[upload-queue] init ok, file=" << g_queue_file
              << ", pending=" << upload_queue_pending_count() << "\n";
    return true;
}

void upload_queue_deinit() {
    g_running.store(false);
    g_cv.notify_all();
    if (g_worker.joinable()) {
        g_worker.join();
    }

    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_queue_file.empty()) {
        persist_locked();
    }
}

void upload_queue_enqueue_telemetry(const TelemetryPayload& payload) {
    std::lock_guard<std::mutex> lk(g_mu);
    QueueTask t{};
    t.id = g_next_id++;
    t.type = TaskType::Telemetry;
    t.telemetry = payload;
    g_tasks.push_back(std::move(t));
    persist_locked();
    g_cv.notify_all();
}

void upload_queue_enqueue_predict(const std::string& local_file, const UploadMetadata& metadata) {
    std::lock_guard<std::mutex> lk(g_mu);
    QueueTask t{};
    t.id = g_next_id++;
    t.type = TaskType::Predict;
    t.local_file = local_file;
    t.metadata = metadata;
    g_tasks.push_back(std::move(t));
    persist_locked();
    g_cv.notify_all();
}

std::size_t upload_queue_pending_count() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_tasks.size();
}
