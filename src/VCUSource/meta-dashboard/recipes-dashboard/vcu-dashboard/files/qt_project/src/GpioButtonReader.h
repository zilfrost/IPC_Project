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
#include <dirent.h>
#include "VehicleModel.h"

// Monitors a GPIO pin via sysfs and calls VehicleModel::toggleHazard() on
// each debounced falling edge (active-low button press).
// NOTE: GPIO 14 (BCM) is also UART TX — ensure enable_uart=0 in /boot/config.txt.
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

    // On modern kernels the GPIO chip base is not 0 (Pi 4 = 512, Pi 5 varies).
    // Scan /sys/class/gpio/gpiochip* and pick the chip with the most lines
    // (= main BCM controller). BCM pin N maps to sysfs pin (base + N).
    int resolveSysfsPin(int bcmPin) {
        DIR* dir = opendir("/sys/class/gpio");
        if (!dir) return bcmPin; // fallback: old kernel with base 0

        int bestBase  = 0;
        int bestNgpio = 0;
        struct dirent* ent;

        while ((ent = readdir(dir)) != nullptr) {
            if (strncmp(ent->d_name, "gpiochip", 8) != 0) continue;

            char path[128];
            int base = 0, ngpio = 0;

            snprintf(path, sizeof(path), "/sys/class/gpio/%s/base", ent->d_name);
            FILE* f = fopen(path, "r");
            if (!f) continue;
            fscanf(f, "%d", &base);
            fclose(f);

            snprintf(path, sizeof(path), "/sys/class/gpio/%s/ngpio", ent->d_name);
            f = fopen(path, "r");
            if (!f) continue;
            fscanf(f, "%d", &ngpio);
            fclose(f);

            if (ngpio > bestNgpio) {
                bestNgpio = ngpio;
                bestBase  = base;
            }
        }
        closedir(dir);

        return bestBase + bcmPin;
    }

    bool exportGpio(int sysfsPin) {
        char buf[64];

        // Export pin (ignore EBUSY — already exported is fine)
        FILE* f = fopen("/sys/class/gpio/export", "w");
        if (f) { fprintf(f, "%d", sysfsPin); fclose(f); }
        usleep(100000); // 100 ms for kernel to create the sysfs entry

        snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%d/direction", sysfsPin);
        f = fopen(buf, "w");
        if (!f) return false;
        fputs("in", f); fclose(f);

        snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%d/edge", sysfsPin);
        f = fopen(buf, "w");
        if (!f) return false;
        fputs("falling", f); fclose(f);

        return true;
    }

    void readLoop() {
        const int sysfsPin = resolveSysfsPin(m_gpioPin);

        if (!exportGpio(sysfsPin)) {
            m_running.store(false, std::memory_order_relaxed);
            return;
        }

        char buf[64];
        snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%d/value", sysfsPin);
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
