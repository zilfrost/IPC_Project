QT += quick svg
CONFIG += c++17

TARGET   = vcu-dashboard
TEMPLATE = app

# INCLUDEPATH lets VehicleModel.cpp's #include "VehicleModel.h" and
# CanReader.h's #include "VehicleModel.h" resolve correctly without
# modifying any original source file.
INCLUDEPATH += src

SOURCES += \
    main.cpp \
    src/VehicleModel.cpp
# CanReader is a header-only class — no CanReader.cpp exists or is needed.

HEADERS += \
    src/CanReader.h \
    src/VehicleModel.h

RESOURCES += resources.qrc

# ARM release flags (Raspberry Pi 4 aarch64 toolchain)
linux-aarch64-gnu-g++|linux-aarch64-unknown-linux-gnu-g++|linux-aarch64-oe-g++ {
    QMAKE_CXXFLAGS_RELEASE += -O3 -ffast-math
    # -flto removed: causes multi-minute silent linker hang during Yocto cross-compilation
}

# Out-of-source build artefacts (mirrors existing project convention)
MOC_DIR     = build/moc
OBJECTS_DIR = build/obj
RCC_DIR     = build/rcc

target.path = /usr/bin
INSTALLS   += target
