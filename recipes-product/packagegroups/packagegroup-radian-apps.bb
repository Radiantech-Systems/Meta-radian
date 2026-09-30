SUMMARY = "Radian Application Package Group"
DESCRIPTION = "Radian applications for Jetson"
LICENSE = "CLOSED"

inherit packagegroup

RDEPENDS:${PN} = " \
    heartbeat \
    temp-monitor \
    radian-video-recorder \
    radian-telemetry-app \
    radian-object-detector \
"
