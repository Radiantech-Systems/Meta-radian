"""
Jetson Footage Recorder — runs as its own process, separate from the
telemetry agent (agent.py). Records the camera in fixed-length
segments ("slices") using ffmpeg, then uploads each finished segment
to the backend's /footage/upload endpoint for the dashboard's
Recorded Footage page.

Requires ffmpeg installed on the Jetson: sudo apt install -y ffmpeg

Run:
    python3 footage_recorder.py
"""
import os
import subprocess
import tempfile
import time
from datetime import datetime, timezone

import requests
from dotenv import load_dotenv

from logger import get_logger

load_dotenv()

logger = get_logger("footage_recorder")

SERVER_URL = os.getenv("SERVER_URL", "http://localhost:8000")
DEVICE_ID = os.getenv("DEVICE_ID", "jetson-orin-01")
API_KEY = os.getenv("API_KEY", "")

FOOTAGE_ENABLED = os.getenv("FOOTAGE_ENABLED", "true").lower() == "true"
FOOTAGE_DEVICE = os.getenv("FOOTAGE_DEVICE", "/dev/video0")
FOOTAGE_SEGMENT_SECONDS = int(os.getenv("FOOTAGE_SEGMENT_SECONDS", "60"))
FOOTAGE_RESOLUTION = os.getenv("FOOTAGE_RESOLUTION", "1280x720")
FOOTAGE_FRAMERATE = os.getenv("FOOTAGE_FRAMERATE", "15")


def record_segment(tmp_path: str) -> bool:
    """Records one fixed-length clip with ffmpeg. Returns True on success."""
    cmd = [
        "ffmpeg", "-y",
        "-f", "v4l2",
        "-framerate", FOOTAGE_FRAMERATE,
        "-video_size", FOOTAGE_RESOLUTION,
        "-i", FOOTAGE_DEVICE,
        "-t", str(FOOTAGE_SEGMENT_SECONDS),
        "-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p",
        tmp_path,
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        logger.error(f"ffmpeg failed: {result.stderr[-500:]}")
        return False
    return True


def upload_segment(tmp_path: str, started_at: datetime) -> bool:
    filename = f"{DEVICE_ID}_{started_at.strftime('%Y%m%dT%H%M%S')}.mp4"
    headers = {"X-API-Key": API_KEY} if API_KEY else {}
    try:
        with open(tmp_path, "rb") as f:
            resp = requests.post(
                f"{SERVER_URL.rstrip('/')}/footage/upload",
                headers=headers,
                data={
                    "device_id": DEVICE_ID,
                    "started_at": started_at.isoformat(),
                    "duration_seconds": str(FOOTAGE_SEGMENT_SECONDS),
                },
                files={"file": (filename, f, "video/mp4")},
                timeout=30,
            )
        if resp.status_code == 200:
            logger.info(f"Uploaded {filename}")
            return True
        logger.warning(f"Upload rejected ({resp.status_code}): {resp.text[:200]}")
        return False
    except requests.exceptions.RequestException as e:
        logger.error(f"Upload failed: {e}")
        return False


def main():
    if not FOOTAGE_ENABLED:
        logger.info("FOOTAGE_ENABLED=false — footage recorder is disabled, exiting.")
        return

    logger.info(f"Starting footage recorder | device={FOOTAGE_DEVICE} | "
                f"segment={FOOTAGE_SEGMENT_SECONDS}s | server={SERVER_URL}")

    while True:
        started_at = datetime.now(timezone.utc)
        with tempfile.NamedTemporaryFile(suffix=".mp4", delete=False) as tmp:
            tmp_path = tmp.name
        try:
            ok = record_segment(tmp_path)
            if ok and os.path.getsize(tmp_path) > 0:
                uploaded = upload_segment(tmp_path, started_at)
                if not uploaded:
                    logger.warning("Segment recorded but upload failed; discarding (no local retry queue yet).")
            else:
                logger.warning("Segment recording produced no usable output; skipping upload.")
        except Exception as e:
            logger.error(f"Unexpected error in recording loop: {e}")
        finally:
            if os.path.exists(tmp_path):
                os.remove(tmp_path)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        logger.info("Footage recorder stopped by user.")
