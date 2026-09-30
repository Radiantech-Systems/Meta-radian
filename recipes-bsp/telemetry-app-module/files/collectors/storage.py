"""Root filesystem disk usage, via psutil."""
import psutil


def collect(path: str = "/") -> dict:
    usage = psutil.disk_usage(path)
    return {
        "disk_used_gb": round(usage.used / (1024 ** 3), 2),
        "disk_free_gb": round(usage.free / (1024 ** 3), 2),
        "disk_total_gb": round(usage.total / (1024 ** 3), 2),
        "disk_usage_percent": usage.percent,
    }
