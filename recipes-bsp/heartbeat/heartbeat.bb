SUMMARY = "UART Heartbeat Application"
DESCRIPTION = "Sends a heartbeat message every second through Jetson UART"
LICENSE = "MIT"

LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://heartbeat-source "

S = "${WORKDIR}/heartbeat-source"

inherit autotools

FILES:${PN} += "${bindir}/"
