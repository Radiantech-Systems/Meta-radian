"""
Collects static/slow-changing system identity info:
device name/id, hostname, JetPack/Ubuntu/kernel versions, boot time, uptime.
"""
import os
import platform
import subprocess
import time
import psutil


def _read_file(path: str) -> str | None:
    try:
        with open(path, "r") as f:
            return f.read().strip()
    except Exception:
        return None


def _run(cmd: list[str]) -> str | None:
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=2)
        return out.stdout.strip() if out.returncode == 0 else None
    except Exception:
        return None


def get_jetpack_version() -> str | None:
    # JetPack version is derived from /etc/nv_tegra_release on real Jetson hardware
    content = _read_file("/etc/nv_tegra_release")
    if content:
        return content
    return None


def collect(device_id: str, device_name: str) -> dict:
    boot_time_ts = psutil.boot_time()
    uptime_seconds = time.time() - boot_time_ts

    return {
        "device_name": device_name,
        "device_id": device_id,
        "hostname": platform.node(),
        "jetpack_version": get_jetpack_version(),
        "ubuntu_version": _run(["lsb_release", "-ds"]) or platform.version(),
        "kernel_version": platform.release(),
        "boot_time": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(boot_time_ts)),
        "uptime_seconds": round(uptime_seconds, 1),
    }
