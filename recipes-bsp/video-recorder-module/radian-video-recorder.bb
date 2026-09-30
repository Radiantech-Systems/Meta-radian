SUMMARY = "Radian modular video recorder"
DESCRIPTION = "Modular RTSP video recorder and video server"
LICENSE = "CLOSED"

PACKAGE_ARCH = "aarch64"

SRC_URI = " \
    file://Makefile.am \
    file://configure.ac \
    file://videoserver.py \
    file://src/ \
    file://include/ \
    file://video-recorder.service \
    file://video-server.service \
"

S = "${WORKDIR}"

inherit autotools pkgconfig systemd

DEPENDS += "nlohmann-json"

RDEPENDS:${PN} = " \
    gstreamer1.0 \
    python3-core \
    python3-flask \
    python3-flask-cors \
"

FILES:${PN} += " \
    ${bindir}/video-recorder \
    ${bindir}/videoserver.py \
    /root/video_recorder \
    /root/video_recorder/recordings \
    /root/video_recorder/logs \
"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/video-recorder ${D}${bindir}/video-recorder

    install -m 0755 ${WORKDIR}/videoserver.py \
        ${D}${bindir}/videoserver.py

    install -d ${D}/root/video_recorder/recordings
    install -d ${D}/root/video_recorder/logs

    install -d ${D}${systemd_system_unitdir}

    install -m 0644 ${WORKDIR}/video-recorder.service \
        ${D}${systemd_system_unitdir}/video-recorder.service

    install -m 0644 ${WORKDIR}/video-server.service \
        ${D}${systemd_system_unitdir}/video-server.service
}

SYSTEMD_SERVICE:${PN} = "video-recorder.service video-server.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"
