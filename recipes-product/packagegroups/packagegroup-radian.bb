SUMMARY = "Radian package group"
DESCRIPTION = "Package group for Radian applications"

LICENSE = "MIT"

PACKAGE_ARCH = "${MACHINE_ARCH}"

inherit packagegroup

RDEPENDS:${PN} = "\
    hello \
    p2p-uart \
    radian-services \
"
