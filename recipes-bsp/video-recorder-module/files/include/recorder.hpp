#ifndef RECORDER_HPP
#define RECORDER_HPP

#include <string>

class Recorder
{
public:
    Recorder();

    void start();

private:
    void createRecordingDirectory();
    std::string buildPipeline();

    std::string getRTSPUrl();

    double getDiskUsagePercent();
    std::string findOldestRecording();

    void cleanupOldVideos();
};

#endif // RECORDER_HPP
