# Pi 5 overlay: replaces the base service with one that calls the DRM
# card-detection wrapper, and installs the wrapper itself.
# Active only when meta-mcp2515-pi5 is in bblayers.conf (Pi 5 builds).
# Pi 4 builds never include this layer — they use direct ExecStart.

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# PR:append forces sstate invalidation when this bbappend or its files change.
# Increment the digit whenever vcu-dashboard.service or vcu-dashboard-start.sh is modified.
PR:append = ".2"

SRC_URI:append = " file://vcu-dashboard-start.sh"

do_install:append() {
    install -m 0755 ${WORKDIR}/vcu-dashboard-start.sh ${D}${bindir}/vcu-dashboard-start.sh
    install -m 0644 ${WORKDIR}/vcu-dashboard.service ${D}${systemd_system_unitdir}/vcu-dashboard.service
}

FILES:${PN}:append = " ${bindir}/vcu-dashboard-start.sh"
