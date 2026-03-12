#include "mcp3008.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/spi/spidev.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

int g_fd = -1;
uint32_t g_speed_hz = 1350000;
uint8_t g_mode = SPI_MODE_0;
uint8_t g_bits = 8;
std::string g_dev;

}  // namespace

bool mcp3008_init(const std::string& dev, uint32_t speed_hz) {
    mcp3008_deinit();

    int fd = open(dev.c_str(), O_RDWR);
    if (fd < 0) {
        std::cerr << "[mcp3008] open failed: " << dev << ", err=" << std::strerror(errno) << "\n";
        return false;
    }

    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = speed_hz;

    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0 ||
        ioctl(fd, SPI_IOC_RD_MODE, &mode) < 0 ||
        ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits) < 0 ||
        ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed) < 0) {
        std::cerr << "[mcp3008] ioctl config failed, err=" << std::strerror(errno) << "\n";
        close(fd);
        return false;
    }

    g_fd = fd;
    g_speed_hz = speed;
    g_mode = mode;
    g_bits = bits;
    g_dev = dev;

    std::cout << "[mcp3008] initialized on " << g_dev
              << ", mode=" << static_cast<int>(g_mode)
              << ", bits=" << static_cast<int>(g_bits)
              << ", speed_hz=" << g_speed_hz << "\n";
    return true;
}

void mcp3008_deinit() {
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
    g_dev.clear();
}

bool mcp3008_read_channel(int channel, int& value) {
    value = 0;
    if (g_fd < 0) return false;
    if (channel < 0 || channel > 7) return false;

    // MCP3008 single-ended read frame:
    // byte0=start(1), byte1=single(1)+channel(3), byte2=dummy.
    uint8_t tx[3] = {
        0x01,
        static_cast<uint8_t>(0x80 | ((channel & 0x07) << 4)),
        0x00
    };
    uint8_t rx[3] = {0, 0, 0};

    spi_ioc_transfer tr{};
    tr.tx_buf = reinterpret_cast<unsigned long>(tx);
    tr.rx_buf = reinterpret_cast<unsigned long>(rx);
    tr.len = 3;
    tr.speed_hz = g_speed_hz;
    tr.bits_per_word = g_bits;

    if (ioctl(g_fd, SPI_IOC_MESSAGE(1), &tr) < 1) {
        std::cerr << "[mcp3008] SPI transfer failed, err=" << std::strerror(errno) << "\n";
        return false;
    }

    // 10-bit ADC result is spread across rx[1:2].
    value = ((rx[1] & 0x03) << 8) | rx[2];
    return true;
}
