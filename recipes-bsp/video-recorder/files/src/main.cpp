#include <iostream>

#include "config.hpp"
#include "logger.hpp"
#include "recorder.hpp"

int main()
{
    Logger::info("=================================");
    Logger::info("Jetson Video Recorder");
    Logger::info("Version 1.0");
    Logger::info("=================================");

    Config cfg =
        ConfigLoader::load("config/config.json");

    std::cout
        << "RTSP URL : "
        << cfg.rtspUrl
        << std::endl;

    std::cout
        << "Recording Directory : "
        << cfg.recordingDirectory
        << std::endl;

    std::cout
        << "Segment Duration : "
        << cfg.segmentDurationSeconds
        << " Seconds"
        << std::endl;

    Recorder recorder;

    recorder.start();

    return 0;
}
