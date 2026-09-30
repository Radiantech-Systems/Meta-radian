"""
Camera enumeration via V4L2 (/dev/video*). Reports name/status/resolution/fps
for each detected camera. Best-effort: cameras that can't be queried are
still listed with status 'unknown'.
"""
import glob
import subprocess


def _query_v4l2(device: str) -> dict:
    info = {"resolution": None, "fps": None}
    try:
        out = subprocess.run(
            ["v4l2-ctl", "-d", device, "--get-fmt-video"],
            capture_output=True, text=True, timeout=2,
        )
        if out.returncode == 0:
            for line in out.stdout.splitlines():
                line = line.strip()
                if line.startswith("Width/Height"):
                    info["resolution"] = line.split(":", 1)[-1].strip().replace("/", "x")
        out_fps = subprocess.run(
            ["v4l2-ctl", "-d", device, "--get-parm"],
            capture_output=True, text=True, timeout=2,
        )
        if out_fps.returncode == 0:
            for line in out_fps.stdout.splitlines():
                if "fps" in line.lower():
                    info["fps"] = line.strip()
    except Exception:
        pass
    return info


def collect() -> list[dict]:
    cameras = []
    for device in sorted(glob.glob("/dev/video*")):
        details = _query_v4l2(device)
        cameras.append({
            "name": device,
            "status": "connected" if details.get("resolution") else "unknown",
            "resolution": details.get("resolution"),
            "fps": details.get("fps"),
        })
    return cameras
