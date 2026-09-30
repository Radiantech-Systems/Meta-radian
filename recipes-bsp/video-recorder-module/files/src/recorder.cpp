#include "recorder.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <sys/statvfs.h>

#include <nlohmann/json.hpp>

#include "file_manager.hpp"
#include "timestamp_manager.hpp"

using json = nlohmann::json;

namespace fs = std::filesystem;

namespace
{
    // Camera discovery JSON
    constexpr const char *CAMERA_JSON_FILE =
        "/var/lib/camera-discovery/cameras.json";

    // Disk cleanup settings
    constexpr double MAX_DISK_USAGE_PERCENT = 95.0;
    constexpr double TARGET_DISK_USAGE_PERCENT = 80.0;

    // Cleanup check interval
    constexpr int CLEANUP_INTERVAL_SECONDS = 30;

    // Recording directory
    constexpr const char *RECORDING_ROOT =
        "/root/video_recorder/recordings";
}


// ============================================================
// Constructor
// ============================================================

Recorder::Recorder()
{
}


// ============================================================
// Get RTSP URL from camera discovery JSON
// ============================================================

std::string Recorder::getRTSPUrl()
{
    std::ifstream file(CAMERA_JSON_FILE);

    if (!file.is_open())
    {
        std::cerr
            << "[Recorder] Failed to open camera JSON: "
            << CAMERA_JSON_FILE
            << std::endl;

        return "";
    }

    try
    {
        json data;
        file >> data;

        if (!data.contains("cameras") ||
            !data["cameras"].is_array())
        {
            std::cerr
                << "[Recorder] Invalid camera JSON: "
                << "cameras array not found"
                << std::endl;

            return "";
        }

        for (const auto &camera : data["cameras"])
        {
            // Check camera status
            if (!camera.contains("status") ||
                camera["status"] != "online")
            {
                continue;
            }

            // Check RTSP information
            if (!camera.contains("rtsp") ||
                !camera["rtsp"].is_object())
            {
                continue;
            }

            if (!camera["rtsp"].contains("valid") ||
                !camera["rtsp"]["valid"].get<bool>())
            {
                continue;
            }

            if (!camera["rtsp"].contains("url") ||
                !camera["rtsp"]["url"].is_string())
            {
                continue;
            }

            std::string rtspUrl =
                camera["rtsp"]["url"].get<std::string>();

            if (rtspUrl.empty())
            {
                continue;
            }

            std::string cameraId = "unknown";
            std::string cameraIp = "unknown";

            if (camera.contains("id") &&
                camera["id"].is_string())
            {
                cameraId =
                    camera["id"].get<std::string>();
            }

            if (camera.contains("ip") &&
                camera["ip"].is_string())
            {
                cameraIp =
                    camera["ip"].get<std::string>();
            }

            std::cout
                << "[Recorder] Camera found"
                << " ID=" << cameraId
                << " IP=" << cameraIp
                << std::endl;

            std::cout
                << "[Recorder] RTSP URL: "
                << rtspUrl
                << std::endl;

            return rtspUrl;
        }

        std::cerr
            << "[Recorder] No valid online camera found"
            << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr
            << "[Recorder] Failed to parse camera JSON: "
            << e.what()
            << std::endl;
    }

    return "";
}


// ============================================================
// Create recording directory
// ============================================================

void Recorder::createRecordingDirectory()
{
    FileManager::createRecordingFolder();
}


// ============================================================
// Build GStreamer recording pipeline
// ============================================================

std::string Recorder::buildPipeline()
{
    // Get RTSP URL dynamically from cameras.json
    std::string rtspUrl = getRTSPUrl();

    if (rtspUrl.empty())
    {
        std::cerr
            << "[Recorder] No valid RTSP URL available"
            << std::endl;

        return "";
    }

    std::string recordingFolder =
        FileManager::getTodayFolder();

    std::string timestamp =
        TimestampManager::currentTime();

    std::string outputFile =
        recordingFolder +
        "/" +
        timestamp +
        "_%05d.mp4";

    std::string pipeline;

    pipeline =
        "gst-launch-1.0 "
        "rtspsrc "
        "location='" + rtspUrl + "' "
        "protocols=tcp "
        "latency=50 "
        "! rtph264depay "
        "! h264parse "
        "! nvv4l2decoder "
        "! nvvidconv "
        "! nvv4l2h264enc "
        "! h264parse "
        "! splitmuxsink "
        "location='" + outputFile + "' "
        "max-size-time=300000000000";

    return pipeline;
}


// ============================================================
// Get root filesystem usage
// ============================================================

double Recorder::getDiskUsagePercent()
{
    struct statvfs stat;

    if (statvfs("/", &stat) != 0)
    {
        std::cerr
            << "[Cleanup] Failed to get filesystem statistics"
            << std::endl;

        return -1.0;
    }

    unsigned long long totalBytes =
        static_cast<unsigned long long>(stat.f_blocks) *
        static_cast<unsigned long long>(stat.f_frsize);

    unsigned long long availableBytes =
        static_cast<unsigned long long>(stat.f_bavail) *
        static_cast<unsigned long long>(stat.f_frsize);

    if (totalBytes == 0)
    {
        return -1.0;
    }

    unsigned long long usedBytes =
        totalBytes - availableBytes;

    double usagePercent =
        (static_cast<double>(usedBytes) /
         static_cast<double>(totalBytes)) *
        100.0;

    return usagePercent;
}


// ============================================================
// Find oldest MP4 recording
// ============================================================

