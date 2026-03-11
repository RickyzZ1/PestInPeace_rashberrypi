#include "uploader.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>

namespace {

std::string trim(const std::string& s) {
    std::size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) {
        ++b;
    }
    std::size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
        --e;
    }
    return s.substr(b, e - b);
}

std::string get_env_or_default(const char* key, const std::string& fallback) {
    const char* v = std::getenv(key);
    if (!v) return fallback;
    std::string out = trim(v);
    return out.empty() ? fallback : out;
}

std::string shell_escape_single_quote(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    out.push_back('\'');
    for (char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('\'');
    return out;
}

std::string normalize_predict_url(const std::string& raw) {
    std::string u = trim(raw);
    while (!u.empty() && u.back() == '/') {
        u.pop_back();
    }

    if (u.size() >= 8 && u.compare(u.size() - 8, 8, "/predict") == 0) {
        return u;
    }
    if (u.find("/predict?") != std::string::npos) {
        return u;
    }
    return u + "/predict";
}

std::string normalize_telemetry_url(const std::string& raw) {
    std::string u = trim(raw);
    while (!u.empty() && u.back() == '/') {
        u.pop_back();
    }

    if (u.size() >= 10 && u.compare(u.size() - 10, 10, "/telemetry") == 0) {
        return u;
    }
    return u + "/telemetry";
}

std::string append_query(const std::string& url, const std::string& key, const std::string& value) {
    if (value.empty()) return url;
    const char sep = (url.find('?') == std::string::npos) ? '?' : '&';
    return url + sep + key + "=" + value;
}

std::string fmt_double(double v) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4) << v;
    return oss.str();
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

int extract_int_field(const std::string& json_body, const std::string& field, int fallback) {
    try {
        const std::regex re("\\\"" + field + "\\\"\\s*:\\s*([0-9]+)");
        std::smatch m;
        if (std::regex_search(json_body, m, re) && m.size() >= 2) {
            return std::stoi(m[1].str());
        }
    } catch (...) {
        return fallback;
    }
    return fallback;
}

std::string extract_string_field(const std::string& json_body, const std::string& field) {
    try {
        const std::regex re("\\\"" + field + "\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
        std::smatch m;
        if (std::regex_search(json_body, m, re) && m.size() >= 2) {
            return m[1].str();
        }
    } catch (...) {
        return "";
    }
    return "";
}

} // namespace

bool upload_to_predict(const std::string& local_file, const UploadMetadata& metadata) {
    std::string predict_url = normalize_predict_url(
        get_env_or_default("PREDICT_URL", "https://aca-aphid-yolo.jollystone-e01fd827.swedencentral.azurecontainerapps.io/predict")
    );

    predict_url = append_query(predict_url, "conf", get_env_or_default("PREDICT_CONF", "0.25"));
    predict_url = append_query(predict_url, "iou", get_env_or_default("PREDICT_IOU", "0.45"));
    predict_url = append_query(predict_url, "imgsz", get_env_or_default("PREDICT_IMGSZ", "640"));
    predict_url = append_query(predict_url, "max_det", get_env_or_default("PREDICT_MAX_DET", "1000"));

    const std::string timeout = get_env_or_default("PREDICT_TIMEOUT", "30");

    const std::string image_arg = "image=@" + local_file + ";type=image/jpeg";
    const std::string round_arg = "round_id=" + metadata.round_id;
    const std::string capture_arg = "capture_id=" + metadata.capture_id;
    const std::string ts_arg = "captured_at_utc=" + metadata.captured_at_utc;
    const std::string lux_arg = "lux_avg=" + fmt_double(metadata.lux_avg);
    const std::string env_valid_arg = std::string("env_valid=") + (metadata.env_valid ? "1" : "0");
    const std::string temp_arg = "temperature_c=" + fmt_double(metadata.temperature_c);
    const std::string pressure_arg = "pressure_hpa=" + fmt_double(metadata.pressure_hpa);
    const std::string humidity_arg = "humidity_pct=" + fmt_double(metadata.humidity_pct);

    std::string cmd =
        "curl -sS --connect-timeout 10 --max-time " + timeout +
        " -X POST" +
        " -F " + shell_escape_single_quote(image_arg) +
        " -F " + shell_escape_single_quote(round_arg) +
        " -F " + shell_escape_single_quote(capture_arg) +
        " -F " + shell_escape_single_quote(ts_arg) +
        " -F " + shell_escape_single_quote(lux_arg) +
        " -F " + shell_escape_single_quote(env_valid_arg) +
        " -F " + shell_escape_single_quote(temp_arg) +
        " -F " + shell_escape_single_quote(pressure_arg) +
        " -F " + shell_escape_single_quote(humidity_arg) +
        " " + shell_escape_single_quote(predict_url) +
        " -w '\\n%{http_code}'";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        std::cerr << "[uploader] popen failed for /predict call.\n";
        return false;
    }

    std::string output;
    std::array<char, 1024> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    int rc = pclose(pipe);
    if (rc != 0) {
        std::cerr << "[uploader] curl process failed, rc=" << rc << "\n";
        return false;
    }

    const std::size_t pos = output.rfind('\n');
    if (pos == std::string::npos || pos + 1 >= output.size()) {
        std::cerr << "[uploader] unexpected /predict response format.\n";
        return false;
    }

    const std::string body = output.substr(0, pos);
    const std::string status_str = output.substr(pos + 1);
    int status_code = 0;
    try {
        status_code = std::stoi(status_str);
    } catch (...) {
        std::cerr << "[uploader] invalid HTTP status from curl: " << status_str << "\n";
        return false;
    }

    if (status_code < 200 || status_code >= 300) {
        std::cerr << "[uploader] /predict HTTP " << status_code << "\n";
        if (!body.empty()) {
            std::cerr << "[uploader] body: " << body << "\n";
        }
        return false;
    }

    const int count = extract_int_field(body, "count", -1);
    const std::string req_id = extract_string_field(body, "request_id");
    std::cout << "[uploader] /predict ok"
              << ", request_id=" << (req_id.empty() ? "n/a" : req_id)
              << ", count=" << count
              << "\n";

    return true;
}

