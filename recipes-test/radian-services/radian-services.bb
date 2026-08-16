SUMMARY = "Radian Systemd Services"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit systemd

SRC_URI = " \
    file://hello.service \
    file://p2p_uart.service \
"

S = "${WORKDIR}"

SYSTEMD_SERVICE:${PN} = "hello.service p2p_uart.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install() {
    install -d ${D}${systemd_system_unitdir}

    install -m 0644 ${WORKDIR}/hello.service \
        ${D}${systemd_system_unitdir}/hello.service

    install -m 0644 ${WORKDIR}/p2p_uart.service \
        ${D}${systemd_system_unitdir}/p2p_uart.service
}

FILES:${PN} += "${systemd_system_unitdir}"
