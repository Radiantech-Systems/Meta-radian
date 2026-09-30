SUMMARY = "Radian modular camera discovery"
DESCRIPTION = "Modular camera discovery service for DHCP cameras and RTSP generation"
LICENSE = "MIT"
PACKAGE_ARCH = "aarch64"
LIC_FILES_CHKSUM = "file://LICENSE;md5=9ebe7d326dd0236d3a6e14b3525ed3fa"

inherit systemd

SRC_URI = " \
    file://camera_discovery.py \
    file://config.json \
    file://camera-discovery.service \
    file://LICENSE \
"

S = "${WORKDIR}"

RDEPENDS:${PN} += " \
    python3 \
    ffmpeg \
"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${WORKDIR}/camera_discovery.py \
        ${D}${bindir}/camera_discovery.py

    install -d ${D}${sysconfdir}/camera-discovery
    install -m 0644 ${WORKDIR}/config.json \
        ${D}${sysconfdir}/camera-discovery/config.json

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/camera-discovery.service \
        ${D}${systemd_system_unitdir}/camera-discovery.service

    install -d ${D}${localstatedir}/lib/camera-discovery
}

SYSTEMD_SERVICE:${PN} = "camera-discovery.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

FILES:${PN} += " \
    ${systemd_system_unitdir}/camera-discovery.service \
    ${sysconfdir}/camera-discovery/config.json \
    ${localstatedir}/lib/camera-discovery \
"
