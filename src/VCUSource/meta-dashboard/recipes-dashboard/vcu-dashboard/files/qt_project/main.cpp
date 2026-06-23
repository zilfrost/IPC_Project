#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCoreApplication>
#include <QCursor>
#include <QDebug>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QQuickWindow>
#include <QSGRendererInterface>
#endif
// INCLUDEPATH += src in vcu_dashboard.pro makes these resolve correctly
// without path prefixes — the files themselves are not modified.
#include "VehicleModel.h"
#include "CanReader.h"
#include "GpioButtonReader.h"

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM",            QByteArrayLiteral("eglfs"));
    qputenv("QSG_RENDER_LOOP",            QByteArrayLiteral("threaded"));
    qputenv("QT_QPA_EGLFS_FORCEVSYNC",   QByteArrayLiteral("1"));
    qputenv("QT_QPA_EGLFS_SWAPINTERVAL", QByteArrayLiteral("1"));
    qputenv("QSG_BATCH_OPAQUE",           QByteArrayLiteral("1"));
    // Force Qt to treat the framebuffer as exactly 800×480 logical pixels.
    // Without these, EGLFS reads EDID or KMS mode (often 1920×1080) and
    // Screen.width/height return the wrong values, breaking uniformScale.
    // Physical mm = 7-inch panel, 16:9 aspect → 154×86 mm.
    qputenv("QT_QPA_EGLFS_WIDTH",            QByteArrayLiteral("800"));
    qputenv("QT_QPA_EGLFS_HEIGHT",           QByteArrayLiteral("480"));
    qputenv("QT_QPA_EGLFS_PHYSICAL_WIDTH",   QByteArrayLiteral("154"));
    qputenv("QT_QPA_EGLFS_PHYSICAL_HEIGHT",  QByteArrayLiteral("86"));
    // Disable automatic HiDPI scaling — AA_EnableHighDpiScaling combined with
    // a high reported DPI can silently halve the scene, pinning it top-left.
    qputenv("QT_AUTO_SCREEN_SCALE_FACTOR",   QByteArrayLiteral("0"));
    qputenv("QT_SCALE_FACTOR",               QByteArrayLiteral("1"));

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // AA_EnableHighDpiScaling is disabled via QT_AUTO_SCREEN_SCALE_FACTOR above;
    // the attribute is kept commented out to avoid accidental re-enablement.
    // QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    // AA_UseOpenGLES targets the Pi's GLES2 framebuffer.
    QCoreApplication::setAttribute(Qt::AA_UseOpenGLES);
#else
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
#endif
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QGuiApplication app(argc, argv);
    QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor));

    // Create the vehicle data model (lives on the main/GUI thread)
    VehicleModel vehicleModel;

    // Read CAN interface from argv[1], fallback to vcan0
    const QString canInterface = (argc > 1 && argv[1] && argv[1][0] != '\0')
            ? QString::fromLocal8Bit(argv[1])
            : QStringLiteral("vcan0");

    // CanReader(VehicleModel*, interface) — model pointer comes first
    CanReader canReader(&vehicleModel, canInterface);

    // GPIO 14 hazard button — syncs hazard state to CanReader so CAN 0x107 is
    // suppressed while hazard is active (prevents CAN frames fighting the blink)
    GpioButtonReader gpioReader(&vehicleModel, 14);
    QObject::connect(&vehicleModel, &VehicleModel::hazardChanged,
                     [&canReader, &vehicleModel]() {
                         canReader.setHazardActive(vehicleModel.hazard());
                     });

    // Expose the unmodified C++ model to QML as "vehicleModel"
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("vehicleModel", &vehicleModel);

    // QML is bundled under qml/ in resources.qrc → prefix is qrc:/qml/main.qml
    // (The only difference from the original project's qrc:/main.qml)
    const QUrl url(QStringLiteral("qrc:/qml/main.qml"));
    engine.load(url);

    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Failed to load QML from" << url;
        return -1;
    }

    canReader.start();
    gpioReader.start();

    int result = app.exec();

    // Stop threads before VehicleModel destructs
    canReader.stop();
    gpioReader.stop();

    return result;
}
