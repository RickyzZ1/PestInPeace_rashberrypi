#pragma once
#include <string>

// 上传本地文件到 Azure Blob Storage
// local_file : 本地完整路径
// remote_name: Blob 中的文件名（如 photo_xxx.jpg）
bool upload_to_azure(const std::string& local_file,
                     const std::string& remote_name);
