#ifndef GPIOBUTTONREADER_H
#define GPIOBUTTONREADER_H

#include <QMetaObject>
#include <atomic>
#include <thread>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include "VehicleModel.h"

// Monitors a GPIO pin via sysfs and calls VehicleModel::toggleHazard() on
// each debounced falling edge (active-low button press).
// Requires the sysfs GPIO interface (/sys/class/gpio/) to be available.
// NOTE: GPIO 14 (BCM) is also UART TX — ensure serial console is disabled in /boot/config.txt.
class GpioButtonReader {
public:
    explicit GpioButtonReader(VehicleModel* model, int gpioPin = 14)
        : m_model(model), m_gpioPin(gpioPin), m_running(false) {}

    ~GpioButtonReader() { stop(); }

    void start() {
        bool expected = false;
        if (!m_running.compare_exchange_strong(expected, true)) return;
        m_thread = std::thread(&GpioButtonReader::readLoop, this);
    }

    void stop() {
        m_running.store(false, std::memory_order_release);
        if (m_thread.joinable()) m_thread.join();
    }

private:
    VehicleModel*      m_model;
    int                m_gpioPin;
    std::atomic<bool>  m_running;
    std::thread        m_thread;

    bool exportGpio() {
        char buf[64];

        // Export pin (ignore EBUSY — already exported is fine)
        FILE* f = fopen("/sys/class/gpio/export", "w");
        if (f) { fprintf(f, "%d", m_gpioPin); fclose(f); }
        usleep(100000); // 100 ms for kernel to create the sysfs entry

        snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%d/direction", m_gpioPin);
        f = fopen(buf, "w");
        if (!f) return false;
        fputs("in", f); fclose(f);

        snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%d/edge", m_gpioPin);
        f = fopen(buf, "w");
        if (!f) return false;
        fputs("falling", f); fclose(f);

        return true;
    }

    void readLoop() {
        if (!exportGpio()) {
            m_running.store(false, std::memory_order_relaxed);
            return;
        }

        char buf[64];
        snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%d/value", m_gpioPin);
        int fd = open(buf, O_RDONLY);
        if (fd < 0) {
            m_running.store(false, std::memory_order_relaxed);
            return;
        }

        // Discard the initial edge that fires when the fd is first opened
        char val[4];
        read(fd, val, sizeof(val));

        int64_t lastPressMs = 0;

        while (m_running.load(std::memory_order_acquire)) {
            struct pollfd pfd;
            pfd.fd     = fd;
            pfd.events = POLLPRI | POLLERR;
            pfd.revents = 0;

            if (poll(&pfd, 1, 200) <= 0) continue; // timeout → re-check m_running

            lseek(fd, 0, SEEK_SET);
            char c = '1';
            read(fd, &c, 1);

            // 50 ms debounce
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            const int64_t nowMs = static_cast<int64_t>(ts.tv_sec) * 1000
                                  + ts.tv_nsec / 1000000;
            if (nowMs - lastPressMs < 50) continue;
            lastPressMs = nowMs;

            if (c == '0') { // active-low: '0' = button pressed
                QMetaObject::invokeMethod(m_model, "toggleHazard", Qt::QueuedConnection);
            }
        }

        close(fd);
    }
};

#endif
