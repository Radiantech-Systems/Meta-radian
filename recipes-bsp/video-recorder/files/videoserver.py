from flask import Flask, jsonify, send_from_directory
from flask_cors import CORS
import os

app = Flask(__name__)
CORS(app)

VIDEO_ROOT = "/root/video_recorder/events/videos"


@app.route("/")
def home():
    return "Jetson Video Server is running"


@app.route("/videos")
def videos():
    video_list = []

    for root, dirs, files in os.walk(VIDEO_ROOT):
        for filename in files:
            if filename.endswith(".mp4"):
                full_path = os.path.join(root, filename)

                relative_path = os.path.relpath(
                    full_path,
                    VIDEO_ROOT
                )

                video_list.append({
                    "name": filename,
                    "path": relative_path
                })

    video_list.sort(
        key=lambda x: x["path"],
        reverse=True
    )

    return jsonify(video_list)


@app.route("/video/<path:filename>")
def video(filename):
    directory = os.path.dirname(
        os.path.join(VIDEO_ROOT, filename)
    )

    file_name = os.path.basename(filename)

    return send_from_directory(
        directory,
        file_name
    )


if __name__ == "__main__":
    app.run(
        host="0.0.0.0",
        port=5000,
        threaded=True
    )
