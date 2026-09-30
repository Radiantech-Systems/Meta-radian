"""
Sends a telemetry payload to the backend's POST /telemetry endpoint.
Isolated from agent.py so retry/backoff logic can evolve independently
of the collection loop.
"""
"""
HTTP communication with the telemetry backend.

Handles:
    - telemetry upload
    - snapshot command polling
    - snapshot result upload
    - sliced-video command polling
    - sliced-video result upload
"""

import os
import requests

from logger import get_logger

logger = get_logger("sender")

API_KEY = os.getenv("API_KEY", "")


def _headers() -> dict:
    return {"X-API-Key": API_KEY} if API_KEY else {}


def send(
    server_url: str,
    payload: dict,
    timeout: float = 3.0,
) -> bool:
    """Send the normal telemetry payload to the backend."""
    try:
        resp = requests.post(
            f"{server_url.rstrip('/')}/telemetry",
            json=payload,
            headers=_headers(),
            timeout=timeout,
        )

        if resp.status_code == 200:
            return True

        logger.warning(
            f"Telemetry server responded {resp.status_code}: "
            f"{resp.text[:200]}"
        )
        return False

    except requests.exceptions.RequestException as e:
        logger.error(
            f"Failed to reach server at {server_url}: {e}"
        )
        return False


def get_pending_command(
    server_url: str,
    device_id: str,
    timeout: float = 3.0,
):
    """
    Ask the backend whether this Jetson has a pending
    snapshot command.
    """
    try:
        resp = requests.get(
            f"{server_url.rstrip('/')}/snapshot-commands/pending",
            params={"device_id": device_id},
            headers=_headers(),
            timeout=timeout,
        )

        if resp.status_code != 200:
            logger.warning(
                f"Command server responded {resp.status_code}: "
                f"{resp.text[:200]}"
            )
            return None

        data = resp.json()

        if data.get("status") == "none":
            return None

        return data

    except requests.exceptions.RequestException as e:
        logger.warning(
            f"Failed to check for commands: {e}"
        )
        return None

    except ValueError as e:
        logger.warning(
            f"Invalid command response from server: {e}"
        )
        return None


def get_pending_video_command(
    server_url: str,
    device_id: str,
    timeout: float = 3.0,
):
    """
    Ask the backend whether this Jetson has a pending
    sliced-video metadata command.
    """
    try:
        resp = requests.get(
            f"{server_url.rstrip('/')}/video-commands/pending",
            params={"device_id": device_id},
            headers=_headers(),
            timeout=timeout,
        )

        if resp.status_code != 200:
            logger.warning(
                f"Video command server responded "
                f"{resp.status_code}: {resp.text[:200]}"
            )
            return None

        data = resp.json()

        if data.get("status") == "none":
            return None

        return data

    except requests.exceptions.RequestException as e:
        logger.warning(
            f"Failed to check for video commands: {e}"
        )
        return None

    except ValueError as e:
        logger.warning(
            f"Invalid video command response: {e}"
        )
        return None


def send_snapshot_result(
    server_url: str,
    device_id: str,
    request_id: str,
    data: dict,
    timeout: float = 15.0,
) -> bool:
    """Send an on-demand snapshot result back to the backend."""
    try:
        payload = {
            "device_id": device_id,
            "request_id": request_id,
            "data": data,
        }

        resp = requests.post(
            f"{server_url.rstrip('/')}/snapshot-commands/result",
            json=payload,
            headers=_headers(),
            timeout=timeout,
        )

        if resp.status_code == 200:
            return True

        logger.warning(
            f"Snapshot result server responded {resp.status_code}: "
            f"{resp.text[:200]}"
        )
        return False

    except requests.exceptions.RequestException as e:
        logger.error(
            f"Failed to send snapshot result: {e}"
        )
        return False

    except ValueError as e:
        logger.error(
            f"Invalid snapshot result response: {e}"
        )
        return False


def send_video_result(
    server_url: str,
    device_id: str,
    request_id: str,
    data: dict,
    timeout: float = 10.0,
) -> bool:
    """Send sliced-video metadata result back to the backend."""
    try:
        payload = {
            "device_id": device_id,
            "request_id": request_id,
            "data": data,
        }

        resp = requests.post(
            f"{server_url.rstrip('/')}/video-commands/result",
            json=payload,
            headers=_headers(),
            timeout=timeout,
        )

        if resp.status_code == 200:
            return True

        logger.warning(
            f"Video result server responded {resp.status_code}: "
            f"{resp.text[:200]}"
        )
        return False

    except requests.exceptions.RequestException as e:
        logger.error(
            f"Failed to send video result: {e}"
        )
        return False

    except ValueError as e:
        logger.error(
            f"Invalid video result response: {e}"
        )
        return False


