#ifndef CANREADER_H
#define CANREADER_H

#include <QObject>
#include <QString>
#include <QMetaObject>
#include <QDebug>
#include <atomic>
#include <array>
#include <cstdint>
#include <thread>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <string.h>
#include <ctime>
#include "VehicleModel.h"

// Converts a YYMMDDHHMM tuple (YY = years since 2000) to a Unix epoch (UTC).
// Self-contained — no OS timezone dependency.
static inline uint32_t yymmddhhmm_to_epoch(uint8_t yy, uint8_t mm, uint8_t dd,
                                            uint8_t hh, uint8_t mn)
{
    const uint16_t year = 2000U + yy;
    const uint8_t days_in_month[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
#define IS_LEAP(y) (((y)%4==0) && (((y)%100!=0) || ((y)%400==0)))
    uint32_t days = 0;
    for (uint16_t y = 1970; y < year; ++y)
        days += IS_LEAP(y) ? 366U : 365U;
    for (uint8_t m = 1; m < mm; ++m) {
        days += days_in_month[m - 1];
        if (m == 2 && IS_LEAP(year)) days++;
    }
    days += (uint32_t)(dd - 1U);
#undef IS_LEAP
    return days * 86400UL + (uint32_t)hh * 3600UL + (uint32_t)mn * 60UL;
}

class CanReader {
public:
    explicit CanReader(VehicleModel* model, const QString& interface = "vcan0")
        : m_model(model),
          m_interface(interface),
          m_running(false),
          m_socket(-1),
          m_dirtyMask(0),
          m_flushQueued(false),
          m_speed(0),
          m_batterySoC(0),
          m_rpm(0),
          m_temperature(20),
          m_gearIndex(0),
          m_timestamp(0),
          m_tripRaw(0),
          m_modeIndex(0),
          m_leftSignal(false),
          m_rightSignal(false),
          m_connectionLost(false) {}

    ~CanReader() {
        stop();
    }

    void start() {
        bool expected = false;
        // [RACE FIX #1] Atomic state transition prevents concurrent start/stop races.
        if (!m_running.compare_exchange_strong(expected, true)) return;
        m_dirtyMask.store(0);
        m_flushQueued.store(false);
        m_connectionLost.store(false);

        if (m_thread.joinable()) {
            m_thread.join();
        }

        m_thread = std::thread(&CanReader::readLoop, this);
    }

    void stop() {
        // [RACE FIX #2] Release store is visible to readLoop()'s acquire load across threads.
        m_running.store(false, std::memory_order_release);

        // [RACE FIX #3] Atomically take ownership of socket fd before closing.
        // This prevents double-close between stop() and readLoop() teardown.
        const int socketFd = m_socket.exchange(-1);
        if (socketFd >= 0) {
            close(socketFd);
        }

        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

private:
    enum DirtyBits : uint32_t {
        DirtySpeed          = 1u << 0,
        DirtyBatterySoC     = 1u << 1,
        DirtyRpm            = 1u << 2,
        DirtyTemperature    = 1u << 3,
        DirtyGear           = 1u << 4,
        DirtyTimestamp      = 1u << 5,
        DirtyTrip           = 1u << 6,
        DirtyMode           = 1u << 7,
        DirtyLeftSignal     = 1u << 8,
        DirtyRightSignal    = 1u << 9,
        DirtyConnectionLost = 1u << 10,
        DirtyNode1HB        = 1u << 11,
        DirtyNode2HB        = 1u << 12,
        DirtyNode3HB        = 1u << 13
    };

    VehicleModel* m_model;
    QString m_interface;
    // [RACE FIX #4] Shared between GUI thread and CAN thread -> must be atomic.
    std::atomic<bool> m_running;
    // [RACE FIX #5] Shared socket handle for coordinated shutdown.
    std::atomic<int> m_socket;
    std::thread m_thread;

    // Coalesced update state (latest value wins) to avoid flooding GUI event queue.
    std::atomic<uint32_t> m_dirtyMask;
    std::atomic<bool> m_flushQueued;

    std::atomic<int>      m_speed;
    std::atomic<int>      m_batterySoC;
    std::atomic<int>      m_rpm;
    std::atomic<int>      m_temperature;
    std::atomic<int>      m_gearIndex;
    std::atomic<uint32_t> m_timestamp;
    std::atomic<int>      m_tripRaw;
    std::atomic<int>      m_modeIndex;
    std::atomic<bool>     m_leftSignal;
    std::atomic<bool>     m_rightSignal;
    std::atomic<bool>     m_connectionLost;
    std::atomic<bool>     m_encoderOk{false};
    std::atomic<bool>     m_socOk{false};
    std::atomic<bool>     m_gearOk{false};
    std::atomic<bool>     m_modeOk{false};
    std::atomic<bool>     m_bmp180Ok{false};
    std::atomic<bool>     m_ds3231Ok{false};
    std::atomic<bool>     m_signalOk{false};

    void queueModelFlush() {
        bool expected = false;
        // acq_rel: success synchronizes with the release store in the flush lambda;
        // acquire on failure observes the prior successful CAS.
        if (!m_flushQueued.compare_exchange_strong(expected, true,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            return;
        }

        QMetaObject::invokeMethod(m_model, [this]() {
            static const char* gears[] = {"P", "R", "N", "D", "D1", "D2", "D3"};
            static const char* modes[] = {"ECO", "NORMAL", "SPORT"};

            while (true) {
                // acquire: synchronizes-with every publishDirty fetch_or(release),
                // making all preceding relaxed data stores visible to this thread.
                const uint32_t dirty = m_dirtyMask.exchange(0, std::memory_order_acquire);
                if (dirty == 0) {
                    break;
                }

                if (dirty & DirtySpeed) {
                    m_model->setSpeed(m_speed.load(std::memory_order_relaxed));
                }
                if (dirty & DirtyBatterySoC) {
                    m_model->setBatterySoC(m_batterySoC.load(std::memory_order_relaxed));
                }
                if (dirty & DirtyRpm) {
                    m_model->setRpm(m_rpm.load(std::memory_order_relaxed));
                }
                if (dirty & DirtyTemperature) {
                    m_model->setTemperature(m_temperature.load(std::memory_order_relaxed));
                }
                if (dirty & DirtyGear) {
                    const int gearIndex = m_gearIndex.load(std::memory_order_relaxed);
                    if (gearIndex >= 0 && gearIndex < 7) {
                        m_model->setGear(QString::fromLatin1(gears[gearIndex]));
                    }
                }
                if (dirty & DirtyTimestamp) {
                    m_model->setSystemTimestamp(m_timestamp.load(std::memory_order_relaxed));
                }
                if (dirty & DirtyTrip) {
                    m_model->processRawDistance(
                        static_cast<double>(m_tripRaw.load(std::memory_order_relaxed)) / 10.0);
                }
                if (dirty & DirtyMode) {
                    const int modeIndex = m_modeIndex.load(std::memory_order_relaxed);
                    if (modeIndex >= 0 && modeIndex < 3) {
                        m_model->setMode(QString::fromLatin1(modes[modeIndex]));
                    }
                }
                if (dirty & DirtyLeftSignal) {
                    m_model->setLeftSignal(m_leftSignal.load(std::memory_order_relaxed));
                }
                if (dirty & DirtyRightSignal) {
                    m_model->setRightSignal(m_rightSignal.load(std::memory_order_relaxed));
                }
                if (dirty & DirtyConnectionLost) {
                    m_model->setConnectionLost(m_connectionLost.load(std::memory_order_relaxed));
                }
                if (dirty & DirtyNode1HB) {
                    m_model->setNode1Heartbeat(m_encoderOk.load(std::memory_order_relaxed));
                }
                if (dirty & DirtyNode2HB) {
                    m_model->setNode2Heartbeat(
                        m_socOk.load(std::memory_order_relaxed),
                        m_gearOk.load(std::memory_order_relaxed),
                        m_modeOk.load(std::memory_order_relaxed));
                }
                if (dirty & DirtyNode3HB) {
                    m_model->setNode3Heartbeat(
                        m_bmp180Ok.load(std::memory_order_relaxed),
                        m_ds3231Ok.load(std::memory_order_relaxed),
                        m_signalOk.load(std::memory_order_relaxed));
                }
            }

            // release: re-arms the flush gate; CAN thread CAS will observe this.
            m_flushQueued.store(false, std::memory_order_release);
            // acquire: catches any bits the CAN thread set between our last exchange
            // and the store above (the window where its CAS would have failed).
            if (m_dirtyMask.load(std::memory_order_acquire) != 0u) {
                queueModelFlush();
            }
        }, Qt::QueuedConnection);
    }

    inline void publishDirty(uint32_t bits) {
        // release: makes all preceding relaxed data stores visible to any thread
        // that subsequently performs an acquire load/exchange on m_dirtyMask.
        m_dirtyMask.fetch_or(bits, std::memory_order_release);
        queueModelFlush();
    }

    inline void setConnectionLostState(bool lost) {
        // relaxed: only the CAN thread writes m_connectionLost; the subsequent
        // publishDirty fetch_or(release) propagates the new value to the GUI thread.
        if (m_connectionLost.exchange(lost, std::memory_order_relaxed) != lost) {
            publishDirty(DirtyConnectionLost);
        }
    }

    void handleFrame(const struct can_frame& frame) {
        const canid_t canId = frame.can_id & CAN_SFF_MASK;

        switch (canId) {
            case 0x100: {
                if (__builtin_expect(frame.can_dlc < 1, 0)) {
                    qWarning() << "Ignoring speed frame with invalid DLC:" << frame.can_dlc;
                    break;
                }
                // 16-bit big-endian speed, compatible with legacy 1-byte payloads.
                const int speed = (frame.can_dlc >= 2)
                        ? (((int)frame.data[0] << 8) | (int)frame.data[1])
                        : (int)frame.data[0];
                m_speed.store(speed, std::memory_order_relaxed);
                publishDirty(DirtySpeed);
                break;
            }
            case 0x101: {
                if (__builtin_expect(frame.can_dlc < 2, 0)) {
                    qWarning() << "Ignoring RPM frame with invalid DLC:" << frame.can_dlc;
                    break;
                }
                const int rpm = ((int)frame.data[0] << 8) | (int)frame.data[1];
                m_rpm.store(rpm, std::memory_order_relaxed);
                publishDirty(DirtyRpm);
                break;
            }
            case 0x102: {
                if (__builtin_expect(frame.can_dlc < 1, 0)) {
                    qWarning() << "Ignoring SoC frame with invalid DLC:" << frame.can_dlc;
                    break;
                }
                m_batterySoC.store((int)frame.data[0], std::memory_order_relaxed);
                publishDirty(DirtyBatterySoC);
                break;
            }
            case 0x103: {
                if (__builtin_expect(frame.can_dlc < 1, 0)) {
                    qWarning() << "Ignoring temperature frame with invalid DLC:" << frame.can_dlc;
                    break;
                }
                m_temperature.store((int)(signed char)frame.data[0], std::memory_order_relaxed);
                publishDirty(DirtyTemperature);
                break;
            }
            case 0x104: {
                if (__builtin_expect(frame.can_dlc < 1, 0)) {
                    qWarning() << "Ignoring gear frame with invalid DLC:" << frame.can_dlc;
                    break;
                }
                if (frame.data[0] < 7) {
                    m_gearIndex.store((int)frame.data[0], std::memory_order_relaxed);
                    publishDirty(DirtyGear);
                }
                break;
            }
            case 0x105: {
                if (__builtin_expect(frame.can_dlc < 5, 0)) {
                    qWarning() << "Ignoring RTC datetime frame with invalid DLC:" << frame.can_dlc;
                    break;
                }
                // YYMMDDHHMM: byte[0]=YY byte[1]=MM byte[2]=DD byte[3]=HH byte[4]=Min
                const uint32_t ts = yymmddhhmm_to_epoch(
                    frame.data[0], frame.data[1], frame.data[2],
                    frame.data[3], frame.data[4]);
                m_timestamp.store(ts, std::memory_order_relaxed);
                publishDirty(DirtyTimestamp);
                break;
            }
            case 0x106: {
                if (__builtin_expect(frame.can_dlc < 2, 0)) {
                    qWarning() << "Ignoring trip frame with invalid DLC:" << frame.can_dlc;
                    break;
                }
                const int rawTrip = ((int)frame.data[0] << 8) | (int)frame.data[1];
                m_tripRaw.store(rawTrip, std::memory_order_relaxed);
                publishDirty(DirtyTrip);
                break;
            }
            case 0x107: {
                if (__builtin_expect(frame.can_dlc < 1, 0)) {
                    qWarning() << "Ignoring turn signal frame with invalid DLC:" << frame.can_dlc;
                    break;
                }
                m_leftSignal.store( (frame.data[0] & 0x01) != 0, std::memory_order_relaxed);
                m_rightSignal.store((frame.data[0] & 0x02) != 0, std::memory_order_relaxed);
                publishDirty(DirtyLeftSignal | DirtyRightSignal);
                break;
            }
            case 0x108: {
                if (__builtin_expect(frame.can_dlc < 1, 0)) {
                    qWarning() << "Ignoring mode frame with invalid DLC:" << frame.can_dlc;
                    break;
                }
                if (frame.data[0] < 3) {
                    m_modeIndex.store((int)frame.data[0], std::memory_order_relaxed);
                    publishDirty(DirtyMode);
                }
                break;
            }
            case 0x1F1: {
                if (__builtin_expect(frame.can_dlc < 1, 0)) break;
                m_encoderOk.store(frame.data[0] == 0x01, std::memory_order_relaxed);
                publishDirty(DirtyNode1HB);
                break;
            }
            case 0x1F2: {
                if (__builtin_expect(frame.can_dlc < 3, 0)) break;
                m_socOk.store(frame.data[0]  == 0x01, std::memory_order_relaxed);
                m_gearOk.store(frame.data[1] == 0x01, std::memory_order_relaxed);
                m_modeOk.store(frame.data[2] == 0x01, std::memory_order_relaxed);
                publishDirty(DirtyNode2HB);
                break;
            }
            case 0x1F3: {
                if (__builtin_expect(frame.can_dlc < 3, 0)) break;
                m_bmp180Ok.store(frame.data[0] == 0x01, std::memory_order_relaxed);
                m_ds3231Ok.store(frame.data[1] == 0x01, std::memory_order_relaxed);
                m_signalOk.store(frame.data[2] == 0x01, std::memory_order_relaxed);
                publishDirty(DirtyNode3HB);
                break;
            }
            default:
                break;
        }
    }

    void readLoop() {
        int s;
        struct sockaddr_can addr;
        struct ifreq ifr;
        struct can_frame frame;

        // Open RAW CAN socket
        if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
            qWarning() << "Failed to open CAN socket:" << strerror(errno);
            m_running.store(false, std::memory_order_relaxed);
            return;
        }
        m_socket.store(s);

        // Receive only dashboard-related IDs to reduce userspace processing overhead.
        static const std::array<struct can_filter, 12> kFilters = {{
            {0x100, CAN_SFF_MASK},
            {0x101, CAN_SFF_MASK},
            {0x102, CAN_SFF_MASK},
            {0x103, CAN_SFF_MASK},
            {0x104, CAN_SFF_MASK},
            {0x105, CAN_SFF_MASK},
            {0x106, CAN_SFF_MASK},
            {0x107, CAN_SFF_MASK},
            {0x108, CAN_SFF_MASK},
            {0x1F1, CAN_SFF_MASK},
            {0x1F2, CAN_SFF_MASK},
            {0x1F3, CAN_SFF_MASK}
        }};
        if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, kFilters.data(),
                       static_cast<socklen_t>(kFilters.size() * sizeof(struct can_filter))) < 0) {
            qWarning() << "Failed to set CAN filters:" << strerror(errno);
        }

        const int socketFlags = fcntl(s, F_GETFL, 0);
        if (socketFlags >= 0) {
            if (fcntl(s, F_SETFL, socketFlags | O_NONBLOCK) < 0) {
                qWarning() << "Failed to set non-blocking CAN socket:" << strerror(errno);
            }
        } else {
            qWarning() << "Failed to query CAN socket flags:" << strerror(errno);
        }

        strncpy(ifr.ifr_name, m_interface.toUtf8().constData(), IFNAMSIZ - 1);
        ifr.ifr_name[IFNAMSIZ - 1] = '\0';

        if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
            qWarning() << "Failed to get interface index for" << m_interface << ":" << strerror(errno);
            close(s);
            m_socket.store(-1);
            m_running.store(false, std::memory_order_relaxed);
            return;
        }

        memset(&addr, 0, sizeof(addr));
        addr.can_family = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;

        if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            qWarning() << "Failed to bind CAN socket:" << strerror(errno);
            close(s);
            m_socket.store(-1);
            m_running.store(false, std::memory_order_relaxed);
            return;
        }

        int consecutivePollTimeouts = 0;
        bool timeoutStateActive = false;

        // Continuous data read loop (poll prevents indefinite blocking on read)
        // acquire: synchronizes-with stop()'s release store so exit is prompt.
        while (m_running.load(std::memory_order_acquire)) {
            struct pollfd pfd;
            pfd.fd = s;
            pfd.events = POLLIN;
            pfd.revents = 0;

            constexpr int kPollTimeoutMs = 100;
            constexpr int kConnectionLostMs = 1500;
            constexpr int kTimeoutThreshold = kConnectionLostMs / kPollTimeoutMs;
            const int pollResult = poll(&pfd, 1, kPollTimeoutMs);

            if (pollResult == 0) {
                ++consecutivePollTimeouts;
                if (consecutivePollTimeouts >= kTimeoutThreshold && !timeoutStateActive) {
                    timeoutStateActive = true;
                    m_speed.store(0, std::memory_order_relaxed);
                    m_rpm.store(0, std::memory_order_relaxed);
                    publishDirty(DirtySpeed | DirtyRpm);
                    setConnectionLostState(true);
                }
                continue; // timeout: re-check m_running
            }

            if (pollResult < 0) {
                if (errno == EINTR) continue;
                qWarning() << "poll() error:" << strerror(errno);
                break;
            }

            if ((pfd.revents & POLLIN) == 0) {
                if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    qWarning() << "CAN socket error: revents =" << pfd.revents;
                    break;
                }
                continue;
            }

            bool receivedValidFrame = false;
            // relaxed: same thread as the outer loop; sequential consistency within
            // the thread guarantees we see stop()'s write no later than the next
            // outer-loop acquire load, and a closed socket causes EAGAIN anyway.
            while (m_running.load(std::memory_order_relaxed)) {
                const int nbytes = read(s, &frame, sizeof(struct can_frame));
                if (nbytes == sizeof(struct can_frame)) {
                    receivedValidFrame = true;
                    handleFrame(frame);
                    continue;
                }

                if (nbytes < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    qWarning() << "read() error:" << strerror(errno);
                    m_running.store(false, std::memory_order_relaxed);
                    break;
                }

                if (nbytes > 0) {
                    qWarning() << "Partial CAN frame read:" << nbytes << "bytes";
                }
                break;
            }

            if (receivedValidFrame) {
                consecutivePollTimeouts = 0;
                if (timeoutStateActive) {
                    timeoutStateActive = false;
                }
                setConnectionLostState(false);
            }
        }
        // [RACE FIX #7] Same atomic close protocol as stop().
        const int socketFd = m_socket.exchange(-1);
        if (socketFd >= 0) {
            close(socketFd);
        }
        m_running.store(false, std::memory_order_relaxed);
    }
};

#endif
