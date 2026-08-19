SUMMARY = "UART Heartbeat Application"
DESCRIPTION = "Sends a heartbeat message every second through Jetson UART"
LICENSE = "MIT"

LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://heartbeat-source \
    file://heartbeat.service \
"

S = "${WORKDIR}/heartbeat-source"

inherit autotools systemd

do_install:append() {
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/heartbeat.service \
        ${D}${systemd_system_unitdir}/heartbeat.service
}

SYSTEMD_SERVICE:${PN} = "heartbeat.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

FILES:${PN} += "${bindir}/"
