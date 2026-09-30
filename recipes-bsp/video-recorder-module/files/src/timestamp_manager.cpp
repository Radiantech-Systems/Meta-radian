#include "timestamp_manager.hpp"

#include <ctime>

std::string TimestampManager::currentDate()
{
    time_t now=time(nullptr);

    char buffer[32];

    strftime(buffer,
             sizeof(buffer),
             "%Y-%m-%d",
             localtime(&now));

    return buffer;
}

std::string TimestampManager::currentTime()
{
    time_t now=time(nullptr);

    char buffer[32];

    strftime(buffer,
             sizeof(buffer),
             "%H-%M-%S",
             localtime(&now));

    return buffer;
}

std::string TimestampManager::currentDateTime()
{
    return currentDate()+"_"+currentTime();
}
