"""
Lightweight live video server that runs directly on the Jetson.

Uses ffmpeg to read frames from the camera (V4L2) and Flask to serve
them as an MJPEG stream — a format browsers can display natively with
a plain <img> tag, no HLS.js, no separate media server, no MediaMTX
required for this. Much simpler than the RTSP-server + MediaMTX path
used for external CCTV cameras (see docker/mediamtx.yml if you still
want that for a real IP camera later — this is for the Jetson's own
camera specifically).

Endpoints:
    GET /video_feed  -> multipart/x-mixed-replace MJPEG stream
    GET /health       -> {"status": "ok"}

Run:
    python3 live_stream_server.py
"""
import os
import subprocess

from flask import Flask, Response
from dotenv import load_dotenv

from logger import get_logger

load_dotenv()

logger = get_logger("live_stream_server")

app = Flask(__name__)

DEVICE = os.getenv("LIVE_STREAM_DEVICE", os.getenv("FOOTAGE_DEVICE", "/dev/video0"))
WIDTH = os.getenv("LIVE_STREAM_WIDTH", "1280")
HEIGHT = os.getenv("LIVE_STREAM_HEIGHT", "720")
FPS = os.getenv("LIVE_STREAM_FPS", "15")
QUALITY = os.getenv("LIVE_STREAM_JPEG_QUALITY", "5")  # ffmpeg -q:v, 2 (best) - 31 (worst)
PORT = int(os.getenv("LIVE_STREAM_PORT", "5001"))


def _generate_mjpeg():
    """Spawns ffmpeg reading the camera and re-yields each JPEG frame it
    produces, wrapped in the multipart boundary format browsers expect
    for an MJPEG <img> stream."""
    cmd = [
        "ffmpeg",
        "-f", "v4l2",
        "-framerate", FPS,
        "-video_size", f"{WIDTH}x{HEIGHT}",
        "-i", DEVICE,
        "-f", "mjpeg",
        "-q:v", QUALITY,
        "-",
    ]
    logger.info(f"Starting ffmpeg: {' '.join(cmd)}")
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=10 ** 7)
    buffer = b""
    try:
        while True:
            chunk = proc.stdout.read(4096)
            if not chunk:
                logger.warning("ffmpeg produced no more data — camera may have disconnected.")
                break
            buffer += chunk
            start = buffer.find(b"\xff\xd8")  # JPEG SOI marker
            end = buffer.find(b"\xff\xd9")    # JPEG EOI marker
            if start != -1 and end != -1 and end > start:
                frame = buffer[start:end + 2]
                buffer = buffer[end + 2:]
                yield (
                    b"--frame\r\n"
                    b"Content-Type: image/jpeg\r\n\r\n" + frame + b"\r\n"
                )
    finally:
        proc.terminate()
        proc.wait(timeout=5)


@app.route("/video_feed")
def video_feed():
    return Response(_generate_mjpeg(), mimetype="multipart/x-mixed-replace; boundary=frame")


@app.route("/health")
def health():
    return {"status": "ok", "device": DEVICE}


if __name__ == "__main__":
    logger.info(f"Starting live stream server | device={DEVICE} | {WIDTH}x{HEIGHT}@{FPS}fps | port={PORT}")
    app.run(host="0.0.0.0", port=PORT, threaded=True)
