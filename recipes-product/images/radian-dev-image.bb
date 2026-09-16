require recipes-demo/images/demo-image-sato.bb

SUMMARY = "Radian Development Image"
DESCRIPTION = "Custom Radian development image for Jetson AGX Orin"
LICENSE = "MIT"

IMAGE_INSTALL:append = " \
    packagegroup-radian \
    packagegroup-radian-network \
    packagegroup-radian-devel \
    packagegroup-radian-ai \
    packagegroup-radian-gui \
    packagegroup-radian-system \
    packagegroup-radian-apps \
    ffmpeg \
    kernel-modules \
    modemmanager \
    radian-5g \
"
