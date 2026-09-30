"""
GPU usage/frequency/temperature.
Usage % and temperature come from tegrastats (GR3D_FREQ / gpu@).
Frequency is read directly from the GPU devfreq sysfs node when present.
"""
from collectors import tegrastats_reader


def _gpu_frequency_mhz() -> float | None:
    try:
        with open("/sys/devices/platform/17000000.gpu/devfreq/17000000.gpu/cur_freq") as f:
            return round(int(f.read().strip()) / 1_000_000, 1)
    except Exception:
        return None


def collect() -> dict:
    stats = tegrastats_reader.get()
    return {
        "usage_percent": stats.get("gpu_usage_percent"),
        "frequency_mhz": _gpu_frequency_mhz(),
        "temperature_c": stats.get("gpu_temperature_c"),
    }
