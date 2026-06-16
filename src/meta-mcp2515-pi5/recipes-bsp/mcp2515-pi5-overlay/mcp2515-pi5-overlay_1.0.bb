SUMMARY = "MCP2515 CAN controller Device Tree overlay for Raspberry Pi 5"
DESCRIPTION = "Custom DT overlay fixing the RP1 interrupt-parent mis-patch \
               and switching to IRQ_TYPE_LEVEL_LOW for the Pi 5 PCIe MSI domain. \
               COMPATIBLE_MACHINE guard ensures Pi 4 builds are completely unaffected."

LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"

COMPATIBLE_MACHINE = "raspberrypi5"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI = "file://mcp2515-can0-pi5.dts"

DEPENDS = "dtc-native"

S = "${WORKDIR}"

do_compile() {
    dtc -@ -I dts -O dtb -o ${WORKDIR}/mcp2515-can0-pi5.dtbo ${WORKDIR}/mcp2515-can0-pi5.dts
}

do_install() {
    install -d ${D}/boot/overlays
    install -m 0644 ${WORKDIR}/mcp2515-can0-pi5.dtbo ${D}/boot/overlays/
}

FILES:${PN} = "/boot/overlays/mcp2515-can0-pi5.dtbo"
