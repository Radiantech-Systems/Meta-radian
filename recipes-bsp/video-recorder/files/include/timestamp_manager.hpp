#ifndef VIDEO_RECORDER_TIMESTAMP_MANAGER_HPP
#define VIDEO_RECORDER_TIMESTAMP_MANAGER_HPP

#include <string>

class TimestampManager
{
public:
    static std::string currentDate();
    static std::string currentTime();
    static std::string currentDateTime();
};

#endif
