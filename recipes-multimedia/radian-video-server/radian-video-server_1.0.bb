SUMMARY = "Radian recorded video HTTP API"
DESCRIPTION = "Lists and serves Radian event video files over HTTP"
LICENSE = "CLOSED"

SRC_URI = " \
    file://configure.ac \
    file://Makefile.am \
    file://src/main.c \
    file://radian-video-server.conf \
    file://radian-video-server.service \
"

S = "${WORKDIR}"

inherit autotools pkgconfig systemd

DEPENDS = " \
    glib-2.0 \
    json-glib \
    libsoup-3.0 \
"

RDEPENDS:${PN} = " \
    glib-2.0 \
    json-glib \
    libsoup-3.0 \
"

do_install:append() {
    install -d ${D}${sysconfdir}/radian-video-server
    install -m 0644 ${WORKDIR}/radian-video-server.conf \
        ${D}${sysconfdir}/radian-video-server/config.conf

    # The object detector writes completed event videos here.
    install -d ${D}/root/video_recorder/events/videos

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/radian-video-server.service \
        ${D}${systemd_system_unitdir}/radian-video-server.service
}

SYSTEMD_SERVICE:${PN} = "radian-video-server.service"
SYSTEMD_AUTO_ENABLE:${PN} = "disable"

CONFFILES:${PN} += "${sysconfdir}/radian-video-server/config.conf"

FILES:${PN} += " \
    ${sysconfdir}/radian-video-server/config.conf \
    /root/video_recorder/events/videos \
"
