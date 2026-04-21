#pragma once

#include <string>

struct UploadMetadata {
    std::string round_id;
    std::string capture_id;
    std::string captured_at_utc;
    double lux_avg = 0.0;
    bool env_valid = false;
    double temperature_c = 0.0;
    double pressure_hpa = 0.0;
    double humidity_pct = 0.0;
};

struct TelemetryPayload {
    std::string device_id;
    std::string ts_utc;
    bool has_temperature = false;
    double temperature = 0.0;
    bool has_humidity = false;
    double humidity = 0.0;
    bool has_light = false;
    double light = 0.0;
};

struct WeeklyDecisionInput {
    int aphid_count = 0;
    double field_area_ha = 1.0;
    int exposure_days = 7;
    double t_mean = 15.0;
    double rh_mean = 70.0;
    int in_tepp_window = -1; // -1 means not provided
    int apps_so_far = 0;
    bool respect_compliance_gate = true;
};

struct WeeklyDecisionResult {
    bool ok = false;
    int scope_class = 0;
    std::string scope_name;
    std::string raw_json;
};

// Upload a local image to the website project's /predict endpoint.
// local_file: absolute/relative image path.
bool upload_to_predict(const std::string& local_file, const UploadMetadata& metadata, int* out_count = nullptr);

// Upload sensor telemetry to /telemetry endpoint.
bool upload_telemetry(const TelemetryPayload& payload);

// Call cloud weekly spray decision API and return parsed scope + full JSON.
bool fetch_weekly_decision(const WeeklyDecisionInput& input, WeeklyDecisionResult& out);
