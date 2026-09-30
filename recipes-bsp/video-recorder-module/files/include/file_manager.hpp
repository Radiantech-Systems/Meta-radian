
#ifndef VIDEO_RECORDER_FILE_MANAGER_HPP
#define VIDEO_RECORDER_FILE_MANAGER_HPP

#include <string>

class FileManager
{
public:
    static bool createRecordingFolder();

    static std::string getTodayFolder();

    static std::string getRecordingPath();

    static unsigned long long getRecordingsSize();

    static std::string getOldestRecording();

    static void cleanupOldRecordings(
        double maxStorageGB
    );
};

#endif
