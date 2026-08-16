#include "file_manager.hpp"

#include "timestamp_manager.hpp"
#include "logger.hpp"

#include <filesystem>
#include <vector>
#include <algorithm>
#include <string>

namespace fs = std::filesystem;

static const std::string RECORDINGS_ROOT =
    "/root/video_recorder/recordings";


bool FileManager::createRecordingFolder()
{
    try
    {
        fs::create_directories(
            RECORDINGS_ROOT + "/" +
            TimestampManager::currentDate()
        );

        return true;
    }
    catch (const fs::filesystem_error& e)
    {
        Logger::error(
            "Failed to create recording folder: " +
            std::string(e.what())
        );

        return false;
    }
}


std::string FileManager::getTodayFolder()
{
    return RECORDINGS_ROOT + "/" +
           TimestampManager::currentDate();
}


std::string FileManager::getRecordingPath()
{
    return getTodayFolder() + "/" +
           TimestampManager::currentTime() +
           ".mp4";
}


unsigned long long FileManager::getRecordingsSize()
{
    unsigned long long totalSize = 0;

    try
    {
        if (!fs::exists(RECORDINGS_ROOT))
            return 0;

        for (const auto& entry :
             fs::recursive_directory_iterator(RECORDINGS_ROOT))
        {
            if (!entry.is_regular_file())
                continue;

            if (entry.path().extension() != ".mp4")
                continue;

            try
            {
                totalSize += fs::file_size(entry.path());
            }
            catch (...)
            {
                // File may be currently changing/deleted.
                continue;
            }
        }
    }
    catch (const fs::filesystem_error& e)
    {
        Logger::error(
            "Failed to calculate recording storage: " +
            std::string(e.what())
        );
    }

    return totalSize;
}


std::string FileManager::getOldestRecording()
{
    fs::path oldestFile;
    fs::file_time_type oldestTime;

    bool found = false;

    try
    {
        if (!fs::exists(RECORDINGS_ROOT))
            return "";

        for (const auto& entry :
             fs::recursive_directory_iterator(RECORDINGS_ROOT))
        {
            if (!entry.is_regular_file())
                continue;

            if (entry.path().extension() != ".mp4")
                continue;

            try
            {
                auto currentTime =
                    fs::last_write_time(entry.path());

                if (!found || currentTime < oldestTime)
                {
                    oldestTime = currentTime;
                    oldestFile = entry.path();
                    found = true;
                }
            }
            catch (...)
            {
                continue;
            }
        }
    }
    catch (const fs::filesystem_error& e)
    {
        Logger::error(
            "Failed to find oldest recording: " +
            std::string(e.what())
        );
    }

    if (!found)
        return "";

    return oldestFile.string();
}


void FileManager::cleanupOldRecordings(
    double maxStorageGB
)
{
    const unsigned long long maxBytes =
        static_cast<unsigned long long>(
            maxStorageGB *
            1024.0 *
            1024.0 *
            1024.0
        );

    unsigned long long currentSize =
        getRecordingsSize();

    Logger::info(
        "Recording storage: " +
        std::to_string(
            currentSize / (1024.0 * 1024.0 * 1024.0)
        ) +
        " GB / " +
        std::to_string(maxStorageGB) +
        " GB"
    );

    while (currentSize > maxBytes)
    {
        std::string oldest =
            getOldestRecording();

        if (oldest.empty())
        {
            Logger::error(
                "Storage limit exceeded but no MP4 "
                "file could be found for deletion."
            );

            break;
        }

        try
        {
            auto fileSize = fs::file_size(oldest);

            Logger::info(
                "Deleting oldest recording: " +
                oldest
            );

            fs::remove(oldest);

            currentSize =
                (currentSize > fileSize)
                ? currentSize - fileSize
                : 0;
        }
        catch (const fs::filesystem_error& e)
        {
            Logger::error(
                "Failed to delete recording: " +
                std::string(e.what())
            );

            break;
        }
    }
}
