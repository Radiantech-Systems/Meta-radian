#ifndef VIDEO_RECORDER_CONFIG_HPP
#define VIDEO_RECORDER_CONFIG_HPP

#include <string>

struct Config
{
    std::string rtspUrl;
    std::string recordingDirectory;
    std::string logDirectory;

    int segmentDurationSeconds;
    int latency;

    std::string protocol;

    bool autoReconnect;
    int reconnectDelaySeconds;

    bool display;
    bool record;
};

class ConfigLoader
{
public:
    static Config load(const std::string& path);
};

#endif
