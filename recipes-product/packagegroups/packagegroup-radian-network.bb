SUMMARY = "Radian Network Packages"
LICENSE = "MIT"

inherit packagegroup

RDEPENDS:${PN} = "\
    openssh \
    openssh-sftp-server \
    mosquitto \
    mosquitto-clients \
    curl \
    wget \
    iproute2 \
    net-tools \
    iputils \
    tcpdump \
    ethtool \
    iperf3 \
    netcat \
"

