SUMMARY = "Jetson RAW and AI RTSP stream forwarding services"
DESCRIPTION = "Systemd services for forwarding CCTV RAW and Jetson AI RTSP streams to AWS MediaMTX"
LICENSE = "CLOSED"

SRC_URI = " \
    file://raw-stream-forward.sh \
    file://jetson-raw-forward.service \
    file://jetson-raw-forward.path \
    file://jetson-ai-forward.service \
"

S = "${WORKDIR}"

inherit systemd

DEPENDS += "python3-native"
RDEPENDS:${PN} += "ffmpeg python3-core"

do_install() {
    install -d ${D}${bindir}
    install -d ${D}${systemd_system_unitdir}

    install -m 0755 ${WORKDIR}/raw-stream-forward.sh \
        ${D}${bindir}/raw-stream-forward.sh

    install -m 0644 ${WORKDIR}/jetson-raw-forward.service \
        ${D}${systemd_system_unitdir}/jetson-raw-forward.service

    install -m 0644 ${WORKDIR}/jetson-raw-forward.path \
        ${D}${systemd_system_unitdir}/jetson-raw-forward.path

    install -m 0644 ${WORKDIR}/jetson-ai-forward.service \
        ${D}${systemd_system_unitdir}/jetson-ai-forward.service
}

SYSTEMD_SERVICE:${PN} = " \
    jetson-raw-forward.service \
    jetson-raw-forward.path \
    jetson-ai-forward.service \
"

SYSTEMD_AUTO_ENABLE:${PN} = "enable"

FILES:${PN} += " \
    ${bindir}/raw-stream-forward.sh \
    ${systemd_system_unitdir}/jetson-raw-forward.service \
    ${systemd_system_unitdir}/jetson-raw-forward.path \
    ${systemd_system_unitdir}/jetson-ai-forward.service \
"
