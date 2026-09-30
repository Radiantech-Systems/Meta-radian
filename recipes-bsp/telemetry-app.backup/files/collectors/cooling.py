"""
Fan RPM/PWM + board temperature.
Fan control lives under /sys/devices/pwm-fan on Jetson; falls back to
tegrastats-parsed values (board temp always comes from tegrastats).
"""
from collectors import tegrastats_reader


def _read_int(path: str) -> int | None:
    try:
        with open(path) as f:
            return int(f.read().strip())
    except Exception:
        return None


def collect() -> dict:
    stats = tegrastats_reader.get()
    fan_pwm = _read_int("/sys/devices/pwm-fan/target_pwm")
    fan_rpm = _read_int("/sys/devices/pwm-fan/rpm_measured")
    return {
        "fan_rpm": fan_rpm,
        "fan_pwm": fan_pwm if fan_pwm is not None else stats.get("fan_pwm"),
        "board_temperature_c": stats.get("board_temperature_c"),
    }
