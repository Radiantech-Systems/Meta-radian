bin_PROGRAMS = video-recorder

video_recorder_SOURCES = \
    src/main.cpp \
    src/recorder.cpp \
    src/timestamp_manager.cpp \
    src/file-manager.cpp \
    src/logger.cpp \
    src/config.cpp

video_recorder_CPPFLAGS = \
    -I$(srcdir)/include

video_recorder_CXXFLAGS = \
    -std=c++17 \
    -Wall \
    -Wextra \
    -pthread

video_recorder_LDFLAGS = \
    -pthread
