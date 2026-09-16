SUMMARY = "Radian edge video recording and relay service"
DESCRIPTION = "Records an H.264 RTSP source in segments and optionally relays it over SRT"
LICENSE = "CLOSED"

SRC_URI = " \
    file://configure.ac \
    file://Makefile.am \
    file://src/main.c \
    file://radian-video-relay.conf \
    file://radian-video-relay.service \
"

S = "${WORKDIR}"

inherit autotools pkgconfig systemd

DEPENDS = " \
    glib-2.0 \
    gstreamer1.0 \
"

RDEPENDS:${PN} = " \
    gstreamer1.0 \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
"

do_install:append() {
    install -d ${D}${sysconfdir}/radian-video-relay
    install -m 0644 ${WORKDIR}/radian-video-relay.conf \
        ${D}${sysconfdir}/radian-video-relay/config.conf

    # Keep the legacy recording location during side-by-side validation so
    # existing event-generation and video-serving applications can consume
    # relay output without configuration changes.
    install -d ${D}/root/video_recorder/recordings

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/radian-video-relay.service \
        ${D}${systemd_system_unitdir}/radian-video-relay.service
}

SYSTEMD_SERVICE:${PN} = "radian-video-relay.service"
SYSTEMD_AUTO_ENABLE:${PN} = "disable"

CONFFILES:${PN} += "${sysconfdir}/radian-video-relay/config.conf"

FILES:${PN} += " \
    ${sysconfdir}/radian-video-relay/config.conf \
    /root/video_recorder/recordings \
"
