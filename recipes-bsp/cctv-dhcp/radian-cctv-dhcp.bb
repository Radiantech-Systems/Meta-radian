SUMMARY = "Radian CCTV DHCP configuration"
DESCRIPTION = "Modular DHCP configuration for Radian CCTV cameras"
LICENSE = "CLOSED"

PACKAGE_ARCH = "aarch64"

SRC_URI = "file://cctv-dhcp.conf"

S = "${WORKDIR}"


do_install() {
    install -d ${D}${sysconfdir}/dnsmasq.d
    install -m 0644 ${WORKDIR}/cctv-dhcp.conf \
        ${D}${sysconfdir}/dnsmasq.d/cctv-dhcp.conf
}

FILES:${PN} = "${sysconfdir}/dnsmasq.d/cctv-dhcp.conf"
