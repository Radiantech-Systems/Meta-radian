#ifndef VIDEO_RECORDER_RECORDER_HPP
#define VIDEO_RECORDER_RECORDER_HPP

#include <string>

class Recorder
{
public:
    Recorder();

    bool createRecordingDirectory();

    std::string buildPipeline();

    void start();
};

#endif
