SUMMARY = "Radian AI Packages"
LICENSE = "MIT"

inherit packagegroup

RDEPENDS:${PN} = "\
    opencv \
    cuda-toolkit \
    python3-tensorrt \
    tensorrt-core \
    tensorrt-plugins-prebuilt \
    deepstream-7.1 \
"

