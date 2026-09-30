#!/bin/sh

JSON_FILE="/var/lib/camera-discovery/cameras.json"

AWS_RTSP_URL="rtsp://radiantech-dashboard.radiantechsys.com:8554/jetson1_raw"

echo "=============================================="
echo "Jetson RAW RTSP Forwarder"
echo "=============================================="
echo "Camera JSON: $JSON_FILE"
echo "AWS RTSP:    $AWS_RTSP_URL"

if [ ! -f "$JSON_FILE" ]; then
    echo "ERROR: Camera JSON file not found:"
    echo "$JSON_FILE"
    exit 1
fi

RTSP_URL=$(/usr/bin/python3 -c '
import json
import sys

json_file = sys.argv[1]

try:
    with open(json_file, "r") as f:
        data = json.load(f)

    cameras = data.get("cameras", [])

    for camera in cameras:
        if camera.get("status") != "online":
            continue

        rtsp = camera.get("rtsp", {})

        if rtsp.get("valid") and rtsp.get("url"):
            print(rtsp["url"])
            sys.exit(0)

    print("ERROR: No valid online RTSP camera found", file=sys.stderr)
    sys.exit(1)

except Exception as e:
    print("ERROR: Failed to read camera JSON:", e, file=sys.stderr)
    sys.exit(1)
' "$JSON_FILE")

if [ -z "$RTSP_URL" ]; then
    echo "ERROR: No RTSP URL found"
    exit 1
fi

echo "Using CCTV RTSP URL:"
echo "$RTSP_URL"
echo "Starting FFmpeg..."

exec /usr/bin/ffmpeg \
    -rtsp_transport tcp \
    -i "$RTSP_URL" \
    -map 0:v:0 \
    -c:v copy \
    -an \
    -rtbufsize 64M \
    -use_wallclock_as_timestamps 1 \
    -f rtsp \
    -rtsp_transport tcp \
    "$AWS_RTSP_URL"
