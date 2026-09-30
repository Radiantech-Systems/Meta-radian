"""
Ethernet status, IP/MAC address, and upload/download throughput.
Throughput is computed as a delta between consecutive psutil
counter reads (bytes/sec -> kbps), matching the 1-second collection cadence.
"""
import socket
import time
import psutil

_last_sample: dict = {"time": None, "sent": None, "recv": None}


def _mac_address(interface: str) -> str | None:
    try:
        addrs = psutil.net_if_addrs().get(interface, [])
        for a in addrs:
            if a.family == psutil.AF_LINK:
                return a.address
    except Exception:
        pass
    return None


def _ip_address(interface: str) -> str | None:
    try:
        addrs = psutil.net_if_addrs().get(interface, [])
        for a in addrs:
            if a.family == socket.AF_INET:
                return a.address
    except Exception:
        pass
    return None


def _is_up(interface: str) -> bool:
    try:
        stats = psutil.net_if_stats().get(interface)
        return bool(stats and stats.isup)
    except Exception:
        return False


def collect(interface: str = "eth0") -> dict:
    global _last_sample
    now = time.time()
    counters = psutil.net_io_counters(pernic=True).get(interface)

    upload_kbps = download_kbps = None
    if counters:
        if _last_sample["time"] is not None:
            dt = max(now - _last_sample["time"], 1e-6)
            upload_kbps = round(((counters.bytes_sent - _last_sample["sent"]) * 8 / 1000) / dt, 1)
            download_kbps = round(((counters.bytes_recv - _last_sample["recv"]) * 8 / 1000) / dt, 1)
        _last_sample = {"time": now, "sent": counters.bytes_sent, "recv": counters.bytes_recv}

    return {
        "ethernet_status": "up" if _is_up(interface) else "down",
        "ip_address": _ip_address(interface),
        "mac_address": _mac_address(interface),
        "upload_speed_kbps": upload_kbps,
        "download_speed_kbps": download_kbps,
    }
