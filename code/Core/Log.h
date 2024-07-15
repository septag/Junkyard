#pragma once

#include "Base.h"

enum class LogLevel
{
    Default = 0,
    Error,
    Warning,
    Info,
    Verbose,
    Debug,
    _Count
};

struct LogEntry
{
    LogLevel    type;
    uint32      channels;
    uint32      textLen;
    uint32      sourceFileLen;
    uint32      line;
    const char* text;
    const char* sourceFile;
};

// NOTE: custom callbacks should take care of thread-safety for their data
using LogCallback = void(*)(const LogEntry& entry, void* userData);

namespace Log
{
    API void RegisterCallback(LogCallback callback, void* userData);
    API void UnregisterCallback(LogCallback callback);
    API void SetSettings(LogLevel logLevel, bool breakOnErrors, bool treatWarningsAsErrors);

    namespace _private
    {
        API void PrintInfo(uint32 channels, const char* source_file, uint32 line, const char* fmt, ...);
        API void PrintDebug(uint32 channels, const char* source_file, uint32 line, const char* fmt, ...);
        API void PrintVerbose(uint32 channels, const char* source_file, uint32 line, const char* fmt, ...);
        API void PrintWarning(uint32 channels, const char* source_file, uint32 line, const char* fmt, ...);
        API void PrintError(uint32 channels, const char* source_file, uint32 line, const char* fmt, ...);
    }
};

// Use macros to include source location automatically
#define LOG_INFO(_text, ...)      Log::_private::PrintInfo(0, __FILE__, __LINE__, _text, ##__VA_ARGS__)
#define LOG_DEBUG(_text, ...)     Log::_private::PrintDebug(0, __FILE__, __LINE__, _text, ##__VA_ARGS__)
#define LOG_VERBOSE(_text, ...)   Log::_private::PrintVerbose(0, __FILE__, __LINE__, _text, ##__VA_ARGS__)
#define LOG_WARNING(_text, ...)   Log::_private::PrintWarning(0, __FILE__, __LINE__, _text, ##__VA_ARGS__)
#define LOG_ERROR(_text, ...)     Log::_private::PrintError(0, __FILE__, __LINE__, _text, ##__VA_ARGS__)

