"""
Power mode + consumption.
Power mode is read via nvpmodel (Jetson power-mode manager).
Consumption (VDD_IN) comes from the shared tegrastats reader.
"""
import subprocess
from collectors import tegrastats_reader


def _power_mode() -> str | None:
    try:
        out = subprocess.run(["nvpmodel", "-q"], capture_output=True, text=True, timeout=2)
        if out.returncode == 0:
            for line in out.stdout.splitlines():
                if "NV Power Mode" in line:
                    return line.split(":", 1)[-1].strip()
    except Exception:
        pass
    return None


def collect() -> dict:
    stats = tegrastats_reader.get()
    return {
        "power_mode": _power_mode(),
        "power_consumption_mw": stats.get("power_consumption_mw"),
        "input_voltage_mv": None,  # reserved for future STM32 integration
        "current_ma": None,        # reserved for future STM32 integration
    }