def get_pending_video_stream_command(
    server_url: str,
    device_id: str,
    timeout: float = 3.0,
):
    """
    Ask the backend whether this Jetson has a pending
    on-demand video streaming request.
    """
    try:
        resp = requests.get(
            f"{server_url.rstrip('/')}/video-stream/pending",
            params={"device_id": device_id},
            headers=_headers(),
            timeout=timeout,
        )

        if resp.status_code != 200:
            logger.warning(
                f"Video stream command server responded "
                f"{resp.status_code}: {resp.text[:200]}"
            )
            return None

        data = resp.json()

        if data.get("status") == "none":
            return None

        if data.get("command") != "video_stream_request":
            logger.warning(
                f"Unexpected video stream command: {data}"
            )
            return None

        return data

    except requests.exceptions.RequestException as e:
        logger.warning(
            f"Failed to check for video stream commands: {e}"
        )
        return None

    except ValueError as e:
        logger.warning(
            f"Invalid video stream command response: {e}"
        )
        return None


def stream_video_file(
    server_url: str,
    device_id: str,
    request_id: str,
    video_path: str,
    range_header: str | None = None,
) -> bool:
    """
    Stream one selected MP4 from the Jetson's local Flask
    video server to the AWS video-stream upload endpoint.

    The MP4 is streamed in chunks and is never loaded completely
    into memory or placed inside telemetry JSON.
    """

    from urllib.parse import quote

    try:
        # Prevent path traversal.
        clean_path = video_path.strip().lstrip("/")

        if not clean_path or ".." in clean_path.split("/"):
            logger.error(
                f"Rejected unsafe video path: {video_path}"
            )
            return False

        local_url = (
            "http://127.0.0.1:5002/video/"
            + quote(clean_path, safe="/")
        )

        local_headers = {}

        if range_header:
            local_headers["Range"] = range_header

        logger.info(
            f"Starting video stream: {clean_path} "
            f"request_id={request_id}"
        )

        # Ask the existing Flask video server for the selected file.
        local_resp = requests.get(
            local_url,
            headers=local_headers,
            stream=True,
            timeout=(5, None),
        )

        if local_resp.status_code not in (200, 206):
            logger.error(
                f"Local video server returned "
                f"{local_resp.status_code}: "
                f"{local_resp.text[:200]}"
            )
            local_resp.close()
            return False

        upload_headers = _headers()

        upload_headers.update({
            "X-Video-Status": str(local_resp.status_code),
            "X-Video-Content-Type": (
                local_resp.headers.get(
                    "Content-Type",
                    "video/mp4",
                )
            ),
            "X-Video-Accept-Ranges": (
                local_resp.headers.get(
                    "Accept-Ranges",
                    "bytes",
                )
            ),
        })

        if local_resp.headers.get("Content-Length"):
            upload_headers["X-Video-Content-Length"] = (
                local_resp.headers["Content-Length"]
            )

        if local_resp.headers.get("Content-Range"):
            upload_headers["X-Video-Content-Range"] = (
                local_resp.headers["Content-Range"]
            )

        def video_chunks():
            try:
                for chunk in local_resp.iter_content(
                    chunk_size=64 * 1024
                ):
                    if chunk:
                        yield chunk
            finally:
                local_resp.close()

        upload_url = (
            f"{server_url.rstrip('/')}"
            f"/video-stream/upload/{request_id}"
        )

        logger.info(
            f"Forwarding video stream to AWS: "
            f"request_id={request_id}"
        )

        upload_resp = requests.post(
            upload_url,
            headers=upload_headers,
            data=video_chunks(),
            timeout=(5, None),
        )

        if upload_resp.status_code == 200:
            logger.info(
                f"Video stream completed: "
                f"request_id={request_id}"
            )
            return True

        logger.warning(
            f"Video stream upload returned "
            f"{upload_resp.status_code}: "
            f"{upload_resp.text[:200]}"
        )

        return False

    except requests.exceptions.RequestException as e:
        logger.error(
            f"Video streaming request failed: {e}"
        )
        return False

    except Exception as e:
        logger.exception(
            f"Unexpected video streaming error: {e}"
        )
        return False
