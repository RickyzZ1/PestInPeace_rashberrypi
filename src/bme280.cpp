#include "bme280.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <linux/i2c-dev.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

// BME280 driver notes:
// - auto-discovers bus/address (env override first, then common defaults)
// - reads and stores factory calibration coefficients once at init
// - uses Bosch integer compensation formulas for T/P/H conversion
namespace {

static constexpr uint8_t DEFAULT_ADDR1 = 0x76;
static constexpr uint8_t DEFAULT_ADDR2 = 0x77;
static constexpr const char* DEFAULT_I2C_DEV = "/dev/i2c-1";

static constexpr uint8_t REG_ID = 0xD0;
static constexpr uint8_t REG_RESET = 0xE0;
static constexpr uint8_t REG_CTRL_HUM = 0xF2;
static constexpr uint8_t REG_STATUS = 0xF3;
static constexpr uint8_t REG_CTRL_MEAS = 0xF4;
static constexpr uint8_t REG_CONFIG = 0xF5;
static constexpr uint8_t REG_PRESS_MSB = 0xF7;

static int g_fd = -1;
static std::string g_i2c_dev;
static uint8_t g_addr = 0;

struct CalibData {
    uint16_t T1 = 0;
    int16_t T2 = 0;
    int16_t T3 = 0;
    uint16_t P1 = 0;
    int16_t P2 = 0;
    int16_t P3 = 0;
    int16_t P4 = 0;
    int16_t P5 = 0;
    int16_t P6 = 0;
    int16_t P7 = 0;
    int16_t P8 = 0;
    int16_t P9 = 0;
    uint8_t H1 = 0;
    int16_t H2 = 0;
    uint8_t H3 = 0;
    int16_t H4 = 0;
    int16_t H5 = 0;
    int8_t H6 = 0;
};

static CalibData g_calib;
static int32_t g_t_fine = 0;

static bool read_bytes_fd(int fd, uint8_t start_reg, uint8_t* buf, std::size_t len) {
    // I2C repeated-read pattern: set start register pointer, then burst-read bytes.
    if (write(fd, &start_reg, 1) != 1) return false;
    return read(fd, buf, len) == static_cast<ssize_t>(len);
}

static bool write_reg_fd(int fd, uint8_t reg, uint8_t val) {
    uint8_t data[2] = {reg, val};
    return write(fd, data, 2) == 2;
}

static uint16_t u16_le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

static int16_t s16_le(const uint8_t* p) {
    return static_cast<int16_t>(u16_le(p));
}

static std::vector<std::string> collect_i2c_candidates() {
    std::vector<std::string> out;
    auto add_unique = [&](const std::string& dev) {
        if (dev.empty()) return;
        if (std::find(out.begin(), out.end(), dev) == out.end()) {
            out.push_back(dev);
        }
    };

    const char* env_dev = std::getenv("BME280_I2C_DEV");
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

static std::vector<uint8_t> collect_addr_candidates() {
    std::vector<uint8_t> out;

    const char* env_addr = std::getenv("BME280_I2C_ADDR");
    if (env_addr) {
        char* endp = nullptr;
        const unsigned long v = std::strtoul(env_addr, &endp, 0);
        if (endp != env_addr && *endp == '\0' && v <= 0x7F) {
            out.push_back(static_cast<uint8_t>(v));
        }
    }

    if (std::find(out.begin(), out.end(), DEFAULT_ADDR1) == out.end()) out.push_back(DEFAULT_ADDR1);
    if (std::find(out.begin(), out.end(), DEFAULT_ADDR2) == out.end()) out.push_back(DEFAULT_ADDR2);
    return out;
}

static bool read_calibration(int fd, CalibData& c) {
    // Datasheet layout:
    // 0x88..0xA1 holds temp/pressure and H1, 0xE1..0xE7 holds remaining humidity coeffs.
    uint8_t b1[26] = {};
    uint8_t b2[7] = {};
    if (!read_bytes_fd(fd, 0x88, b1, sizeof(b1))) return false;
    if (!read_bytes_fd(fd, 0xE1, b2, sizeof(b2))) return false;

    c.T1 = u16_le(&b1[0]);
    c.T2 = s16_le(&b1[2]);
    c.T3 = s16_le(&b1[4]);
    c.P1 = u16_le(&b1[6]);
    c.P2 = s16_le(&b1[8]);
    c.P3 = s16_le(&b1[10]);
    c.P4 = s16_le(&b1[12]);
    c.P5 = s16_le(&b1[14]);
    c.P6 = s16_le(&b1[16]);
    c.P7 = s16_le(&b1[18]);
    c.P8 = s16_le(&b1[20]);
    c.P9 = s16_le(&b1[22]);
    c.H1 = b1[25];

    c.H2 = s16_le(&b2[0]);
    c.H3 = b2[2];
    // H4/H5 are packed across E4/E5/E6:
    // H4 uses E4 as msb and E5[3:0] as lsb nibble.
    // H5 uses E6 as msb and E5[7:4] as lsb nibble.
    c.H4 = static_cast<int16_t>((static_cast<int16_t>(b2[3]) << 4) | (b2[4] & 0x0F));
    c.H5 = static_cast<int16_t>((static_cast<int16_t>(b2[5]) << 4) | (b2[4] >> 4));
    c.H6 = static_cast<int8_t>(b2[6]);

    return true;
}

static bool try_init_one(const std::string& dev, uint8_t addr) {
    int fd = open(dev.c_str(), O_RDWR);
    if (fd < 0) {
        std::cerr << "[bme280] open " << dev << " failed: " << std::strerror(errno) << "\n";
        return false;
    }

    if (ioctl(fd, I2C_SLAVE, addr) < 0) {
        close(fd);
        return false;
    }

    uint8_t id = 0;
    if (!read_bytes_fd(fd, REG_ID, &id, 1) || id != 0x60) {
        close(fd);
        return false;
    }

    CalibData calib{};
    if (!read_calibration(fd, calib)) {
        close(fd);
        return false;
    }

    // Soft reset command from datasheet.
    if (!write_reg_fd(fd, REG_RESET, 0xB6)) {
        close(fd);
        return false;
    }
    usleep(3000);

    for (int i = 0; i < 20; ++i) {
        uint8_t st = 0;
        if (!read_bytes_fd(fd, REG_STATUS, &st, 1)) {
            close(fd);
            return false;
        }
        // STATUS[0]=1 while NVM calibration copy is running after reset.
        if ((st & 0x01) == 0) break;
        usleep(2000);
    }

    // Write humidity oversampling first. CTRL_MEAS write latches CTRL_HUM.
    if (!write_reg_fd(fd, REG_CTRL_HUM, 0x01)) {  // x1 humidity oversampling
        close(fd);
        return false;
    }
    if (!write_reg_fd(fd, REG_CTRL_MEAS, 0x27)) { // temp x1, press x1, normal mode
        close(fd);
        return false;
    }
    if (!write_reg_fd(fd, REG_CONFIG, 0xA0)) {    // standby and filter
        close(fd);
        return false;
    }

    g_fd = fd;
    g_i2c_dev = dev;
    g_addr = addr;
    g_calib = calib;
    std::cout << "[bme280] initialized on " << g_i2c_dev
              << ", addr=0x" << std::hex << static_cast<int>(g_addr) << std::dec << "\n";
    return true;
}

}  // namespace

bool bme280_init() {
    bme280_deinit();
    const auto devs = collect_i2c_candidates();
    const auto addrs = collect_addr_candidates();
    for (const auto& d : devs) {
        for (uint8_t a : addrs) {
            if (try_init_one(d, a)) return true;
        }
    }

    std::cerr << "[bme280] init failed on all buses/addresses.\n";
    return false;
}

void bme280_deinit() {
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
    g_i2c_dev.clear();
    g_addr = 0;
    g_t_fine = 0;
    g_calib = CalibData{};
}

bool bme280_read_env(double& temperature_c, double& humidity_percent, double& pressure_hpa) {
    if (g_fd < 0) return false;

    uint8_t data[8] = {};
    if (!read_bytes_fd(g_fd, REG_PRESS_MSB, data, sizeof(data))) return false;

    // Pressure and temperature are 20-bit unsigned raw values (msb, lsb, xlsb[7:4]).
    const int32_t adc_p = (static_cast<int32_t>(data[0]) << 12) |
                          (static_cast<int32_t>(data[1]) << 4) |
                          (static_cast<int32_t>(data[2]) >> 4);
    const int32_t adc_t = (static_cast<int32_t>(data[3]) << 12) |
                          (static_cast<int32_t>(data[4]) << 4) |
                          (static_cast<int32_t>(data[5]) >> 4);
    // Humidity is 16-bit unsigned (msb, lsb).
    const int32_t adc_h = (static_cast<int32_t>(data[6]) << 8) |
                          static_cast<int32_t>(data[7]);

    // Guard invalid raw sentinels.
    if (adc_t == 0x80000 || adc_p == 0x80000 || adc_h == 0x8000) return false;

    // Datasheet compensation formula (integer path) for temperature.
    int32_t var1 = ((((adc_t >> 3) - (static_cast<int32_t>(g_calib.T1) << 1))) *
                    static_cast<int32_t>(g_calib.T2)) >> 11;
    int32_t var2 = (((((adc_t >> 4) - static_cast<int32_t>(g_calib.T1)) *
                      ((adc_t >> 4) - static_cast<int32_t>(g_calib.T1))) >> 12) *
                    static_cast<int32_t>(g_calib.T3)) >> 14;

    g_t_fine = var1 + var2;
    const int32_t t = (g_t_fine * 5 + 128) >> 8;
    temperature_c = static_cast<double>(t) / 100.0;

    // Datasheet compensation formula (64-bit integer path) for pressure.
    int64_t v1 = static_cast<int64_t>(g_t_fine) - 128000;
    int64_t v2 = v1 * v1 * static_cast<int64_t>(g_calib.P6);
    v2 += (v1 * static_cast<int64_t>(g_calib.P5)) << 17;
    v2 += static_cast<int64_t>(g_calib.P4) << 35;
    v1 = ((v1 * v1 * static_cast<int64_t>(g_calib.P3)) >> 8) +
         ((v1 * static_cast<int64_t>(g_calib.P2)) << 12);
    v1 = (((static_cast<int64_t>(1) << 47) + v1) * static_cast<int64_t>(g_calib.P1)) >> 33;
    if (v1 == 0) return false;

    int64_t p = 1048576 - adc_p;
    p = (((p << 31) - v2) * 3125) / v1;
    v1 = (static_cast<int64_t>(g_calib.P9) * (p >> 13) * (p >> 13)) >> 25;
    v2 = (static_cast<int64_t>(g_calib.P8) * p) >> 19;
    p = ((p + v1 + v2) >> 8) + (static_cast<int64_t>(g_calib.P7) << 4);
    const double pressure_pa = static_cast<double>(p) / 256.0;
    pressure_hpa = pressure_pa / 100.0;

    // Datasheet compensation formula for relative humidity.
    int32_t h = g_t_fine - 76800;
    h = (((((adc_h << 14) - (static_cast<int32_t>(g_calib.H4) << 20) -
             (static_cast<int32_t>(g_calib.H5) * h)) +
            16384) >> 15) *
         (((((((h * static_cast<int32_t>(g_calib.H6)) >> 10) *
              (((h * static_cast<int32_t>(g_calib.H3)) >> 11) + 32768)) >> 10) +
            2097152) *
               static_cast<int32_t>(g_calib.H2) +
           8192) >>
          14));
    h = h - (((((h >> 15) * (h >> 15)) >> 7) * static_cast<int32_t>(g_calib.H1)) >> 4);
    h = std::clamp(h, 0, 419430400);
    humidity_percent = static_cast<double>(h >> 12) / 1024.0;

    return true;
}
