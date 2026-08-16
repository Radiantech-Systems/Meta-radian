SUMMARY = "Radian Video Recorder"
DESCRIPTION = "RTSP video recorder using GStreamer and NVIDIA hardware acceleration"
LICENSE = "CLOSED"

SRC_URI = " \
    file://Makefile.am \
    file://configure.ac \
    file://videoserver.py \
    file://src/ \
    file://include/ \
"

S = "${WORKDIR}"

inherit autotools pkgconfig

DEPENDS = ""

RDEPENDS:${PN} = " \
    gstreamer1.0 \
    python3-core \
"

FILES:${PN} += " \
    ${bindir}/video-recorder \
    ${bindir}/videoserver.py \
    /root/video_recorder \
    /root/video_recorder/recordings \
    /root/video_recorder/logs \
"

do_install() {
    # Install C++ video recorder executable
    install -d ${D}${bindir}
    install -m 0755 ${B}/video-recorder ${D}${bindir}/video-recorder

    # Install Python video server
    install -m 0755 ${WORKDIR}/videoserver.py ${D}${bindir}/videoserver.py

    # Create application directories
    install -d ${D}/root/video_recorder/recordings
    install -d ${D}/root/video_recorder/logs
}
