#include "VehicleModel.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <cstdio>
#include <unistd.h>

static QString odoPath()    { return QDir::homePath() + QStringLiteral("/odometer.json"); }
static QString odoTmpPath() { return QDir::homePath() + QStringLiteral("/odometer.json.tmp"); }
static constexpr int kPeriodicSaveIntervalMs = 10 * 60 * 1000; // 10 minutes

// ── Construction / Destruction ─────────────────────────────────────────────

VehicleModel::VehicleModel(QObject* parent)
    : QObject(parent)
{
    m_odometer       = loadOdometerFromDisk();
    m_lastSavedOdoKm = static_cast<int>(m_odometer);

    m_saveTimer = new QTimer(this);
    m_saveTimer->setInterval(kPeriodicSaveIntervalMs);
    connect(m_saveTimer, &QTimer::timeout, this, [this]() {
        saveOdometerToDisk(m_odometer);
    });
    m_saveTimer->start();

    // ── Heartbeat watchdog timers (2 s per node) ──────────────────────────
    m_hb1Timer = new QTimer(this);
    m_hb1Timer->setSingleShot(true);
    m_hb1Timer->setInterval(3000);
    connect(m_hb1Timer, &QTimer::timeout, this, [this]() {
        if (m_node1Heartbeat) { m_node1Heartbeat = false; emit node1HeartbeatChanged(); }
    });

    m_hb2Timer = new QTimer(this);
    m_hb2Timer->setSingleShot(true);
    m_hb2Timer->setInterval(3000);
    connect(m_hb2Timer, &QTimer::timeout, this, [this]() {
        if (m_node2Heartbeat) { m_node2Heartbeat = false; emit node2HeartbeatChanged(); }
    });

    m_hb3Timer = new QTimer(this);
    m_hb3Timer->setSingleShot(true);
    m_hb3Timer->setInterval(3000);
    connect(m_hb3Timer, &QTimer::timeout, this, [this]() {
        if (m_node3Heartbeat) { m_node3Heartbeat = false; emit node3HeartbeatChanged(); }
    });

    m_hazardTimer = new QTimer(this);
    m_hazardTimer->setInterval(500);
    connect(m_hazardTimer, &QTimer::timeout, this, [this]() {
        m_hazardBlinkState = !m_hazardBlinkState;
        setLeftSignal(m_hazardBlinkState);
        setRightSignal(m_hazardBlinkState);
    });
}

VehicleModel::~VehicleModel()
{
    // Flush any un-saved fractional km on clean shutdown
    saveOdometerToDisk(m_odometer);
}

// ── Odometer Accumulation ──────────────────────────────────────────────────

void VehicleModel::processRawDistance(double rawDistance)
{
    if (rawDistance < 0.0) return;

    // 1. Update Trip directly from hardware
    if (m_tripDistance != rawDistance) {
        m_tripDistance = rawDistance;
        emit tripDistanceChanged();
    }

    // 2. Odometer Accumulation Logic
    if (m_lastRawDistance < 0.0) {
        // First value after power-on: establish baseline without crediting any
        // distance. Prevents false accumulation when VCU resumes with a non-zero
        // trip counter after an ignition cycle.
        m_lastRawDistance = rawDistance;
        return;
    }

    double delta = rawDistance - m_lastRawDistance;

    // Handle manual Trip Reset mid-drive (counter drops back toward 0)
    if (delta < 0.0) {
        delta = rawDistance;
    }

    if (delta > 0.0) {
        m_odometer += delta;

        if (static_cast<int>(m_odometer) > m_lastSavedOdoKm) {
            m_lastSavedOdoKm = static_cast<int>(m_odometer);
            saveOdometerToDisk(m_odometer);
            emit odometerChanged();
        }
    }

    m_lastRawDistance = rawDistance;
}

// ── Persistence ────────────────────────────────────────────────────────────

void VehicleModel::saveOdometerToDisk(double value)
{
    QJsonObject obj;
    obj[QStringLiteral("odometer")] = value;
    const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    // Write to a temp file, fsync, then atomically rename over the real file.
    // A crash between write and rename leaves the previous file intact.
    QFile tmp(odoTmpPath());
    if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "saveOdometerToDisk: cannot open temp file:" << tmp.errorString();
        return;
    }
    tmp.write(data);
    tmp.flush();
    ::fdatasync(tmp.handle()); // flush kernel buffers to SD card before rename
    tmp.close();

    if (std::rename(odoTmpPath().toLocal8Bit().constData(),
                    odoPath().toLocal8Bit().constData()) != 0) {
        qWarning() << "saveOdometerToDisk: atomic rename failed";
    }
}

double VehicleModel::loadOdometerFromDisk()
{
    QFile file(odoPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return 0.0; // First run or file not present — start from zero
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "loadOdometerFromDisk: JSON corrupted — resetting odometer to 0";
        return 0.0;
    }

    const QJsonValue val = doc.object().value(QStringLiteral("odometer"));
    if (!val.isDouble()) {
        qWarning() << "loadOdometerFromDisk: 'odometer' key missing or invalid — resetting to 0";
        return 0.0;
    }

    return qBound(0.0, val.toDouble(), 999999.0);
}

// ── Hazard Toggle ──────────────────────────────────────────────────────────

void VehicleModel::toggleHazard()
{
    m_hazard = !m_hazard;
    if (m_hazard) {
        m_hazardBlinkState = true;
        setLeftSignal(true);
        setRightSignal(true);
        m_hazardTimer->start();
    } else {
        m_hazardTimer->stop();
        setLeftSignal(false);
        setRightSignal(false);
    }
    emit hazardChanged();
}

// ── Heartbeat Setters ──────────────────────────────────────────────────────

void VehicleModel::setNode1Heartbeat(bool encoderOkVal)
{
    m_hb1Timer->start();  // restart 2 s watchdog
    if (!m_node1Heartbeat) { m_node1Heartbeat = true; emit node1HeartbeatChanged(); }
    if (m_encoderOk != encoderOkVal) { m_encoderOk = encoderOkVal; emit encoderOkChanged(); }
}

void VehicleModel::setNode2Heartbeat(bool socOkVal, bool gearOkVal, bool modeOkVal)
{
    m_hb2Timer->start();
    if (!m_node2Heartbeat) { m_node2Heartbeat = true; emit node2HeartbeatChanged(); }
    if (m_socOk  != socOkVal)  { m_socOk  = socOkVal;  emit socOkChanged(); }
    if (m_gearOk != gearOkVal) { m_gearOk = gearOkVal; emit gearOkChanged(); }
    if (m_modeOk != modeOkVal) { m_modeOk = modeOkVal; emit modeOkChanged(); }
}

void VehicleModel::setNode3Heartbeat(bool bmp180OkVal, bool ds3231OkVal, bool signalOkVal)
{
    m_hb3Timer->start();
    if (!m_node3Heartbeat) { m_node3Heartbeat = true; emit node3HeartbeatChanged(); }
    if (m_bmp180Ok != bmp180OkVal) { m_bmp180Ok = bmp180OkVal; emit bmp180OkChanged(); }
    if (m_ds3231Ok != ds3231OkVal) { m_ds3231Ok = ds3231OkVal; emit ds3231OkChanged(); }
    if (m_signalOk != signalOkVal) { m_signalOk = signalOkVal; emit signalOkChanged(); }
}

