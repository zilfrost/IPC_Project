#pragma once

#include <atomic>
#include <thread>
#include <string>
#include <cstring>
#include <cerrno>

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

#include <QObject>
#include <QMetaObject>

#include "VehicleModel.h"

// ── GpsReader ──────────────────────────────────────────────────────────────
// Header-only background GPS thread.  Mirrors CanReader.h's pattern exactly:
//   • Opens /dev/serial0 (PL011 UART, freed from Bluetooth by disable-bt overlay)
//   • Configures 9600 baud 8N1 raw mode via termios
//   • Reads NMEA sentences line-by-line; parses $GPRMC only
//   • Posts setGpsPosition() to the GUI thread via QueuedConnection — never
//     calls VehicleModel methods directly from this background thread
//   • stop() uses atomic exchange(-1) to close the fd and break the read loop,
//     identical to CanReader's double-close prevention pattern
// ──────────────────────────────────────────────────────────────────────────

class GpsReader
{
public:
    GpsReader(VehicleModel* model, const std::string& device = "/dev/serial0")
        : m_model(model), m_device(device), m_fd(-1), m_running(false)
    {}

    ~GpsReader() { stop(); }

    void start()
    {
        m_running = true;
        m_thread  = std::thread(&GpsReader::readLoop, this);
    }

    void stop()
    {
        m_running = false;
        int fd = m_fd.exchange(-1);
        if (fd >= 0) ::close(fd);
        if (m_thread.joinable()) m_thread.join();
    }

private:
    // ── readLoop — runs on background thread ───────────────────────────────
    void readLoop()
    {
        int fd = ::open(m_device.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
        if (fd < 0) {
            // Device not present — exit silently; GPS simply never provides a fix.
            // The dashboard shows the default/saved position from disk.
            return;
        }
        m_fd = fd;

        // Configure PL011 UART: 9600 baud, 8N1, raw mode, no flow control
        struct termios tty;
        ::memset(&tty, 0, sizeof(tty));
        ::tcgetattr(fd, &tty);

        ::cfsetispeed(&tty, B9600);
        ::cfsetospeed(&tty, B9600);

        tty.c_cflag  = CS8 | CLOCAL | CREAD;  // 8 data bits, no modem ctrl, enable RX
        tty.c_iflag  = IGNPAR;                 // ignore parity errors
        tty.c_oflag  = 0;
        tty.c_lflag  = 0;                      // raw mode — no echo, no signals
        tty.c_cc[VMIN]  = 1;                   // block until at least 1 byte arrives
        tty.c_cc[VTIME] = 10;                  // 1.0 s read timeout (units = 0.1 s)

        ::tcflush(fd, TCIFLUSH);
        ::tcsetattr(fd, TCSANOW, &tty);

        // Switch to blocking mode now that termios is configured
        int flags = ::fcntl(fd, F_GETFL, 0);
        ::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

        // ── Main read loop ─────────────────────────────────────────────────
        char buf[256];
        int  pos = 0;

        while (m_running) {
            char    c;
            ssize_t n = ::read(m_fd.load(), &c, 1);

            if (n < 0) {
                if (errno == EBADF || errno == ENODEV) break;  // stop() closed fd
                if (errno == EINTR) continue;
                continue;
            }
            if (n == 0) continue;  // 1 s timeout — loop again

            if (c == '\n' || pos >= static_cast<int>(sizeof(buf)) - 1) {
                buf[pos] = '\0';
                if (pos > 0) parseLine(buf);
                pos = 0;
            } else if (c != '\r') {
                buf[pos++] = c;
            }
        }

        int fd2 = m_fd.exchange(-1);
        if (fd2 >= 0) ::close(fd2);
    }

    // ── parseLine — parses a single NMEA sentence ──────────────────────────
    // Only $GPRMC is processed; all others ($GPGSV, $GPVTG, $GNGGA …) are skipped.
    //
    // $GPRMC format:
    //   $GPRMC,hhmmss.ss,A,DDMM.MMMM,N,DDDMM.MMMM,E,speed,track,ddmmyy,,,A*hh
    //     [0]     [1]   [2]   [3]   [4]    [5]    [6]
    //   field[2] == 'A' → active fix;  'V' → void (no fix yet)
    void parseLine(const char* line)
    {
        if (::strncmp(line, "$GPRMC", 6) != 0) return;

        char  copy[256];
        ::strncpy(copy, line, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = '\0';

        const char* fields[15] = {};
        int         nFields    = 0;
        char*       tok        = ::strtok(copy, ",");

        while (tok && nFields < 15) {
            fields[nFields++] = tok;
            tok = ::strtok(nullptr, ",");
        }

        if (nFields < 7) return;
        if (!fields[2] || fields[2][0] != 'A') return;  // A = active fix; V = void

        double lat = nmeaToDecimal(fields[3], fields[4] ? fields[4][0] : 'N');
        double lon = nmeaToDecimal(fields[5], fields[6] ? fields[6][0] : 'E');

        if (lat == 0.0 && lon == 0.0) return;  // ignore null island

        // Post to GUI thread — NEVER call setGpsPosition directly from here
        VehicleModel* model = m_model;
        QMetaObject::invokeMethod(model,
            [model, lat, lon]() { model->setGpsPosition(lat, lon); },
            Qt::QueuedConnection);
    }

    // ── nmeaToDecimal — converts NMEA DDMM.MMMM to decimal degrees ─────────
    static double nmeaToDecimal(const char* nmea, char hemisphere)
    {
        if (!nmea || nmea[0] == '\0') return 0.0;

        double raw = ::atof(nmea);
        int    deg = static_cast<int>(raw / 100);
        double min = raw - deg * 100.0;
        double dd  = deg + min / 60.0;

        if (hemisphere == 'S' || hemisphere == 'W') dd = -dd;
        return dd;
    }

    // ── Members ────────────────────────────────────────────────────────────
    VehicleModel*     m_model;
    std::string       m_device;
    std::atomic<int>  m_fd;
    std::atomic<bool> m_running;
    std::thread       m_thread;
};
