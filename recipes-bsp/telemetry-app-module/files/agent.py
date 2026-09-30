"""
Jetson AGX Orin Telemetry Agent.

Runs continuously on the Jetson itself, collecting every metric
category once per second and POSTing the combined document to the
FastAPI backend running on the Ubuntu server.

The same HTTP connection is also used to check for on-demand
dashboard commands such as AI snapshot requests.

This process never serves a UI.
"""

import os
import platform
import socket
import time
import base64
import threading
from datetime import datetime, timezone
from pathlib import Path

from dotenv import load_dotenv

from logger import get_logger

from sender import (
    send,
    get_pending_command,
    send_snapshot_result,
    send_video_result,
    get_pending_video_command,
    get_pending_video_stream_command,
    stream_video_file,
)

from collectors import (
    system_info,
    cpu,
    gpu,
    memory,
    storage,
    power,
    cooling,
    network,
    camera,
    stm32,
    tegrastats_reader,
)


load_dotenv()

logger = get_logger("agent")


SERVER_URL = os.getenv(
    "SERVER_URL",
    "http://localhost:8000",
)

INTERVAL = float(
    os.getenv(
        "COLLECTION_INTERVAL_SECONDS",
        "1",
    )
)

DEVICE_ID = (
    os.getenv("DEVICE_ID")
    or socket.gethostname()
)

DEVICE_NAME = os.getenv(
    "DEVICE_NAME",
    "Jetson AGX Orin",
)

NETWORK_INTERFACE = os.getenv(
    "NETWORK_INTERFACE",
    "eth0",
)


# Local AI snapshot directory.
SNAPSHOT_DIR = Path(
    "/root/video_recorder/snapshots"
)

VIDEO_DIR = Path(
    "/root/video_recorder/events/videos"
)

# Limit the number of images returned in one
# on-demand snapshot response.
MAX_SNAPSHOTS_PER_CATEGORY = 30


def collect_telemetry() -> dict:
    return {
        "system_info": system_info.collect(
            DEVICE_ID,
            DEVICE_NAME,
        ),
        "cpu": cpu.collect(),
        "gpu": gpu.collect(),
        "memory": memory.collect(),
        "storage": storage.collect(),
        "power": power.collect(),
        "cooling": cooling.collect(),
        "network": network.collect(
            NETWORK_INTERFACE
        ),
        "cameras": camera.collect(),
        "stm32": stm32.collect(),
        "timestamp": datetime.now(
            timezone.utc
        ).isoformat(),
    }


def collect_snapshots(
    offset: int = 0,
    limit: int = MAX_SNAPSHOTS_PER_CATEGORY,
) -> dict:
    """
    Read the latest local AI snapshots.

    Images are returned temporarily as Base64 data.
    They are not permanently stored on AWS.
    """

    category_directories = {
        "people": ["people"],
        "vehicles": ["vehicles"],
        "others": ["animals", "electronics", "other"],
    }

    result = {
        "people": [],
        "vehicles": [],
        "others": [],
    }

    for output_category, directory_names in category_directories.items():

        all_files = []

        for directory_name in directory_names:

            directory = SNAPSHOT_DIR / directory_name

            if not directory.exists():
                continue

            try:
                files = [
                    path
                    for path in directory.iterdir()
                    if path.is_file()
                    and path.stat().st_size > 0
                    and path.suffix.lower()
                    in {
                        ".jpg",
                        ".jpeg",
                        ".png",
                        ".webp",
                    }
                ]

                all_files.extend(files)

            except Exception as e:
                logger.warning(
                    f"Unable to read snapshot directory "
                    f"{directory}: {e}"
                )

        all_files.sort(
            key=lambda path: path.stat().st_mtime,
            reverse=True,
        )

        selected_files = all_files[offset:offset + limit]

        for path in selected_files:

            try:
                image_bytes = path.read_bytes()

                image_data = base64.b64encode(
                    image_bytes
                ).decode("ascii")

                suffix = path.suffix.lower()

                mime_type = {
                    ".jpg": "image/jpeg",
                    ".jpeg": "image/jpeg",
                    ".png": "image/png",
                    ".webp": "image/webp",
                }.get(
                    suffix,
                    "application/octet-stream",
                )

                timestamp = datetime.fromtimestamp(
                    path.stat().st_mtime,
                    tz=timezone.utc,
                ).isoformat()

                result[output_category].append(
                    {
                        "filename": path.name,
                        "image": image_data,
                        "mime_type": mime_type,
                        "timestamp": timestamp,
                    }
                )

            except Exception as e:
                logger.warning(
                    f"Failed to read snapshot "
                    f"{path}: {e}"
                )

    return result


