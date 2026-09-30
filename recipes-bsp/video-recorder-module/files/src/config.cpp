#include "config.hpp"

Config ConfigLoader::load(const std::string&)
{
    Config cfg;

    cfg.rtspUrl =
    "rtsp://admin:Radian-123@192.168.1.250:554/video/live?channel=1&subtype=0";
    cfg.recordingDirectory = "/root/video_recorder/recordings";

    cfg.logDirectory = "/root/video_recorder/logs";

    cfg.segmentDurationSeconds = 300;

    cfg.latency = 50;

    cfg.protocol = "tcp";

    cfg.autoReconnect = true;

    cfg.reconnectDelaySeconds = 5;

    cfg.display = true;

    cfg.record = true;

    return cfg;
}
