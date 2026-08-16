"""
Sends a telemetry payload to the backend's POST /telemetry endpoint.
Isolated from agent.py so retry/backoff logic can evolve independently
of the collection loop.
"""
import os
import requests

from logger import get_logger

logger = get_logger("sender")

API_KEY = os.getenv("API_KEY", "")


def send(server_url: str, payload: dict, timeout: float = 3.0) -> bool:
    try:
        headers = {"X-API-Key": API_KEY} if API_KEY else {}
        resp = requests.post(f"{server_url.rstrip('/')}/telemetry", json=payload, headers=headers, timeout=timeout)
        if resp.status_code == 200:
            return True
        logger.warning(f"Server responded {resp.status_code}: {resp.text[:200]}")
        return False
    except requests.exceptions.RequestException as e:
        logger.error(f"Failed to reach server at {server_url}: {e}")
        return False

