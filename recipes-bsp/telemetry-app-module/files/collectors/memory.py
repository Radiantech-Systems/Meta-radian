"""RAM and swap usage, via psutil (works identically on Jetson and any Linux host)."""
import psutil


def collect() -> dict:
    vm = psutil.virtual_memory()
    swap = psutil.swap_memory()
    return {
        "ram_used_mb": round(vm.used / (1024 ** 2), 1),
        "ram_free_mb": round(vm.available / (1024 ** 2), 1),
        "ram_total_mb": round(vm.total / (1024 ** 2), 1),
        "ram_usage_percent": vm.percent,
        "swap_used_mb": round(swap.used / (1024 ** 2), 1),
    }
