SUMMARY = "Temperature Monitor"
DESCRIPTION = "Temperature monitoring application"
LICENSE = "MIT"

LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://temp-monitor-source"

S = "${WORKDIR}/temp-monitor-source"

inherit autotools

FILES:${PN} += "${bindir}/temp_monitor"
