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
        << "location='rtsp://admin:Radian-123@192.168.1.250:554/video/live?channel=1&subtype=0' "   
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
        << "location='"
        << FileManager::getTodayFolder()
        << "/"
        << TimestampManager::currentTime()
        << "_%05d.mp4' "
        << "max-size-time=300000000000 ";
    return pipeline.str();
}
void Recorder::cleanupOldVideos()
{
    namespace fs = std::filesystem;

    constexpr double MAX_DISK_USAGE_PERCENT = 95.0;
    constexpr double TARGET_DISK_USAGE_PERCENT = 80.0;

    const fs::path recordingRoot =
        "/root/video_recorder/recordings";

    try
    {
        while (true)
        {
            auto space = fs::space(recordingRoot);

            if (space.capacity == 0)
            {
                Logger::error("Unable to determine disk capacity");
                return;
            }

            double usagePercent =
                (static_cast<double>(space.capacity - space.available) /
                 static_cast<double>(space.capacity)) * 100.0;

            Logger::info(
                "Disk usage: " +
                std::to_string(usagePercent) +
                "%"
            );

            if (usagePercent < MAX_DISK_USAGE_PERCENT)
                return;

            fs::path oldestFile;
            fs::file_time_type oldestTime;
            bool found = false;

            for (const auto& entry :
                 fs::recursive_directory_iterator(recordingRoot))
            {
                if (!entry.is_regular_file())
                    continue;

                if (entry.path().extension() != ".mp4")
                    continue;

                auto fileTime = fs::last_write_time(entry.path());

                if (!found || fileTime < oldestTime)
                {
                    oldestTime = fileTime;
                    oldestFile = entry.path();
                    found = true;
                }
            }

            if (!found)
            {
                Logger::info("No MP4 recordings available for cleanup");
                return;
            }

            Logger::info(
                "Deleting oldest recording: " +
                oldestFile.string()
            );

            fs::remove(oldestFile);

            space = fs::space(recordingRoot);

            usagePercent =
                (static_cast<double>(space.capacity - space.available) /
                 static_cast<double>(space.capacity)) * 100.0;

            Logger::info(
                "Disk usage after cleanup: " +
                std::to_string(usagePercent) +
                "%"
            );

            if (usagePercent <= TARGET_DISK_USAGE_PERCENT)
            {
                Logger::info(
                    "Disk cleanup completed. Usage is now " +
                    std::to_string(usagePercent) +
                    "%"
                );
                return;
            }
        }
    }
    catch (const std::exception& e)
    {
        Logger::error(
            std::string("Storage cleanup error: ") + e.what()
        );
    }
}

void Recorder::start()
{
    Logger::info("Preparing Recorder");

    createRecordingDirectory();

    // Background storage cleanup thread.
    std::thread cleanupThread(
        [this]()
        {
            while (true)
            {
                cleanupOldVideos();

                // Check disk usage every 30 seconds.
                std::this_thread::sleep_for(
                    std::chrono::seconds(30)
                );
            }
        }
    );

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
