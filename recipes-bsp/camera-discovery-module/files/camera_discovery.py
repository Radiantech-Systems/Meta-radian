#!/usr/bin/env python3

import json
import os
import subprocess
import time
from datetime import datetime, timezone


CONFIG_FILE = "/etc/camera-discovery/config.json"


def now():
    return datetime.now(timezone.utc).isoformat()


def load_json(path, default):
    if not os.path.exists(path):
        return default

    try:
        with open(path, "r") as f:
            return json.load(f)
    except Exception:
        return default


def save_json(path, data):
    directory = os.path.dirname(path)

    if directory:
        os.makedirs(directory, exist_ok=True)

    temp = path + ".tmp"

    with open(temp, "w") as f:
        json.dump(data, f, indent=4)

    os.replace(temp, path)


def read_leases(lease_file):
    devices = []

    if not os.path.exists(lease_file):
        return devices

    try:
        with open(lease_file, "r") as f:
            for line in f:
                parts = line.strip().split()

                if len(parts) < 3:
                    continue

                devices.append({
                    "expiry": parts[0],
                    "mac": parts[1].lower(),
                    "ip": parts[2],
                    "hostname": parts[3] if len(parts) >= 4 else ""
                })

    except Exception as e:
        print(f"[ERROR] Failed to read DHCP leases: {e}")

    return devices


def get_camera_id(mac, registry):
    mac = mac.lower()

    for device in registry["devices"]:
        if device.get("mac", "").lower() == mac:
            return device["id"]

    used_numbers = []

    for device in registry["devices"]:
        camera_id = device.get("id", "")

        if camera_id.startswith("camera_"):
            try:
                number = int(camera_id.split("_")[1])
                used_numbers.append(number)
            except Exception:
                pass

    number = 1

    while number in used_numbers:
        number += 1

    camera_id = f"camera_{number:03d}"

    registry["devices"].append({
        "id": camera_id,
        "mac": mac,
        "created_at": now()
    })

    return camera_id


def make_rtsp_url(template, ip):
    return template.replace("{ip}", ip)


def check_rtsp(rtsp_url, timeout):
    command = [
        "ffprobe",
        "-v", "error",
        "-rtsp_transport", "tcp",
        "-select_streams", "v:0",
        "-show_entries",
        "stream=codec_name,profile,width,height,r_frame_rate",
        "-of", "json",
        rtsp_url
    ]

    try:
        result = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=timeout
        )

        if result.returncode != 0:
            return None

        if not result.stdout.strip():
            return None

        data = json.loads(result.stdout)

        streams = data.get("streams", [])

        if not streams:
            return None

        stream = streams[0]

        codec = stream.get("codec_name")

        if not codec:
            return None

        fps = None

        rate = stream.get("r_frame_rate")

        if rate and "/" in rate:
            num, den = rate.split("/")

            try:
                num = float(num)
                den = float(den)

                if den != 0:
                    fps = round(num / den, 2)

            except Exception:
                pass

        return {
            "codec": codec,
            "profile": stream.get("profile"),
            "width": stream.get("width"),
            "height": stream.get("height"),
            "fps": fps
        }

    except subprocess.TimeoutExpired:
        print("[FFPROBE] Timeout")
        return None

    except FileNotFoundError:
        print("[ERROR] ffprobe not found")
        return None

    except Exception as e:
        print(f"[FFPROBE] Error: {e}")
        return None


def discover(config):
    registry_file = config["device_registry"]
    output_file = config["output_file"]

    registry = load_json(
        registry_file,
        {
            "version": 1,
            "devices": []
        }
    )

    leases = read_leases(config["lease_file"])

    cameras = []

    print()
    print("========================================")
    print(" CAMERA DISCOVERY")
    print("========================================")

    if not leases:
        print("[DISCOVERY] No DHCP devices found")

    for device in leases:

        mac = device["mac"]
        ip = device["ip"]
        hostname = device["hostname"]

        camera_id = get_camera_id(
            mac,
            registry
        )

        rtsp_url = make_rtsp_url(
            config["rtsp_template"],
            ip
        )

        print()
        print(f"ID       : {camera_id}")
        print(f"MAC      : {mac}")
        print(f"IP       : {ip}")
        print(f"RTSP     : {rtsp_url}")
        print("[FFPROBE] Checking...")

        stream = check_rtsp(
            rtsp_url,
            config["ffprobe_timeout"]
        )

        if stream:

            print("[FFPROBE] VALID")

            cameras.append({
                "id": camera_id,
                "mac": mac,
                "ip": ip,
                "hostname": hostname,
                "status": "online",
                "last_checked": now(),
                "rtsp": {
                    "valid": True,
                    "url": rtsp_url
                },
                "stream": stream
            })

        else:

            print("[FFPROBE] INVALID")

    save_json(
        registry_file,
        registry
    )

    output = {
        "version": 1,
        "updated_at": now(),
        "cameras": cameras
    }

    save_json(
        output_file,
        output
    )

    print()
    print("========================================")
    print(f"ACTIVE CAMERAS : {len(cameras)}")
    print(f"OUTPUT         : {output_file}")
    print("========================================")


def main():

    print()
    print("========================================")
    print(" Radian Camera Discovery")
    print(" Continuous Mode")
    print("========================================")
    print()

    while True:

        try:

            config = load_json(
                CONFIG_FILE,
                {}
            )

            discover(config)

            interval = config.get(
                "scan_interval",
                10
            )

            print()
            print(f"Next scan in {interval} seconds...")
            print()

            time.sleep(interval)

        except KeyboardInterrupt:

            print()
            print("Camera discovery stopped.")
            break

        except Exception as e:

            print(f"[ERROR] {e}")
            time.sleep(5)


if __name__ == "__main__":
    main()
