SUMMARY = "Jetson UART Applications"
DESCRIPTION = "Jetson UART P2P application and boot/shutdown notification service"
LICENSE = "MIT"

LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://p2p_uart.c \
    file://uart_notify.c \
    file://boot-uart.service \
    file://shutdown-uart.service \
"

S = "${WORKDIR}"

inherit systemd

SYSTEMD_SERVICE:${PN} = "boot-uart.service shutdown-uart.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} ${WORKDIR}/p2p_uart.c -o p2p_uart

    ${CC} ${CFLAGS} ${LDFLAGS} ${WORKDIR}/uart_notify.c -o uart_notify
}

do_install() {
    install -d ${D}${bindir}

    install -m 0755 p2p_uart ${D}${bindir}/p2p_uart
    install -m 0755 uart_notify ${D}${bindir}/uart_notify

    install -d ${D}${systemd_system_unitdir}

    install -m 0644 ${WORKDIR}/boot-uart.service \
        ${D}${systemd_system_unitdir}/boot-uart.service

    install -m 0644 ${WORKDIR}/shutdown-uart.service \
        ${D}${systemd_system_unitdir}/shutdown-uart.service
}
