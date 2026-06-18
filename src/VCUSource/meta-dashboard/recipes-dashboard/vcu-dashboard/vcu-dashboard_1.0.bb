SUMMARY     = "VCU EV Dashboard — Qt5 SVG edition for Raspberry Pi 4"
DESCRIPTION = "Full-screen kiosk dashboard reading CAN telemetry via SocketCAN \
               and rendering an SVG-based Qt5/QML UI at 800×480 on EGLFS."
LICENSE     = "CLOSED"

# ── Package Revision ───────────────────────────────────────────────────────────
# PR is only required when externalsrc is NOT configured in local.conf.
# With externalsrc active (recommended), BitBake hashes the source tree by mtime
# and detects changes automatically — PR bumping is not needed.
# Without externalsrc (plain file:// build), bump PR on every source change to
# force sstate invalidation. Current value: r4.
PR = "r6"

# ── Build-time dependencies ────────────────────────────────────────────────────
# qtbase      : qmake, Qt core, Qt GUI, QPA/EGLFS plugins
# qtdeclarative: Qt Quick 2 / QML engine
# qtsvg       : SVG image provider (Image { source: "*.svg" })
DEPENDS = "qtbase qtdeclarative qtsvg"

# ── Source ─────────────────────────────────────────────────────────────────────
# In-Tree build: qt_project/ lives in this recipe's files/ directory.
# Bitbake copies it to ${WORKDIR}/qt_project via the file:// fetcher — no git
# remote or externalsrc overlay is required.
SRC_URI = "file://qt_project \
           file://vcu-dashboard.service \
          "
S       = "${WORKDIR}/qt_project"

# ── Build ──────────────────────────────────────────────────────────────────────
inherit qmake5 systemd

SYSTEMD_SERVICE:${PN}     = "vcu-dashboard.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

EXTRA_QMAKEVARS_PRE += "CONFIG+=release"

# ── Install ────────────────────────────────────────────────────────────────────
do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/vcu-dashboard ${D}${bindir}/vcu-dashboard
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/vcu-dashboard.service ${D}${systemd_system_unitdir}/vcu-dashboard.service
}

FILES:${PN} = "${bindir}/vcu-dashboard"
FILES:${PN} += "${systemd_system_unitdir}/vcu-dashboard.service"

# ── Runtime dependencies ───────────────────────────────────────────────────────
# qmlplugins packages deliver the QML modules imported in main.qml:
#   QtQuick 2.15  → qtdeclarative-qmlplugins
#   SVG provider  → qtsvg (pulled by DEPENDS, no extra pkg needed)
#   EGLFS QPA     → qtbase-plugins (eglfs entry point)
RDEPENDS:${PN} = " \
    qtbase-plugins \
    qtdeclarative-qmlplugins \
    qtsvg \
"
