#ifndef VEHICLEMODEL_H
#define VEHICLEMODEL_H

#include <QObject>
#include <QString>
#include <QTimer>

class VehicleModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(int     speed           READ speed           WRITE setSpeed           NOTIFY speedChanged)
    Q_PROPERTY(int     batterySoC      READ batterySoC      WRITE setBatterySoC      NOTIFY batterySoCChanged)
    Q_PROPERTY(int     rpm             READ rpm             WRITE setRpm             NOTIFY rpmChanged)
    Q_PROPERTY(int     temperature     READ temperature     WRITE setTemperature     NOTIFY temperatureChanged)
    Q_PROPERTY(QString gear            READ gear            WRITE setGear            NOTIFY gearChanged)
    Q_PROPERTY(int     odometer        READ odometer                                 NOTIFY odometerChanged)
    Q_PROPERTY(double  tripDistance    READ tripDistance    WRITE processRawDistance NOTIFY tripDistanceChanged)
    Q_PROPERTY(QString mode            READ mode            WRITE setMode            NOTIFY modeChanged)
    Q_PROPERTY(bool    leftSignal      READ leftSignal      WRITE setLeftSignal      NOTIFY leftSignalChanged)
    Q_PROPERTY(bool    rightSignal     READ rightSignal     WRITE setRightSignal     NOTIFY rightSignalChanged)
    Q_PROPERTY(bool    connectionLost  READ connectionLost  WRITE setConnectionLost  NOTIFY connectionLostChanged)
    Q_PROPERTY(uint    systemTimestamp READ systemTimestamp WRITE setSystemTimestamp NOTIFY systemTimestampChanged)
    Q_PROPERTY(bool    node1Heartbeat  READ node1Heartbeat                            NOTIFY node1HeartbeatChanged)
    Q_PROPERTY(bool    node2Heartbeat  READ node2Heartbeat                            NOTIFY node2HeartbeatChanged)
    Q_PROPERTY(bool    node3Heartbeat  READ node3Heartbeat                            NOTIFY node3HeartbeatChanged)
    Q_PROPERTY(bool    encoderOk       READ encoderOk                                 NOTIFY encoderOkChanged)
    Q_PROPERTY(bool    socOk           READ socOk                                     NOTIFY socOkChanged)
    Q_PROPERTY(bool    gearOk          READ gearOk                                    NOTIFY gearOkChanged)
    Q_PROPERTY(bool    modeOk          READ modeOk                                    NOTIFY modeOkChanged)
    Q_PROPERTY(bool    bmp180Ok        READ bmp180Ok                                  NOTIFY bmp180OkChanged)
    Q_PROPERTY(bool    ds3231Ok        READ ds3231Ok                                  NOTIFY ds3231OkChanged)
    Q_PROPERTY(bool    signalOk        READ signalOk                                  NOTIFY signalOkChanged)