bool upload_telemetry(const TelemetryPayload& payload) {
    if (payload.device_id.empty()) {
        std::cerr << "[telemetry] device_id is empty\n";
        return false;
    }

    std::string telemetry_url = normalize_telemetry_url(
        get_env_or_default("TELEMETRY_URL",
                           "https://aca-aphid-yolo.jollystone-e01fd827.swedencentral.azurecontainerapps.io/telemetry")
    );
    const std::string timeout = get_env_or_default("TELEMETRY_TIMEOUT", "15");

    std::ostringstream json;
    json << "{"
         << "\"device_id\":\"" << json_escape(payload.device_id) << "\"";
    if (payload.has_temperature) {
        json << ",\"temperature\":" << fmt_double(payload.temperature);
    }
    if (payload.has_humidity) {
        json << ",\"humidity\":" << fmt_double(payload.humidity);
    }
    if (payload.has_light) {
        json << ",\"light\":" << fmt_double(payload.light);
    }
    if (!payload.ts_utc.empty()) {
        json << ",\"ts\":\"" << json_escape(payload.ts_utc) << "\"";
    }
    json << "}";

    std::string cmd =
        "curl -sS --connect-timeout 8 --max-time " + timeout +
        " -X POST" +
        " -H 'Content-Type: application/json'" +
        " --data " + shell_escape_single_quote(json.str()) +
        " " + shell_escape_single_quote(telemetry_url) +
        " -w '\\n%{http_code}'";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        std::cerr << "[telemetry] popen failed.\n";
        return false;
    }

    std::string output;
    std::array<char, 1024> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    const int rc = pclose(pipe);
    if (rc != 0) {
        std::cerr << "[telemetry] curl process failed, rc=" << rc << "\n";
        return false;
    }

    const std::size_t pos = output.rfind('\n');
    if (pos == std::string::npos || pos + 1 >= output.size()) {
        std::cerr << "[telemetry] unexpected response format.\n";
        return false;
    }

    const std::string body = output.substr(0, pos);
    const std::string status_str = output.substr(pos + 1);
    int status_code = 0;
    try {
        status_code = std::stoi(status_str);
    } catch (...) {
        std::cerr << "[telemetry] invalid HTTP status: " << status_str << "\n";
        return false;
    }

    if (status_code < 200 || status_code >= 300) {
        std::cerr << "[telemetry] HTTP " << status_code << "\n";
        if (!body.empty()) {
            std::cerr << "[telemetry] body: " << body << "\n";
        }
        return false;
    }

    std::cout << "[telemetry] ok, device_id=" << payload.device_id << "\n";
    return true;
}
