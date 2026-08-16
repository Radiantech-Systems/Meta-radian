#include "recorder.hpp"
#include "logger.hpp"
#include "file_manager.hpp"
#include "timestamp_manager.hpp"

#include <filesystem>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <thread>
#include <chrono>

Recorder::Recorder()
{
}

bool Recorder::createRecordingDirectory()
{
    FileManager::createRecordingFolder();
    return true;
}

std::string Recorder::buildPipeline()
{
    std::stringstream pipeline;

    pipeline
        << "gst-launch-1.0 "
        << "rtspsrc "
        << "location=rtsp://192.168.1.20:8554/test "
        << "protocols=tcp "
        << "latency=50 "
        << "! "
        << "rtph264depay "
        << "! "
        << "h264parse "
        << "! "
        << "nvv4l2decoder "
        << "! "
        << "nvvidconv "
        << "! "
        << "nvv4l2h264enc "
        << "! "
        << "h264parse "
        << "! "
        << "splitmuxsink "
        << "location="
        << FileManager::getTodayFolder()
        << "/"
        << TimestampManager::currentTime()
        << "_%05d.mp4 "
        << "max-size-time=300000000000 ";

    return pipeline.str();
}
void Recorder::start()
{
    Logger::info("Preparing Recorder");

    createRecordingDirectory();

    // Maximum total recording storage.
    // Change this value when you want a different limit.
    constexpr double MAX_STORAGE_GB = 10.0;

    // Background storage cleanup thread.
    std::thread cleanupThread(
        [MAX_STORAGE_GB]()
        {
            while (true)
            {
                FileManager::cleanupOldRecordings(
                    MAX_STORAGE_GB
                );

                // Check storage every 30 seconds.
                std::this_thread::sleep_for(
                    std::chrono::seconds(30)
                );
            }
        }
    );

    // The recorder runs continuously.
    cleanupThread.detach();

    while (true)
    {
        Logger::info("--------------------------------");

        Logger::info("Building Pipeline");

        std::string pipeline = buildPipeline();

        Logger::info("Pipeline Ready");

        Logger::info(pipeline);

        Logger::info("Recording Started");

        int ret = system(pipeline.c_str());

        Logger::error(
            "Pipeline Stopped. Exit Code = " +
            std::to_string(ret)
        );

        Logger::info(
            "Reconnecting in 5 seconds..."
        );

        std::this_thread::sleep_for(
            std::chrono::seconds(5)
        );
    }
}
