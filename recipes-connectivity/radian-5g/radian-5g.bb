SUMMARY = "Radian RM520N 5G automatic connection and network switching"
DESCRIPTION = "Automatically enables and connects the Quectel RM520N using ModemManager and provides LAN/5G network switching"
LICENSE = "CLOSED"

SRC_URI = " \
    file://radian-5g.service \
    file://radian-5g-connect.sh \
    file://network_switch.cpp \
"

S = "${WORKDIR}"

inherit systemd

SYSTEMD_SERVICE:${PN} = "radian-5g.service"
SYSTEMD_AUTO_ENABLE = "enable"

RDEPENDS:${PN} += " \
    modemmanager \
    iproute2 \
    iputils \
    grep \
    gawk \
"

do_compile() {
    ${CXX} ${CXXFLAGS} ${LDFLAGS} \
        ${WORKDIR}/network_switch.cpp \
        -o ${WORKDIR}/network_switch \
        -pthread
}

do_install() {
    install -d ${D}${bindir}

    install -m 0755 ${WORKDIR}/radian-5g-connect.sh \
        ${D}${bindir}/radian-5g-connect.sh

    install -m 0755 ${WORKDIR}/network_switch \
        ${D}${bindir}/network_switch

    install -d ${D}${systemd_system_unitdir}

    install -m 0644 ${WORKDIR}/radian-5g.service \
        ${D}${systemd_system_unitdir}/radian-5g.service
}

FILES:${PN} += " \
    ${bindir}/radian-5g-connect.sh \
    ${bindir}/network_switch \
    ${systemd_system_unitdir}/radian-5g.service \
"
