#pragma once
#include <cstdio>

enum LogLevel {
    DANGER,
    WARNING,
    INFO,
    SUCCESS
};

void logMessage(LogLevel level, const char* message);
#pragma once