def collect_sliced_videos(
    since: str | None = None,
) -> dict:
    """
    Return metadata for sliced/event videos.

    Only metadata is returned. Video files are never
    loaded into memory or sent through telemetry.
    """

    if not VIDEO_DIR.exists():
        return {
            "videos": []
        }

    videos = []

    try:
        for path in VIDEO_DIR.iterdir():

            if not path.is_file():
                continue

            if path.suffix.lower() != ".mp4":
                continue

            try:
                stat = path.stat()

                modified = datetime.fromtimestamp(
                    stat.st_mtime,
                    tz=timezone.utc,
                ).isoformat()

                if since and modified <= since:
                    continue

                videos.append(
                    {
                        "name": path.name,
                        "path": path.name,
                        "size": stat.st_size,
                        "modified": modified,
                    }
                )

            except OSError as e:
                logger.warning(
                    f"Unable to inspect video {path}: {e}"
                )

    except Exception as e:
        logger.error(
            f"Unable to scan video directory: {e}"
        )

        return {
            "videos": []
        }

    videos.sort(
        key=lambda video: video["modified"],
        reverse=True,
    )

    return {
        "videos": videos
    }


def process_snapshot_command():
    """
    Check AWS for a pending snapshot request.

    If one exists, read the local snapshots and
    send the result back to AWS.
    """

    command = get_pending_command(
        SERVER_URL,
        DEVICE_ID,
    )

    if not command:
        return

    if command.get("command") != "snapshot_request":
        logger.warning(
            f"Unknown command received: {command}"
        )
        return

    request_id = command.get("request_id")

    if not request_id:
        logger.warning(
            "Snapshot command is missing request_id"
        )
        return

    logger.info(
        f"Processing snapshot request "
        f"{request_id}"
    )

    try:

        offset = int(command.get("offset", 0))
        limit = int(
            command.get(
                "limit",
                MAX_SNAPSHOTS_PER_CATEGORY,
            )
        )

        snapshots = collect_snapshots(
            offset=offset,
            limit=limit,
        )

        success = send_snapshot_result(
            SERVER_URL,
            DEVICE_ID,
            request_id,
            snapshots,
        )

        if success:
            logger.info(
                f"Snapshot request "
                f"{request_id} completed"
            )
        else:
            logger.warning(
                f"Failed to send snapshot result "
                f"for request {request_id}"
            )

    except Exception as e:
        logger.error(
            f"Snapshot command processing failed: {e}"
        )


def process_video_stream_command():
    """
    Check AWS for an on-demand video streaming request.

    Only the requested video is streamed. The video is not
    included in telemetry or stored on AWS.
    """

    command = get_pending_video_stream_command(
        SERVER_URL,
        DEVICE_ID,
    )

    if not command:
        return

    request_id = command.get("request_id")
    video_path = command.get("path")
    range_header = command.get("range")

    if not request_id:
        logger.warning(
            "Video stream command is missing request_id"
        )
        return

    if not video_path:
        logger.warning(
            f"Video stream request {request_id} "
            "is missing video path"
        )
        return

    logger.info(
        f"Processing on-demand video stream "
        f"{request_id}: {video_path}"
    )

    try:
        success = stream_video_file(
            SERVER_URL,
            DEVICE_ID,
            request_id,
            video_path,
            range_header,
        )

        if success:
            logger.info(
                f"Video stream {request_id} completed"
            )
        else:
            logger.warning(
                f"Video stream {request_id} failed"
            )

    except Exception as e:
        logger.exception(
            f"Video stream processing failed "
            f"for {request_id}: {e}"
        )


def process_video_command():
    """
    Check AWS for a pending sliced-video metadata request.

    Only video metadata is returned.
    """

    command = get_pending_video_command(
        SERVER_URL,
        DEVICE_ID,
    )

    if not command:
        return

    request_id = command.get("request_id")

    if not request_id:
        logger.warning(
            "Video command is missing request_id"
        )
        return

    logger.info(
        f"Processing sliced-video request "
        f"{request_id}"
    )

    try:
        since = command.get("since")

        videos = collect_sliced_videos(
            since=since
        )

        success = send_video_result(
            SERVER_URL,
            DEVICE_ID,
            request_id,
            videos,
        )

        if success:
            logger.info(
                f"Sliced-video request "
                f"{request_id} completed: "
                f"{len(videos['videos'])} videos"
            )
        else:
            logger.warning(
                f"Failed to send sliced-video result "
                f"for request {request_id}"
            )

    except Exception as e:
        logger.error(
            f"Sliced-video command processing failed: {e}"
        )

