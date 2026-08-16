SUMMARY = "Radian Development Packages"
LICENSE = "MIT"

inherit packagegroup

RDEPENDS:${PN} = "\
    packagegroup-core-buildessential \
    gcc \
    g++ \
    make \
    cmake \
    gdb \
    git \
    python3 \
    python3-pip \
    python3-setuptools \
    python3-wheel \
    python3-virtualenv \
    vim \
    nano \
    procps \
    htop \
    lsof \
    strace \
    ltrace \
    rsync \
    unzip \
    zip \
    file \
    which \
    findutils \
    less \
"
