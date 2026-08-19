SUMMARY = "Temperature Monitor"
DESCRIPTION = "Temperature monitoring application"
LICENSE = "MIT"

LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://temp-monitor-source \
    file://temp-monitor.service \
"

S = "${WORKDIR}/temp-monitor-source"

inherit autotools systemd

do_install:append() {
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/temp-monitor.service \
        ${D}${systemd_system_unitdir}/temp-monitor.service
}

SYSTEMD_SERVICE:${PN} = "temp-monitor.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

FILES:${PN} += "${bindir}/temp_monitor"
