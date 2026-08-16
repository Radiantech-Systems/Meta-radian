# Radian Yocto Layer

`meta-radian` is the custom Yocto/OpenEmbedded layer used for the Radian Jetson platform.

The layer contains custom applications, system services, UART communication utilities, telemetry, temperature monitoring, video recording, hostname configuration, package groups, and the Radian development image.

## Repository

GitHub:

`https://github.com/Radiantech-Systems/Jetson-Power-Control-Using-STM32`

## Layer Structure

```text
meta-radian/
├── conf/
│   └── layer.conf
├── licenses/
│   └── COPYING.MIT
├── recipes-bsp/
│   ├── heartbeat/
│   ├── p2p-uart/
│   ├── telemetry-app/
│   ├── temp-monitor/
│   └── video-recorder/
├── recipes-core/
│   └── radian-hostname/
├── recipes-extra/
├── recipes-multimedia/
├── recipes-product/
│   ├── images/
│   │   └── radian-dev-image.bb
│   └── packagegroups/
│       ├── packagegroup-radian-ai.bb
│       ├── packagegroup-radian-apps.bb
│       ├── packagegroup-radian-devel.bb
│       ├── packagegroup-radian-gui.bb
│       ├── packagegroup-radian-network.bb
│       ├── packagegroup-radian-system.bb
│       └── packagegroup-radian.bb
└── recipes-test/
    ├── hello/
    └── radian-services/
```

## Main Utilities

### 1. Heartbeat

Location:

```text
recipes-bsp/heartbeat/
```

Provides the Radian heartbeat utility used for system/board health monitoring.

### 2. P2P UART

Location:

```text
recipes-bsp/p2p-uart/
```

Provides Jetson ↔ STM32 UART communication utilities.

The UART application uses the Jetson UART device configured by the recipe/source code.

Components include:

```text
p2p_uart.c
uart_notify.c
boot-uart.service
shutdown-uart.service
```

### 3. Telemetry Application

Location:

```text
recipes-bsp/telemetry-app/
```

The telemetry application collects system information from the Jetson and provides the Radian telemetry functionality.

Collectors include:

* CPU
* GPU
* Memory
* Network
* Storage
* Power
* Cooling
* Camera
* STM32
* System information
* TegraStats

The application also contains:

```text
telemetry-agent.service
footage-recorder.service
live_stream.service
```

### 4. Temperature Monitor

Location:

```text
recipes-bsp/temp-monitor/
```

Provides the temperature monitoring utility for the Jetson platform.

### 5. Video Recorder

Location:

```text
recipes-bsp/video-recorder/
```

Provides the Radian video recording functionality.

### 6. Radian Hostname

Location:

```text
recipes-core/radian-hostname/
```

Configures the hostname used by the Radian platform.

### 7. Radian Package Groups

Location:

```text
recipes-product/packagegroups/
```

The package groups organize the Radian software components into functional groups such as:

* AI
* Applications
* Development
* GUI
* Networking
* System utilities

### 8. Radian Development Image

Location:

```text
recipes-product/images/radian-dev-image.bb
```

This image integrates the Radian package groups and custom applications into a Yocto image for the Jetson platform.

---

# Build Instructions

## 1. Enter the Yocto workspace

On the Ubuntu build server:

```bash
cd ~/workspace/radian-tech/raghu/jetson/yocto/WS_1/tegra-demo-distro
```

Initialize the build environment using the appropriate machine configuration:

```bash
source setup-env --machine jetson-agx-orin-devkit
```

If `setup-env` requires the executable form:

```bash
. ./setup-env --machine jetson-agx-orin-devkit
```

## 2. Verify the meta-radian layer

Check that the layer is available:

```bash
bitbake-layers show-layers
```

`meta-radian` should appear in the layer list.

If the layer has not been added:

```bash
bitbake-layers add-layer ../layers/meta-radian
```

Verify again:

```bash
bitbake-layers show-layers
```

## 3. Build individual utilities

Individual recipes can be built to verify compilation.

### Hello

```bash
bitbake hello
```

### P2P UART

```bash
bitbake p2p-uart
```

### Heartbeat

```bash
bitbake heartbeat
```

### Temperature monitor

```bash
bitbake temp-monitor
```

### Video recorder

```bash
bitbake video-recorder
```

### Telemetry application

```bash
bitbake telemetry-app
```

## 4. Build the Radian image

Build the complete Radian development image:

```bash
bitbake radian-dev-image
```

For a complete build log:

```bash
time bitbake radian-dev-image
```

After a successful build, check the generated image files:

