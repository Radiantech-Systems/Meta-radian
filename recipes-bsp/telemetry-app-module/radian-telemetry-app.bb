SUMMARY = "Radian Telemetry Agent Application"
DESCRIPTION = "Telemetry, camera, power, cooling, system monitoring and video services for Jetson"
LICENSE = "CLOSED"

PACKAGE_ARCH = "aarch64"

SRC_URI = " \
    file://.env \
    file://agent.py \
    file://collectors \
    file://footage-recorder.service \
    file://footage_recorder.py \
    file://live_stream.service \
    file://live_stream_server.py \
    file://logger.py \
    file://requirements.txt \
    file://sender.py \
    file://start-jetson.sh \
    file://telemetry-agent.service \
    file://telemetry-app.service \
    file://mediamtx \
    file://mediamtx.yml \
    file://mediamtx.service \
"

S = "${WORKDIR}"

inherit systemd

SYSTEMD_PACKAGES = "${PN}"

SYSTEMD_SERVICE:${PN} = " \
    telemetry-agent.service \
    footage-recorder.service \
    live_stream.service \
    telemetry-app.service \
    mediamtx.service \
" 

SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_install() {
    # Application directory
    install -d ${D}/opt/telemetry-app

    install -m 0600 ${WORKDIR}/.env \
    ${D}/opt/telemetry-app/.env

    # Python application files
    install -m 0755 ${WORKDIR}/agent.py \
        ${D}/opt/telemetry-app/agent.py

    install -m 0755 ${WORKDIR}/footage_recorder.py \
        ${D}/opt/telemetry-app/footage_recorder.py

    install -m 0755 ${WORKDIR}/live_stream_server.py \
        ${D}/opt/telemetry-app/live_stream_server.py

    install -m 0644 ${WORKDIR}/logger.py \
        ${D}/opt/telemetry-app/logger.py

    install -m 0755 ${WORKDIR}/sender.py \
        ${D}/opt/telemetry-app/sender.py

    install -m 0755 ${WORKDIR}/start-jetson.sh \
        ${D}/opt/telemetry-app/start-jetson.sh

    install -m 0644 ${WORKDIR}/requirements.txt \
        ${D}/opt/telemetry-app/requirements.txt

    # Systemd services directory
    install -d ${D}${systemd_system_unitdir}

    # MediaMTX live streaming
    install -d ${D}/usr/bin
    install -m 0755 ${WORKDIR}/mediamtx \
        ${D}/usr/bin/mediamtx

    install -d ${D}/etc
    install -m 0644 ${WORKDIR}/mediamtx.yml \
        ${D}/etc/mediamtx.yml

    install -m 0644 ${WORKDIR}/mediamtx.service \
        ${D}${systemd_system_unitdir}/mediamtx.service

    # Collectors
    install -d ${D}/opt/telemetry-app/collectors

    install -m 0644 ${WORKDIR}/collectors/__init__.py \
        ${D}/opt/telemetry-app/collectors/__init__.py

    install -m 0644 ${WORKDIR}/collectors/camera.py \
        ${D}/opt/telemetry-app/collectors/camera.py

    install -m 0644 ${WORKDIR}/collectors/cooling.py \
        ${D}/opt/telemetry-app/collectors/cooling.py

    install -m 0644 ${WORKDIR}/collectors/cpu.py \
        ${D}/opt/telemetry-app/collectors/cpu.py

    install -m 0644 ${WORKDIR}/collectors/gpu.py \
        ${D}/opt/telemetry-app/collectors/gpu.py

    install -m 0644 ${WORKDIR}/collectors/memory.py \
        ${D}/opt/telemetry-app/collectors/memory.py

    install -m 0644 ${WORKDIR}/collectors/network.py \
        ${D}/opt/telemetry-app/collectors/network.py

    install -m 0644 ${WORKDIR}/collectors/power.py \
        ${D}/opt/telemetry-app/collectors/power.py

    install -m 0644 ${WORKDIR}/collectors/storage.py \
        ${D}/opt/telemetry-app/collectors/storage.py

    install -m 0644 ${WORKDIR}/collectors/system_info.py \
        ${D}/opt/telemetry-app/collectors/system_info.py

    install -m 0644 ${WORKDIR}/collectors/tegrastats_reader.py \
        ${D}/opt/telemetry-app/collectors/tegrastats_reader.py

    # STM32 collector
    install -m 0644 ${WORKDIR}/collectors/stm32.py \
        ${D}/opt/telemetry-app/collectors/stm32.py

    # Systemd services

    install -m 0644 ${WORKDIR}/telemetry-agent.service \
        ${D}${systemd_system_unitdir}/telemetry-agent.service

    install -m 0644 ${WORKDIR}/footage-recorder.service \
        ${D}${systemd_system_unitdir}/footage-recorder.service

    install -m 0644 ${WORKDIR}/live_stream.service \
        ${D}${systemd_system_unitdir}/live_stream.service

    install -m 0644 ${WORKDIR}/telemetry-app.service \
    ${D}${systemd_system_unitdir}/telemetry-app.service
}

RDEPENDS:${PN} += " \
    python3-core \
    python3-logging \
    python3-psutil \
    python3-requests \
    python3-dotenv \
"

FILES:${PN} += " \
    /opt/telemetry-app \
    /usr/bin/mediamtx \
    /etc/mediamtx.yml \
    ${systemd_system_unitdir}/telemetry-agent.service \
    ${systemd_system_unitdir}/footage-recorder.service \
    ${systemd_system_unitdir}/live_stream.service \
    ${systemd_system_unitdir}/telemetry-app.service \
    ${systemd_system_unitdir}/mediamtx.service \
"
