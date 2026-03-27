#include "Logger.h"

#ifdef _WIN32
#include <windows.h>
#include <string>
#endif

void logMessage(LogLevel level, const char* message) {
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    WORD color;

    switch (level) {
    case DANGER:   color = FOREGROUND_RED | FOREGROUND_INTENSITY; break;
    case WARNING: color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
    case INFO:    color = FOREGROUND_BLUE | FOREGROUND_INTENSITY; break;
    case SUCCESS: color = FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
    default:      color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; break;
    }

    SetConsoleTextAttribute(hConsole, color);
    printf("%s\n", message);
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
#else
    const char* colorCode;
    switch (level) {
    case DANGER:   colorCode = "\033[31m"; break;
    case WARNING: colorCode = "\033[33m"; break;
    case INFO:    colorCode = "\033[34m"; break; 
    case SUCCESS: colorCode = "\033[32m"; break;
    default:      colorCode = "\033[0m";  break;
    }
    printf("%s%s\033[0m\n", colorCode, message);
#endif
}