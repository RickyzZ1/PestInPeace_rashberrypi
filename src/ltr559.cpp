#include "ltr559.hpp"

#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

// ===== I2C / LTR559 =====
static const char* I2C_DEV = "/dev/i2c-1";
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

static bool write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return write(g_fd, buf, 2) == 2;
}

static bool read_reg(uint8_t reg, uint8_t &val) {
    if (write(g_fd, &reg, 1) != 1) return false;
    return read(g_fd, &val, 1) == 1;
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

bool ltr559_init() {
    g_fd = open(I2C_DEV, O_RDWR);
    if (g_fd < 0) {
        perror("open i2c");
        return false;
    }

    if (ioctl(g_fd, I2C_SLAVE, LTR559_ADDR) < 0) {
        perror("ioctl I2C_SLAVE");
        close(g_fd);
        g_fd = -1;
        return false;
    }

    uint8_t part = 0;
    if (read_reg(REG_PART_ID, part)) {
        std::cout << "PART_ID=0x" << std::hex << (int)part << std::dec << "\n";
    } else {
        std::cerr << "Read PART_ID failed\n";
    }

    if (!write_reg(REG_ALS_CONTR, 0x01)) return false;      // ALS enable
    if (!write_reg(REG_ALS_MEAS_RATE, 0x03)) return false;  // 测量速率
    if (!write_reg(REG_PS_CONTR, 0x03)) return false;       // PS enable

    usleep(100000); // 100ms稳定时间
    return true;
}

void ltr559_deinit() {
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
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
