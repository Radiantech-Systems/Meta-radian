"""
CPU usage, per-core usage, frequency, and temperature.
Temperature is read from the thermal zone psutil exposes; on real
Jetson hardware this maps to the CPU thermal zone under
/sys/class/thermal/. Falls back gracefully off-device.
"""
import psutil


def _cpu_temperature() -> float | None:
    try:
        temps = psutil.sensors_temperatures()
    except Exception:
        return None
    if not temps:
        return None
    # Common Jetson zone names; fall back to first available sensor
    for key in ("cpu-thermal", "CPU-therm", "thermal-fan-est"):
        if key in temps and temps[key]:
            return temps[key][0].current
    first_key = next(iter(temps))
    if temps[first_key]:
        return temps[first_key][0].current
    return None


def collect() -> dict:
    usage_percent = psutil.cpu_percent(interval=None)
    per_core = psutil.cpu_percent(interval=None, percpu=True)
    freq = psutil.cpu_freq()

    return {
        "usage_percent": usage_percent,
        "per_core_usage": per_core,
        "frequency_mhz": round(freq.current, 1) if freq else None,
        "temperature_c": _cpu_temperature(),
    }
