FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += " \
    file://wpa_supplicant-wlan0.conf \
    file://20-wlan-hotspot.network \
    file://25-wlan-home.network \
    file://networkd-wait.conf \
"

do_install:append() {
    install -d ${D}${sysconfdir}/wpa_supplicant
    install -m 0600 ${WORKDIR}/wpa_supplicant-wlan0.conf ${D}${sysconfdir}/wpa_supplicant/

    install -d ${D}${sysconfdir}/systemd/network
    install -m 0644 ${WORKDIR}/20-wlan-hotspot.network ${D}${sysconfdir}/systemd/network/
    install -m 0644 ${WORKDIR}/25-wlan-home.network ${D}${sysconfdir}/systemd/network/

    install -d ${D}${sysconfdir}/systemd/system/systemd-networkd.service.d
    install -m 0644 ${WORKDIR}/networkd-wait.conf ${D}${sysconfdir}/systemd/system/systemd-networkd.service.d/override.conf
}

# Explicitly ship the installed files so bitbake QA does not report
# "installed and not shipped" errors for the /etc/systemd/ paths.
FILES:${PN}:append = " \
    ${sysconfdir}/wpa_supplicant/wpa_supplicant-wlan0.conf \
    ${sysconfdir}/systemd/network/20-wlan-hotspot.network \
    ${sysconfdir}/systemd/network/25-wlan-home.network \
    ${sysconfdir}/systemd/system/systemd-networkd.service.d/override.conf \
"

# Protect the credentials file from being overwritten by package upgrades.
CONFFILES:${PN}:append = " ${sysconfdir}/wpa_supplicant/wpa_supplicant-wlan0.conf"

SYSTEMD_SERVICE:${PN}:append = " wpa_supplicant@wlan0.service"
