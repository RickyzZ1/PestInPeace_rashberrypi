#pragma once

#include <cstddef>
#include <string>

#include "uploader.hpp"

bool upload_queue_init(const std::string& queue_file);
void upload_queue_deinit();

void upload_queue_enqueue_telemetry(const TelemetryPayload& payload);
void upload_queue_enqueue_predict(const std::string& local_file, const UploadMetadata& metadata);

std::size_t upload_queue_pending_count();
