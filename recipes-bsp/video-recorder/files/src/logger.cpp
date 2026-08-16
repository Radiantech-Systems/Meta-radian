#include "logger.hpp"

#include <fstream>
#include <iostream>
#include <ctime>

static std::string getTime()
{
    std::time_t now = std::time(nullptr);

    char buffer[64];

    std::strftime(
        buffer,
        sizeof(buffer),
        "%Y-%m-%d %H:%M:%S",
        std::localtime(&now));

    return buffer;
}

void Logger::write(
    const std::string& level,
    const std::string& message)
{
    std::ofstream log(
        "/root/video_recorder/logs/video_recorder.log",
        std::ios::app);

    log
        << "["
        << getTime()
        << "] "
        << level
        << " "
        << message
        << std::endl;

    std::cout
        << level
        << " "
        << message
        << std::endl;
}

void Logger::info(const std::string& message)
{
    write("[INFO]", message);
}

void Logger::error(const std::string& message)
{
    write("[ERROR]", message);
}
