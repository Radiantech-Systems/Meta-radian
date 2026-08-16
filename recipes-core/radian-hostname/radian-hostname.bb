SUMMARY = "Radian hostname"
DESCRIPTION = "Install custom hostname"
LICENSE = "MIT"

LIC_FILES_CHKSUM = "file://${LAYERDIR}}/licenses/COPYING.MIT;md5=3da9cfbcb788c80a0384361b4de20420"

SRC_URI = " \
    file://hostname \
"

S = "${WORKDIR}"

do_install() {
    install -d ${D}${sysconfdir}
    install -m 0644 ${WORKDIR}/hostname ${D}${sysconfdir}/hostname
}

FILES:${PN} += "${sysconfdir}/hostname"
