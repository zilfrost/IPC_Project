SUMMARY = "Auto-login root on tty1"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit allarch

SRC_URI = "file://autologin.conf"
S = "${WORKDIR}"

do_install() {
    install -d ${D}${sysconfdir}/systemd/system/getty@tty1.service.d
    install -m 0644 ${WORKDIR}/autologin.conf \
        ${D}${sysconfdir}/systemd/system/getty@tty1.service.d/autologin.conf
}

FILES:${PN} = "${sysconfdir}/systemd/system/getty@tty1.service.d/"
