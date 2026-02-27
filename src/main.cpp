#include "ltr559.hpp"
#include "light.hpp"
#include "camera.hpp"

#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

static volatile std::sig_atomic_t g_stop = 0;
static void on_sig(int) { g_stop = 1; }

int main() {
    std::signal(SIGINT, on_sig);
    std::signal(SIGTERM, on_sig);

    if (!ltr559_init()) {
        std::cerr << "ltr559_init failed\n";
        return 1;
    }

    if (!light_init(4, 26)) {
        std::cerr << "light_init failed (GPIO may be busy)\n";
        ltr559_deinit();
        return 1;
    }

    if (!capture_init("/home/fiveguys/projects/demo/captures",
                      20,    // 测试用20秒；正式部署建议7200（2小时）
                      2304, 1296,
                      5,      // 每轮5张
                      5,      // lux采样5次
                      100,    // 采样间隔100ms
                      0.8,    // 补光阈值
                      300,    // 开灯预热300ms
                      7,      // 保留天数
                      2ull * 1024ull * 1024ull * 1024ull)) { // 最大2GB
        std::cerr << "capture_init failed\n";
        light_deinit();
        ltr559_deinit();
        return 1;
    }

    while (!g_stop) {
        capture_poll();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    light_set_fill(false);
    capture_deinit();
    light_deinit();
    ltr559_deinit();

    std::cout << "Stopped.\n";
    return 0;
}


//make clean && make
//sudo ./iot_app

