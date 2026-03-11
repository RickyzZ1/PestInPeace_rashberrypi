#include "ltr559.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <linux/i2c-dev.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

// ===== I2C / LTR559 =====
static const char* DEFAULT_I2C_DEV = "/dev/i2c-1";
static const uint8_t LTR559_ADDR = 0x23;

// regs
static const uint8_t REG_ALS_CONTR     = 0x80;
static const uint8_t REG_PS_CONTR      = 0x81;
static const uint8_t REG_ALS_MEAS_RATE = 0x85;
static const uint8_t REG_PART_ID       = 0x86;

static const uint8_t REG_ALS_CH1_0     = 0x88; // CH1 low
static const uint8_t REG_ALS_PS_STATUS = 0x8C;

static const uint8_t REG_PS_DATA_0     = 0x8D;
static const uint8_t REG_PS_DATA_1     = 0x8E;

static int g_fd = -1;
static std::string g_i2c_dev;

static bool write_reg_fd(int fd, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return write(fd, buf, 2) == 2;
}

static bool read_reg_fd(int fd, uint8_t reg, uint8_t& val) {
    if (write(fd, &reg, 1) != 1) return false;
    return read(fd, &val, 1) == 1;
}

static bool read_reg(uint8_t reg, uint8_t& val) {
    return read_reg_fd(g_fd, reg, val);
}

// read 4 ALS bytes in one sequence: 0x88..0x8B
static bool read_als_raw(uint16_t &ch0, uint16_t &ch1) {
    uint8_t reg = REG_ALS_CH1_0;
    if (write(g_fd, &reg, 1) != 1) return false;

    uint8_t data[4];
    if (read(g_fd, data, 4) != 4) return false;

    // CH1 first then CH0
    ch1 = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    ch0 = (uint16_t)data[2] | ((uint16_t)data[3] << 8);
    return true;
}

static bool read_ps_raw(uint16_t &ps) {
    uint8_t lo = 0, hi = 0;
    if (!read_reg(REG_PS_DATA_0, lo)) return false;
    if (!read_reg(REG_PS_DATA_1, hi)) return false;
    ps = ((uint16_t)hi << 8) | lo;
    ps &= 0x07FF;
    return true;
}

static std::vector<std::string> collect_i2c_candidates() {
    std::vector<std::string> out;
    auto add_unique = [&](const std::string& dev) {
        if (dev.empty()) return;
        if (std::find(out.begin(), out.end(), dev) == out.end()) {
            out.push_back(dev);
        }
    };

    const char* env_dev = std::getenv("LTR559_I2C_DEV");
    if (env_dev) add_unique(env_dev);
    add_unique(DEFAULT_I2C_DEV);

    try {
        for (const auto& entry : std::filesystem::directory_iterator("/dev")) {
            const auto name = entry.path().filename().string();
            if (name.rfind("i2c-", 0) == 0) {
                add_unique(entry.path().string());
            }
        }
    } catch (...) {
        // Keep existing candidates only.
    }
    return out;
}

static bool try_init_on_bus(const std::string& dev) {
    int fd = open(dev.c_str(), O_RDWR);
    if (fd < 0) {
        std::cerr << "open " << dev << " failed: " << std::strerror(errno) << "\n";
        return false;
    }

    if (ioctl(fd, I2C_SLAVE, LTR559_ADDR) < 0) {
        std::cerr << "ioctl I2C_SLAVE on " << dev << " failed: " << std::strerror(errno) << "\n";
        close(fd);
        return false;
    }

    uint8_t part = 0;
    if (read_reg_fd(fd, REG_PART_ID, part)) {
        std::cout << "[ltr559] bus=" << dev
                  << ", PART_ID=0x" << std::hex << (int)part << std::dec << "\n";
    } else {
        std::cerr << "[ltr559] bus=" << dev << ", read PART_ID failed\n";
    }

    if (!write_reg_fd(fd, REG_ALS_CONTR, 0x01)) {
        close(fd);
        return false;
    }
    if (!write_reg_fd(fd, REG_ALS_MEAS_RATE, 0x03)) {
        close(fd);
        return false;
    }
    if (!write_reg_fd(fd, REG_PS_CONTR, 0x03)) {
        close(fd);
        return false;
    }

    usleep(100000); // 100ms稳定时间
    g_fd = fd;
    g_i2c_dev = dev;
    std::cout << "[ltr559] initialized on " << g_i2c_dev << "\n";
    return true;
}

void ltr559_deinit() {
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
    g_i2c_dev.clear();
}

bool ltr559_init() {
    ltr559_deinit();
    const auto candidates = collect_i2c_candidates();
    for (const auto& dev : candidates) {
        if (try_init_on_bus(dev)) {
            return true;
        }
    }

    std::cerr << "[ltr559] init failed on all I2C buses.\n";
    return false;
}

bool ltr559_read_raw(uint16_t& ch0, uint16_t& ch1, uint16_t& ps, uint8_t& status) {
    if (g_fd < 0) return false;

    if (!read_reg(REG_ALS_PS_STATUS, status)) return false;
    if (!read_als_raw(ch0, ch1)) return false;
    if (!read_ps_raw(ps)) return false;

    return true;
}

double ltr559_estimate_lux(uint16_t ch0, uint16_t ch1) {
    if (ch0 == 0 && ch1 == 0) return 0.0;

    double ratio = (double)ch1 / (double)(ch0 + ch1);

    double lux = 0.0;
    if (ratio < 0.45) lux = 1.7743 * ch0 + 1.1059 * ch1;
    else if (ratio < 0.64) lux = 4.2785 * ch0 - 1.9548 * ch1;
    else if (ratio < 0.85) lux = 0.5926 * ch0 + 0.1185 * ch1;
    else lux = 0.0;

    return lux / 100.0;
}
