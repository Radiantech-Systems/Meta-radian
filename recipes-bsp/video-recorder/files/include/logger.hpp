#ifndef VIDEO_RECORDER_LOGGER_HPP
#define VIDEO_RECORDER_LOGGER_HPP

#include <string>

class Logger
{
public:
    static void write(
        const std::string& level,
        const std::string& message
    );

    static void info(
        const std::string& message
    );

    static void error(
        const std::string& message
    );
};

#endif
