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
API void logRegisterCallback(LogCallback callback, void* userData);
API void logUnregisterCallback(LogCallback callback);
API void logSetSettings(LogLevel logLevel, bool breakOnErrors, bool treatWarningsAsErrors);

namespace _private
{
    API void logPrintInfo(uint32 channels, const char* source_file, uint32 line, const char* fmt, ...);
    API void logPrintDebug(uint32 channels, const char* source_file, uint32 line, const char* fmt, ...);
    API void logPrintVerbose(uint32 channels, const char* source_file, uint32 line, const char* fmt, ...);
    API void logPrintWarning(uint32 channels, const char* source_file, uint32 line, const char* fmt, ...);
    API void logPrintError(uint32 channels, const char* source_file, uint32 line, const char* fmt, ...);
}

// Use macros to include source location automatically
#define logInfo(_text, ...)      _private::logPrintInfo(0, __FILE__, __LINE__, _text, ##__VA_ARGS__)
#define logDebug(_text, ...)     _private::logPrintDebug(0, __FILE__, __LINE__, _text, ##__VA_ARGS__)
#define logVerbose(_text, ...)   _private::logPrintVerbose(0, __FILE__, __LINE__, _text, ##__VA_ARGS__)
#define logWarning(_text, ...)   _private::logPrintWarning(0, __FILE__, __LINE__, _text, ##__VA_ARGS__)
#define logError(_text, ...)     _private::logPrintError(0, __FILE__, __LINE__, _text, ##__VA_ARGS__)

