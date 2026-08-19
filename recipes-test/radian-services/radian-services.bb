SUMMARY = "Radian Systemd Services"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit systemd

SRC_URI = " \
    file://hello.service \
"

S = "${WORKDIR}"

SYSTEMD_SERVICE:${PN} = "hello.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_install() {
    install -d ${D}${systemd_system_unitdir}

    install -m 0644 ${WORKDIR}/hello.service \
        ${D}${systemd_system_unitdir}/hello.service
}

FILES:${PN} += "${systemd_system_unitdir}"