std::string Recorder::findOldestRecording()
{
    std::string oldestFile;
    std::filesystem::file_time_type oldestTime;

    bool found = false;

    try
    {
        if (!fs::exists(RECORDING_ROOT))
        {
            std::cerr
                << "[Cleanup] Recording directory does not exist: "
                << RECORDING_ROOT
                << std::endl;

            return "";
        }

        for (const auto &entry :
             fs::recursive_directory_iterator(RECORDING_ROOT))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }

            if (entry.path().extension() != ".mp4")
            {
                continue;
            }

            auto fileTime =
                fs::last_write_time(entry.path());

            if (!found || fileTime < oldestTime)
            {
                oldestTime = fileTime;
                oldestFile = entry.path().string();
                found = true;
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr
            << "[Cleanup] Error searching recordings: "
            << e.what()
            << std::endl;

        return "";
    }

    return oldestFile;
}


// ============================================================
// Delete old recordings when root filesystem reaches 95%
// ============================================================

void Recorder::cleanupOldVideos()
{
    while (true)
    {
        double usagePercent =
            getDiskUsagePercent();

        if (usagePercent < 0)
        {
            std::this_thread::sleep_for(
                std::chrono::seconds(
                    CLEANUP_INTERVAL_SECONDS));

            continue;
        }

        std::cout
            << "[Cleanup] Root filesystem usage: "
            << usagePercent
            << "%"
            << std::endl;


        // ----------------------------------------------------
        // Below 95% -> nothing to delete
        // ----------------------------------------------------

        if (usagePercent < MAX_DISK_USAGE_PERCENT)
        {
            std::cout
                << "[Cleanup] Usage below "
                << MAX_DISK_USAGE_PERCENT
                << "%. No cleanup required."
                << std::endl;

            std::this_thread::sleep_for(
                std::chrono::seconds(
                    CLEANUP_INTERVAL_SECONDS));

            continue;
        }


        // ----------------------------------------------------
        // 95% reached -> start deleting oldest recordings
        // ----------------------------------------------------

        std::cout
            << "[Cleanup] Disk usage reached "
            << MAX_DISK_USAGE_PERCENT
            << "%. Starting cleanup."
            << std::endl;


        while (usagePercent > TARGET_DISK_USAGE_PERCENT)
        {
            std::string oldestFile =
                findOldestRecording();

            if (oldestFile.empty())
            {
                std::cerr
                    << "[Cleanup] No MP4 recordings available "
                    << "to delete."
                    << std::endl;

                break;
            }


            // Get file size before deletion
            std::uintmax_t fileSize = 0;

            try
            {
                fileSize =
                    fs::file_size(oldestFile);
            }
            catch (...)
            {
            }


            std::cout
                << "[Cleanup] Deleting oldest recording: "
                << oldestFile
                << std::endl;


            try
            {
                if (fs::remove(oldestFile))
                {
                    std::cout
                        << "[Cleanup] Deleted successfully: "
                        << oldestFile
                        << " ("
                        << fileSize / (1024.0 * 1024.0)
                        << " MB)"
                        << std::endl;
                }
                else
                {
                    std::cerr
                        << "[Cleanup] Failed to delete: "
                        << oldestFile
                        << std::endl;

                    break;
                }
            }
            catch (const std::exception &e)
            {
                std::cerr
                    << "[Cleanup] Delete error: "
                    << e.what()
                    << std::endl;

                break;
            }


            // ------------------------------------------------
            // IMPORTANT:
            // Recalculate root filesystem usage after EVERY
            // deletion.
            // ------------------------------------------------

            usagePercent =
                getDiskUsagePercent();

            if (usagePercent < 0)
            {
                break;
            }

            std::cout
                << "[Cleanup] Current root usage: "
                << usagePercent
                << "%"
                << std::endl;
        }


        // ----------------------------------------------------
        // Cleanup finished
        // ----------------------------------------------------

        usagePercent =
            getDiskUsagePercent();

        if (usagePercent >= 0)
        {
            std::cout
                << "[Cleanup] Cleanup finished. "
                << "Root filesystem usage: "
                << usagePercent
                << "%"
                << std::endl;
        }


        std::this_thread::sleep_for(
            std::chrono::seconds(
                CLEANUP_INTERVAL_SECONDS));
    }
}


// ============================================================
// Start recorder
// ============================================================

void Recorder::start()
{
    createRecordingDirectory();


    // Start cleanup thread
    std::thread(
        &Recorder::cleanupOldVideos,
        this
    ).detach();


    while (true)
    {
        std::string pipeline =
            buildPipeline();

        // ----------------------------------------------------
        // No valid camera/RTSP URL
        // ----------------------------------------------------

        if (pipeline.empty())
        {
            std::cerr
                << "[Recorder] No valid camera available."
                << " Retrying in 5 seconds..."
                << std::endl;

            std::this_thread::sleep_for(
                std::chrono::seconds(5));

            continue;
        }


        std::cout
            << "[Recorder] Starting recording pipeline:"
            << std::endl;

        std::cout
            << pipeline
            << std::endl;


        int result =
            std::system(
                pipeline.c_str());


        // ----------------------------------------------------
        // Pipeline stopped/crashed
        // ----------------------------------------------------

        std::cerr
            << "[Recorder] Recording pipeline exited "
            << "with status: "
            << result
            << std::endl;

        std::cerr
            << "[Recorder] Restarting in 5 seconds..."
            << std::endl;


        std::this_thread::sleep_for(
            std::chrono::seconds(5));
    }
}
