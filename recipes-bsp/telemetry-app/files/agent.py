"""
Jetson AGX Orin Telemetry Agent.

Runs continuously on the Jetson itself, collecting every metric
category once per second and POSTing the combined document to the
FastAPI backend running on the Ubuntu server. This process never
serves a UI -- the dashboard runs only on the server.

Run:
    python3 agent.py
"""
import os
import platform
import socket
import time
from datetime import datetime, timezone

from dotenv import load_dotenv

from logger import get_logger
from sender import send
from collectors import (
    system_info, cpu, gpu, memory, storage, power, cooling, network, camera, stm32,
    tegrastats_reader,
)

load_dotenv()

logger = get_logger("agent")

SERVER_URL = os.getenv("SERVER_URL", "http://localhost:8000")
INTERVAL = float(os.getenv("COLLECTION_INTERVAL_SECONDS", "1"))
DEVICE_ID = os.getenv("DEVICE_ID") or socket.gethostname()
DEVICE_NAME = os.getenv("DEVICE_NAME", "Jetson AGX Orin")
NETWORK_INTERFACE = os.getenv("NETWORK_INTERFACE", "eth0")


def collect_telemetry() -> dict:
    return {
        "system_info": system_info.collect(DEVICE_ID, DEVICE_NAME),
        "cpu": cpu.collect(),
        "gpu": gpu.collect(),
        "memory": memory.collect(),
        "storage": storage.collect(),
        "power": power.collect(),
        "cooling": cooling.collect(),
        "network": network.collect(NETWORK_INTERFACE),
        "cameras": camera.collect(),
        "stm32": stm32.collect(),
        "timestamp": datetime.now(timezone.utc).isoformat(),
    }


def main():
    logger.info(f"Starting telemetry agent | device_id={DEVICE_ID} | server={SERVER_URL} | "
                f"interval={INTERVAL}s | platform={platform.platform()}")

    # Prime psutil's CPU percent counters (first call always returns 0.0)
    import psutil
    psutil.cpu_percent(interval=None)
    psutil.cpu_percent(interval=None, percpu=True)

    # Start the shared background tegrastats reader (no-op off-Jetson)
    tegrastats_reader.start(interval_ms=int(INTERVAL * 1000))

    consecutive_failures = 0
    while True:
        start = time.time()
        try:
            payload = collect_telemetry()
            ok = send(SERVER_URL, payload)
            if ok:
                if consecutive_failures:
                    logger.info("Server connection restored.")
                consecutive_failures = 0
            else:
                consecutive_failures += 1
        except Exception as e:
            consecutive_failures += 1
            logger.error(f"Collection error: {e}")

        if consecutive_failures and consecutive_failures % 10 == 0:
            logger.warning(f"{consecutive_failures} consecutive failures sending telemetry.")

        elapsed = time.time() - start
        time.sleep(max(0.0, INTERVAL - elapsed))


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        logger.info("Agent stopped by user.")