# Background command processing.
command_worker_running = True
video_stream_threads = {}


def _stream_video_background(command):
    request_id = command.get("request_id")
    video_path = command.get("path")
    range_header = command.get("range")

    if not request_id or not video_path:
        logger.warning(
            "Invalid video stream command"
        )
        return

    try:
        logger.info(
            f"Processing on-demand video stream "
            f"{request_id}: {video_path}"
        )

        success = stream_video_file(
            SERVER_URL,
            DEVICE_ID,
            request_id,
            video_path,
            range_header,
        )

        if success:
            logger.info(
                f"Video stream {request_id} completed"
            )
        else:
            logger.warning(
                f"Video stream {request_id} failed"
            )

    except Exception as e:
        logger.exception(
            f"Video stream processing failed "
            f"for {request_id}: {e}"
        )

    finally:
        video_stream_threads.pop(
            request_id,
            None,
        )


def command_worker():
    """
    Process dashboard commands independently from telemetry.

    This worker must never block the telemetry heartbeat.
    """

    logger.info(
        "Command worker started."
    )

    while command_worker_running:

        try:
            # Snapshot commands
            process_snapshot_command()

        except Exception as e:
            logger.exception(
                f"Snapshot command worker error: {e}"
            )

        try:
            # Sliced-video metadata commands
            process_video_command()

        except Exception as e:
            logger.exception(
                f"Video command worker error: {e}"
            )

        try:
            # On-demand video stream command.
            #
            # The actual video transfer runs in another
            # background thread so this worker remains free.
            command = get_pending_video_stream_command(
                SERVER_URL,
                DEVICE_ID,
            )

            if command:
                request_id = command.get("request_id")

                if not request_id:
                    logger.warning(
                        "Video stream command missing request_id"
                    )

                elif request_id not in video_stream_threads:

                    thread = threading.Thread(
                        target=_stream_video_background,
                        args=(command,),
                        daemon=True,
                        name=f"video-stream-{request_id[:8]}",
                    )

                    video_stream_threads[request_id] = thread

                    thread.start()

                    logger.info(
                        f"Started background video stream "
                        f"thread for request_id={request_id}"
                    )

        except Exception as e:
            logger.exception(
                f"Video stream command worker error: {e}"
            )

        # Prevent continuous polling from hammering AWS.
        time.sleep(1.0)


def main():

    logger.info(
        f"Starting telemetry agent | "
        f"device_id={DEVICE_ID} | "
        f"server={SERVER_URL} | "
        f"interval={INTERVAL}s | "
        f"platform={platform.platform()}"
    )

    # Prime psutil CPU counters.
    import psutil

    psutil.cpu_percent(
        interval=None
    )

    psutil.cpu_percent(
        interval=None,
        percpu=True,
    )

    # Start the shared background tegrastats reader.
    tegrastats_reader.start(
        interval_ms=int(
            INTERVAL * 1000
        )
    )

    # -------------------------------------------------
    # IMPORTANT:
    # Command processing is completely independent
    # from the telemetry heartbeat.
    # -------------------------------------------------

    command_thread = threading.Thread(
        target=command_worker,
        daemon=True,
        name="command-worker",
    )

    command_thread.start()

    logger.info(
        "Telemetry heartbeat and command worker started."
    )

    consecutive_failures = 0

    while True:

        start = time.time()

        try:

            # -----------------------------------------
            # TELEMETRY ONLY
            # -----------------------------------------

            payload = collect_telemetry()

            ok = send(
                SERVER_URL,
                payload,
            )

            if ok:

                if consecutive_failures:
                    logger.info(
                        "Server connection restored."
                    )

                consecutive_failures = 0

            else:

                consecutive_failures += 1

        except Exception as e:

            consecutive_failures += 1

            logger.error(
                f"Collection error: {e}"
            )

        if (
            consecutive_failures
            and consecutive_failures % 10 == 0
        ):
            logger.warning(
                f"{consecutive_failures} "
                f"consecutive failures sending "
                f"telemetry."
            )

        # Maintain the telemetry interval.
        elapsed = time.time() - start

        time.sleep(
            max(
                0.0,
                INTERVAL - elapsed,
            )
        )


if __name__ == "__main__":

    try:
        main()

    except KeyboardInterrupt:

        logger.info(
            "Agent stopped by user."
        )
