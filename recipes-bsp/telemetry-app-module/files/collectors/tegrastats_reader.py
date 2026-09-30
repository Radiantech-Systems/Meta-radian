"""
Runs `tegrastats` as a long-lived background subprocess and continuously
parses its output into a shared dict. This is far cheaper than invoking
tegrastats fresh on every collection cycle, and matches how NVIDIA's own
tooling (jtop, etc.) consumes it.

tegrastats is only available on real Jetson hardware. When it's missing
(e.g. developing/testing on a regular Ubuntu machine), this module fails
silently and all GPU/power/cooling collectors fall back to None values
so the rest of the pipeline still works end-to-end.

Typical tegrastats line looks like:
RAM 5234/30698MB (lfb 512x4MB) SWAP 0/15349MB (cached 0MB) CPU [12%@2201,9%@2201,...]
GR3D_FREQ 34% EMC_FREQ 12% APE 174 cpu@45.5C tboard@41C tdiode@43C gpu@42.5C
thermal@44C VDD_IN 6543mW/6500mW VDD_CPU_GPU_CV 2100mW/2000mW VDD_SOC 1200mW/1150mW
"""
import re
import shutil
import subprocess
import threading

_lock = threading.Lock()
_latest: dict = {}
_started = False

_PATTERNS = {
    "gpu_usage_percent": re.compile(r"GR3D_FREQ (\d+)%"),
    "gpu_temperature_c": re.compile(r"gpu@([\d.]+)C"),
    "cpu_temperature_c_fallback": re.compile(r"cpu@([\d.]+)C"),
    "board_temperature_c": re.compile(r"tboard@([\d.]+)C"),
    "power_consumption_mw": re.compile(r"VDD_IN (\d+)mW"),
    "fan_pwm": re.compile(r"FAN(?:\s|_PWM=)?(\d+)%?"),
}


def _reader_thread(interval_ms: int):
    global _latest
    try:
        proc = subprocess.Popen(
            ["tegrastats", "--interval", str(interval_ms)],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
        )
    except FileNotFoundError:
        return  # not on Jetson hardware
    for line in proc.stdout:
        parsed = {}
        for key, pattern in _PATTERNS.items():
            m = pattern.search(line)
            if m:
                parsed[key] = float(m.group(1))
        with _lock:
            _latest.update(parsed)


def start(interval_ms: int = 1000):
    """Idempotently starts the background tegrastats reader (no-op if unavailable)."""
    global _started
    if _started:
        return
    _started = True
    if shutil.which("tegrastats") is None:
        return
    t = threading.Thread(target=_reader_thread, args=(interval_ms,), daemon=True)
    t.start()


def get() -> dict:
    with _lock:
        return dict(_latest)
