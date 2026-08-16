SUMMARY = "Radian Application Package Group"
DESCRIPTION = "Radian applications for Jetson"
LICENSE = "CLOSED"

inherit packagegroup

RDEPENDS:${PN} = " \
    heartbeat \
    temp-monitor \
    video-recorder \
    telemetry-app \
"