```bash
find tmp/deploy/images -maxdepth 2 -type f | sort
```

To locate the Tegra flash package:

```bash
find tmp/deploy/images -name "*.tegraflash.tar.gz" -o -name "*.tegraflash.tar"
```

---

# Flash Instructions

## 1. Locate the generated Tegra flash package

After the image build:

```bash
find tmp/deploy/images -name "*.tegraflash.tar.gz"
```

Example:

```text
tmp/deploy/images/p3737-0000-p3701-0005/radian-dev-image-*.tegraflash.tar.gz
```

The exact filename contains the build timestamp.

## 2. Copy the flash package to the flashing machine

Copy the generated `.tegraflash.tar.gz` package to the Ubuntu host that is connected to the Jetson.

For example:

```bash
scp <tegraflash-package>.tar.gz <user>@<flash-host>:~/flash/
```

## 3. Extract the package

On the flashing host:

```bash
mkdir -p ~/flash/radian
cd ~/flash/radian
tar -xzf <tegraflash-package>.tar.gz
```

Enter the extracted directory:

```bash
cd <extracted-directory>
```

## 4. Put the Jetson into recovery mode

Connect the Jetson to the flashing host using the appropriate USB connection.

Put the Jetson AGX Orin into Force Recovery mode according to the board hardware procedure.

Verify that the host detects the Jetson:

```bash
lsusb
```

The NVIDIA device should appear in the USB device list.

## 5. Flash the Jetson

Run the flash script supplied with the generated Tegra flash package.

For the Jetson AGX Orin development kit, the target is typically:

```bash
sudo ./flash.sh jetson-agx-orin-devkit mmcblk0p1
```

Use the exact target and flash command generated for the selected machine/board if it differs.

Wait until flashing completes successfully.

Then reboot the Jetson:

```bash
sudo reboot
```

---

# Post-Flash Verification

After the Jetson boots, verify the Radian utilities.

## 1. Check hostname

```bash
hostname
```

The configured Radian hostname should be displayed.

## 2. Check installed applications

```bash
which p2p_uart
which heartbeat
which temp_monitor
which video-recorder
```

If an application is installed at a different location:

```bash
find /usr -type f \( -name "p2p_uart" -o -name "heartbeat" -o -name "temp_monitor" -o -name "video-recorder" \)
```

## 3. Check installed package files

For example:

```bash
oe-pkgdata-util list-pkg-files p2p-uart
```

Other packages:

```bash
oe-pkgdata-util list-pkg-files heartbeat
oe-pkgdata-util list-pkg-files temp-monitor
oe-pkgdata-util list-pkg-files video-recorder
```

`oe-pkgdata-util` is normally used on the Yocto build host rather than the target Jetson.

---

# UART Testing

The P2P UART utility is intended for Jetson ↔ STM32 communication.

First identify the available UART devices:

```bash
ls -l /dev/ttyTHS*
```

The current implementation uses the UART device configured in `p2p_uart.c`.

Example:

```text
/dev/ttyTHS1
```

Check the UART:

```bash
stty -F /dev/ttyTHS1 115200
```

Run the P2P UART application:

```bash
sudo p2p_uart
```

If the application is installed under `/usr/bin`:

```bash
sudo /usr/bin/p2p_uart
```

The STM32 should be connected to the corresponding Jetson UART TX/RX/GND lines.

Verify that:

1. Jetson transmits data.
2. STM32 receives the data.
3. STM32 transmits the response.
4. Jetson receives the response.

---

# UART Systemd Service Testing

Check the services:

```bash
systemctl status boot-uart.service
systemctl status shutdown-uart.service
```

Check whether they are enabled:

```bash
systemctl is-enabled boot-uart.service
systemctl is-enabled shutdown-uart.service
```

View logs:

```bash
journalctl -u boot-uart.service
journalctl -u shutdown-uart.service
```

For live logs:

```bash
journalctl -fu boot-uart.service
```

---

# Heartbeat Testing

Check whether the heartbeat service/application is installed:

```bash
which heartbeat
```

Run it manually if applicable:

```bash
sudo heartbeat
```

If a systemd service is provided for the heartbeat functionality:

```bash
systemctl status heartbeat.service
```

View logs:

```bash
journalctl -u heartbeat.service
```

---

# Temperature Monitor Testing

Run:

```bash
sudo temp_monitor
```

Check the available thermal zones:

```bash
ls /sys/class/thermal/
```

Temperature information can also be inspected using:

```bash
cat /sys/class/thermal/thermal_zone*/temp
```

