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

// Upload a local image to the website project's /predict endpoint.
// local_file: absolute/relative image path.
bool upload_to_predict(const std::string& local_file, const UploadMetadata& metadata);

// Upload sensor telemetry to /telemetry endpoint.
bool upload_telemetry(const TelemetryPayload& payload);