public:
    explicit VehicleModel(QObject *parent = nullptr);
    ~VehicleModel();

    int     speed()            const { return m_speed; }
    int     batterySoC()       const { return m_batterySoC; }
    int     rpm()              const { return m_rpm; }
    int     temperature()      const { return m_temperature; }
    QString gear()             const { return m_gear; }
    int     odometer()         const { return static_cast<int>(m_odometer); }
    double  tripDistance()     const { return m_tripDistance; }
    QString mode()             const { return m_mode; }
    bool    leftSignal()       const { return m_leftSignal; }
    bool    rightSignal()      const { return m_rightSignal; }
    bool    connectionLost()   const { return m_connectionLost; }
    uint    systemTimestamp()  const { return m_systemTimestamp; }
    bool    node1Heartbeat()   const { return m_node1Heartbeat; }
    bool    node2Heartbeat()   const { return m_node2Heartbeat; }
    bool    node3Heartbeat()   const { return m_node3Heartbeat; }
    bool    encoderOk()        const { return m_encoderOk; }
    bool    socOk()            const { return m_socOk; }
    bool    gearOk()           const { return m_gearOk; }
    bool    modeOk()           const { return m_modeOk; }
    bool    bmp180Ok()         const { return m_bmp180Ok; }
    bool    ds3231Ok()         const { return m_ds3231Ok; }
    bool    signalOk()         const { return m_signalOk; }

    void setSpeed(int v)              { v = qBound(0, v, 300);    if (m_speed       != v) { m_speed       = v; emit speedChanged(); } }
    void setBatterySoC(int v)         { v = qBound(0, v, 100);    if (m_batterySoC  != v) { m_batterySoC  = v; emit batterySoCChanged(); } }
    void setRpm(int v)                { v = qBound(0, v, 10000);  if (m_rpm         != v) { m_rpm         = v; emit rpmChanged(); } }
    void setTemperature(int v)        { v = qBound(-40, v, 120);  if (m_temperature != v) { m_temperature = v; emit temperatureChanged(); } }
    void setGear(const QString &v)    { if (m_gear  != v) { m_gear  = v; emit gearChanged(); } }
    void setMode(const QString &v)    { if (m_mode  != v) { m_mode  = v; emit modeChanged(); } }
    void setLeftSignal(bool v)        { if (m_leftSignal    != v) { m_leftSignal    = v; emit leftSignalChanged(); } }
    void setRightSignal(bool v)       { if (m_rightSignal   != v) { m_rightSignal   = v; emit rightSignalChanged(); } }
    void setConnectionLost(bool v)    { if (m_connectionLost!= v) { m_connectionLost= v; emit connectionLostChanged(); } }
    void setSystemTimestamp(uint v)   { if (m_systemTimestamp != v) { m_systemTimestamp = v; emit systemTimestampChanged(); } }

    // Implemented in VehicleModel.cpp — updates trip display and accumulates odometer
    void processRawDistance(double rawDistance);

    // Heartbeat setters — called by CanReader via QueuedConnection
    void setNode1Heartbeat(bool encoderOkVal);
    void setNode2Heartbeat(bool socOkVal, bool gearOkVal, bool modeOkVal);
    void setNode3Heartbeat(bool bmp180OkVal, bool ds3231OkVal, bool signalOkVal);

signals:
    void speedChanged();
    void batterySoCChanged();
    void rpmChanged();
    void temperatureChanged();
    void gearChanged();
    void odometerChanged();
    void tripDistanceChanged();
    void modeChanged();
    void leftSignalChanged();
    void rightSignalChanged();
    void connectionLostChanged();
    void systemTimestampChanged();
    void node1HeartbeatChanged();
    void node2HeartbeatChanged();
    void node3HeartbeatChanged();
    void encoderOkChanged();
    void socOkChanged();
    void gearOkChanged();
    void modeOkChanged();
    void bmp180OkChanged();
    void ds3231OkChanged();
    void signalOkChanged();

private:
    void   saveOdometerToDisk(double value);
    double loadOdometerFromDisk();

    int     m_speed           { 0 };
    int     m_batterySoC      { 0 };
    int     m_rpm             { 0 };
    int     m_temperature     { 20 };
    QString m_gear            { "P" };
    double  m_odometer        { 0.0 };
    double  m_tripDistance    { 0.0 };
    QString m_mode            { "ECO" };
    bool    m_leftSignal      { false };
    bool    m_rightSignal     { false };
    bool    m_connectionLost  { false };
    uint    m_systemTimestamp { 0 };

    double  m_lastRawDistance  { -1.0 };
    int     m_lastSavedOdoKm   { 0 };
    QTimer* m_saveTimer        { nullptr };

    bool m_node1Heartbeat { false };
    bool m_node2Heartbeat { false };
    bool m_node3Heartbeat { false };
    bool m_encoderOk      { false };
    bool m_socOk          { false };
    bool m_gearOk         { false };
    bool m_modeOk         { false };
    bool m_bmp180Ok       { false };
    bool m_ds3231Ok       { false };
    bool m_signalOk       { false };

    QTimer* m_hb1Timer { nullptr };
    QTimer* m_hb2Timer { nullptr };
    QTimer* m_hb3Timer { nullptr };
};

#endif