The returned values are normally represented in millidegrees Celsius.

---

# Video Recorder Testing

Verify the application:

```bash
which video-recorder
```

Run:

```bash
sudo video-recorder
```

Check the video recorder service:

```bash
systemctl status footage-recorder.service
```

View service logs:

```bash
journalctl -u footage-recorder.service
```

Check generated recordings using the output directory configured by the application.

---

# Telemetry Application Testing

Check the telemetry service:

```bash
systemctl status telemetry-agent.service
```

View logs:

```bash
journalctl -u telemetry-agent.service
```

Follow the logs:

```bash
journalctl -fu telemetry-agent.service
```

Check the Python application files:

```bash
ls -l /usr/bin/
```

or:

```bash
find /usr -type f -name "agent.py" -o -name "sender.py"
```

The telemetry application collects information from:

```text
CPU
GPU
Memory
Network
Storage
Power
Cooling
Camera
STM32
System information
TegraStats
```

---

# Live Stream Testing

Check the live stream service:

```bash
systemctl status live_stream.service
```

View logs:

```bash
journalctl -u live_stream.service
```

If the service is not running:

```bash
sudo systemctl start live_stream.service
```

Check its listening port using:

```bash
ss -lntup
```

---

# Complete System Test

After flashing the image, perform the following sequence:

```text
1. Power on Jetson
        ↓
2. Verify Jetson boots successfully
        ↓
3. Verify Radian hostname
        ↓
4. Verify UART devices
        ↓
5. Verify STM32 ↔ Jetson UART communication
        ↓
6. Verify boot UART service
        ↓
7. Verify shutdown UART service
        ↓
8. Verify heartbeat
        ↓
9. Verify temperature monitor
        ↓
10. Verify telemetry agent
        ↓
11. Verify video recorder
        ↓
12. Verify live streaming
```

Useful commands:

```bash
systemctl --failed
systemctl status telemetry-agent.service
systemctl status footage-recorder.service
systemctl status live_stream.service
systemctl status boot-uart.service
systemctl status shutdown-uart.service
```

Check recent system errors:

```bash
journalctl -p err -b
```

Check the current boot:

```bash
journalctl -b
```

---

# Build Verification

Before committing changes to GitHub, verify that the layer builds successfully:

```bash
bitbake-layers show-layers
bitbake heartbeat
bitbake p2p-uart
bitbake telemetry-app
bitbake temp-monitor
bitbake video-recorder
bitbake radian-dev-image
```

A successful image build should produce the corresponding image and Tegra flash artifacts under:

```text
build/tmp/deploy/images/
```

---

# GitHub Upload

From the `meta-radian` directory:

```bash
git init
git branch -M main
```

Add the GitHub repository:

```bash
git remote add origin https://github.com/Radiantech-Systems/Jetson-Power-Control-Using-STM32.git
```

Check:

```bash
git remote -v
```

Add the complete layer:

```bash
git add .
```

Review what will be committed:

```bash
git status
```

The staged files should contain the `meta-radian` source, recipes, services, C/C++ source, Python source, configuration files, and README.

Commit:

```bash
git commit -m "Add complete meta-radian Yocto layer"
```

Push:

```bash
git push -u origin main
```

If the GitHub repository already contains commits and Git rejects the push, first inspect the remote:

```bash
git fetch origin
git log --oneline --all --decorate -10
```

Do not force-push unless the existing repository history is intentionally being replaced.

---

# Expected GitHub Repository

The repository should contain the layer and documentation, while Yocto build output should remain outside Git:

```text
Jetson-Power-Control-Using-STM32/
├── conf/
├── licenses/
├── recipes-bsp/
├── recipes-core/
├── recipes-extra/
├── recipes-multimedia/
├── recipes-product/
├── recipes-test/
├── .gitignore
└── README.md
```

The following should **not** be committed:

```text
build/
tmp/
sstate-cache/
downloads/
*.log
```

These are generated by the Yocto build system and can be several gigabytes in size.

---

# Development Environment

The layer is intended for the Radian Jetson Yocto development environment.

The build environment used during development includes:

* Ubuntu Linux build host
* Yocto/OpenEmbedded
* NVIDIA meta-tegra
* Jetson AGX Orin
* Radian custom Yocto layer
* Custom C/C++ utilities
* Python telemetry applications
* STM32 ↔ Jetson UART communication

The exact Yocto branch, meta-tegra branch, JetPack/L4T version, machine configuration, and host dependencies should match the project's build environment.

---

# License

See:

```text
licenses/COPYING.MIT
```

for the layer license information.
