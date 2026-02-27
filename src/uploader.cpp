#include "uploader.hpp"

#include <cstdlib>
#include <iostream>

// ====== 修改这里 ======

// Storage Account + Container
static const std::string AZURE_BASE =
    "https://imagest5.blob.core.windows.net/imaget5/";

// Container SAS Token（不要带 ?，不要带 <>）
static const std::string AZURE_SAS =
    "sp=racwdli&st=2026-02-09T11:06:25Z&se=2026-03-14T19:21:25Z&"
    "sv=2024-11-04&sr=c&sig=JKAn4oJHDYHd8tygGmbwahNFlsw30IDUwE99aeoXhj0%3D";
    

// ======================

bool upload_to_azure(const std::string& local_file,
                     const std::string& remote_name)
{
    // 拼完整 URL
    std::string url = AZURE_BASE + remote_name + "?" + AZURE_SAS;

    // curl 命令
    std::string cmd =
        "curl -s -X PUT -T \"" + local_file + "\" "
        "-H \"x-ms-blob-type: BlockBlob\" "
        "\"" + url + "\"";

    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        std::cerr << "[uploader] curl failed, ret=" << ret << "\n";
        return false;
    }

    return true;
}
