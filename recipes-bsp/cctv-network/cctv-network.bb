SUMMARY = "Radian CCTV Ethernet network configuration"
DESCRIPTION = "Static network configuration for the dedicated CCTV Ethernet interface"
LICENSE = "MIT"

SRC_URI = " \
    file://10-cctv-eth0.network \
    file://LICENSE \
"

LIC_FILES_CHKSUM = "file://LICENSE;md5=9ebe7d326dd0236d3a6e14b3525ed3fa"

inherit systemd

S = "${WORKDIR}"

do_install() {
    install -d ${D}${systemd_unitdir}/network
    install -m 0644 ${WORKDIR}/10-cctv-eth0.network \
        ${D}${systemd_unitdir}/network/10-cctv-eth0.network
}

FILES:${PN} += "${systemd_unitdir}/network/10-cctv-eth0.network"

RDEPENDS:${PN} += "systemd"
