#include "Log.h"

#include <stdarg.h> // va_list
#include <stdio.h>  // puts

#include "TracyHelper.h"
#include "System.h"
#include "Debug.h"
#include "Arrays.h"
#include "Allocators.h"

#if PLATFORM_MOBILE || PLATFORM_OSX
    #define TERM_COLOR_RESET     ""
    #define TERM_COLOR_RED       ""
    #define TERM_COLOR_YELLOW    ""
    #define TERM_COLOR_GREEN     ""
    #define TERM_COLOR_CYAN      ""
    #define TERM_COLOR_WHITE     ""
#else
    #define TERM_COLOR_RESET     "\033[0m"
    #define TERM_COLOR_RED       "\033[31m"
    #define TERM_COLOR_YELLOW    "\033[33m"
    #define TERM_COLOR_GREEN     "\033[32m"
    #define TERM_COLOR_CYAN      "\033[36m"
    #define TERM_COLOR_WHITE     "\033[97m"
#endif

#ifndef DEFAULT_LOG_LEVEL
    #if CONFIG_DEV_MODE
        #define DEFAULT_LOG_LEVEL LogLevel::Debug
    #else
        #define DEFAULT_LOG_LEVEL LogLevel::Info
    #endif
#endif

static constexpr uint32 LOG_MAX_MESSAGE_CHARS = 2048;

struct LogContext
{
    StaticArray<Pair<LogCallback, void*>, 8> callbacks;
    LogLevel logLevel = DEFAULT_LOG_LEVEL;
    bool breakOnErrors;
    bool treatWarningsAsErrors;
};

static LogContext gLog;

// corrosponds to EngineLogLevel
static const char* LOG_ENTRY_TYPES[static_cast<uint32>(LogLevel::_Count)] = { 
    "", 
    "[ERR] ",
    "[WRN] ",
    "", 
    "", 
    "[DBG] "
};

namespace Log
{
    void SetSettings(LogLevel logLevel, bool breakOnErrors, bool treatWarningsAsErrors)
    {
        ASSERT(logLevel != LogLevel::Default);

        gLog.logLevel = logLevel;
        gLog.breakOnErrors = breakOnErrors;
        gLog.treatWarningsAsErrors = treatWarningsAsErrors;
    }

    static void _PrintToTerminal(const LogEntry& entry)
    {
        uint32 newSize = entry.textLen + 128;

        MemTempAllocator tmp;
        char* text = tmp.MallocTyped<char>(newSize);

        if (text) {
            const char* openFmt = "";
            const char* closeFmt = "";

            // terminal coloring
            switch (entry.type) {
            case LogLevel::Info:    openFmt = TERM_COLOR_WHITE; closeFmt = TERM_COLOR_WHITE; break;
            case LogLevel::Debug:	openFmt = TERM_COLOR_CYAN; closeFmt = TERM_COLOR_RESET; break;
            case LogLevel::Verbose:	openFmt = TERM_COLOR_RESET; closeFmt = TERM_COLOR_RESET; break;
            case LogLevel::Warning:	openFmt = TERM_COLOR_YELLOW; closeFmt = TERM_COLOR_RESET; break;
            case LogLevel::Error:	openFmt = TERM_COLOR_RED; closeFmt = TERM_COLOR_RESET; break;
            default:			    break;
            }

            Str::PrintFmt(text, newSize, "%s%s%s%s", 
                openFmt, 
                LOG_ENTRY_TYPES[static_cast<uint32>(entry.type)], 
                entry.text, closeFmt);
        
            puts(text);
        }
        else {
            ASSERT_ALWAYS(0, "Not enough stack memory: %u bytes", newSize);
        }
    }

    #if PLATFORM_ANDROID
    static void _PrintToAndroidLog(const LogEntry& entry)
    {
        OSAndroidLogType androidLogType;
        switch (entry.type) {
        case LogLevel::Info:	androidLogType = OSAndroidLogType::Info;        break;
        case LogLevel::Debug:	androidLogType = OSAndroidLogType::Debug;       break;
        case LogLevel::Verbose:	androidLogType = OSAndroidLogType::Verbose;     break;
        case LogLevel::Warning:	androidLogType = OSAndroidLogType::Warn;        break;
        case LogLevel::Error:	androidLogType = OSAndroidLogType::Error;       break;
        default:			    androidLogType = OSAndroidLogType::Unknown;
        }
        
        OS::AndroidPrintToLog(androidLogType, CONFIG_APP_NAME, entry.text);
    }
    #endif // PLATFORM_ANDROID

    static void _PrintToDebugger(const LogEntry& entry)
    {
        if constexpr(!PLATFORM_WINDOWS)
            return;

        uint32 newSize = entry.textLen + 128;
        MemTempAllocator tmp;
        char* text = tmp.MallocTyped<char>(newSize);

        if (text) {
            char source[PATH_CHARS_MAX];
            if (entry.sourceFile)
                Str::PrintFmt(source, sizeof(source), "%s(%d): ", entry.sourceFile, entry.line);
            else 
                source[0] = '\0';
            Str::PrintFmt(text, newSize, "%s%s%s", source, LOG_ENTRY_TYPES[static_cast<uint32>(entry.type)], entry.text);
            Debug::PrintLine(text);
        }
        else {
            ASSERT_ALWAYS(0, "Not enough stack memory: %u bytes", newSize);
        }
    }

    #ifdef TRACY_ENABLE
    static void _PrintToTracy(const LogEntry& entry)
    {
        // terminal coloring
        uint32 color;
        switch (entry.type) {
        case LogLevel::Info:	color = 0xFFFFFF; break;
        case LogLevel::Debug:	color = 0xC8C8C8; break;
        case LogLevel::Verbose:	color = 0x808080; break;
        case LogLevel::Warning:	color = 0xFFFF00; break;
        case LogLevel::Error:	color = 0xFF0000; break;
        default:			    color = 0xFFFFFF; break;
        }

        TracyCMessageC(entry.text, entry.textLen, color);
    }
    #endif

    static void _DispatchLogEntry(const LogEntry& entry)
    {
        _PrintToTerminal(entry); 
        _PrintToDebugger(entry);
        #ifdef TRACY_ENABLE
            _PrintToTracy(entry);
        #endif
        #if PLATFORM_ANDROID
            _PrintToAndroidLog(entry);
        #endif

        for (Pair<LogCallback, void*> c : gLog.callbacks)
            c.first(entry, c.second);

        if (entry.type == LogLevel::Error && gLog.breakOnErrors) {
            ASSERT_MSG(0, "Breaking on error");
        }
    }

    void _private::PrintInfo(uint32 channels, const char* sourceFile, uint32 line, const char* fmt, ...)
    {
        if (gLog.logLevel < LogLevel::Info)
            return;

        MemTempAllocator tmp;
        uint32 fmtLen = Str::Len(fmt) + LOG_MAX_MESSAGE_CHARS;
        char* text = tmp.MallocTyped<char>(fmtLen);

        va_list args;
        va_start(args, fmt);
        Str::PrintFmtArgs(text, fmtLen, fmt, args);
        va_end(args);

        _DispatchLogEntry({
            .type = LogLevel::Info,
            .channels = channels,
            .textLen = Str::Len(text),
            .sourceFileLen = sourceFile ? Str::Len(sourceFile) : 0,
            .line = line,
            .text = text,
            .sourceFile = sourceFile
        });
    }

    // LogDebug only works in none final builds
    void _private::PrintDebug(uint32 channels, const char* sourceFile, uint32 line, const char* fmt, ...)
    {
        #if !CONFIG_FINAL_BUILD
            if (gLog.logLevel < LogLevel::Debug)
                return;
        
            MemTempAllocator tmp;
            uint32 fmtLen = Str::Len(fmt) + LOG_MAX_MESSAGE_CHARS;
            char* text = tmp.MallocTyped<char>(fmtLen);

            va_list args;
            va_start(args, fmt);
            Str::PrintFmtArgs(text, fmtLen, fmt, args);
            va_end(args);

            _DispatchLogEntry({
                .type = LogLevel::Debug,
                .channels = channels,
                .textLen = Str::Len(text),
                .sourceFileLen = sourceFile ? Str::Len(sourceFile) : 0,
                .line = line,
                .text = text,
                .sourceFile = sourceFile
            });
        #else
            UNUSED(channels);
            UNUSED(sourceFile);
            UNUSED(line);
            UNUSED(fmt);
        #endif
    }

    void _private::PrintVerbose(uint32 channels, const char* sourceFile, uint32 line, const char* fmt, ...)
    {
        if (gLog.logLevel < LogLevel::Verbose)
            return;

        MemTempAllocator tmp;
        uint32 fmtLen = Str::Len(fmt) + LOG_MAX_MESSAGE_CHARS;
        char* text = tmp.MallocTyped<char>(fmtLen);

        va_list args;
        va_start(args, fmt);
        Str::PrintFmtArgs(text, fmtLen, fmt, args);
        va_end(args);

        _DispatchLogEntry({
            .type = LogLevel::Verbose,
            .channels = channels,
            .textLen = Str::Len(text),
            .sourceFileLen = sourceFile ? Str::Len(sourceFile) : 0,
            .line = line,
            .text = text,
            .sourceFile = sourceFile
        });
    }

    void _private::PrintWarning(uint32 channels, const char* sourceFile, uint32 line, const char* fmt, ...)
    {
        if (gLog.logLevel < LogLevel::Warning)
            return;

        MemTempAllocator tmp;
        uint32 fmtLen = Str::Len(fmt) + LOG_MAX_MESSAGE_CHARS;
        char* text = tmp.MallocTyped<char>(fmtLen);

        va_list args;
        va_start(args, fmt);
        Str::PrintFmtArgs(text, fmtLen, fmt, args);
        va_end(args);

        _DispatchLogEntry({
            .type = !gLog.treatWarningsAsErrors ? LogLevel::Warning : LogLevel::Error,
            .channels = channels,
            .textLen = Str::Len(text),
            .sourceFileLen = sourceFile ? Str::Len(sourceFile) : 0,
            .line = line,
            .text = text,
            .sourceFile = sourceFile
        });
    }

    void _private::PrintError(uint32 channels, const char* sourceFile, uint32 line, const char* fmt, ...)
    {
        if (gLog.logLevel < LogLevel::Error)
            return;

        MemTempAllocator tmp;
        uint32 fmtLen = Str::Len(fmt) + LOG_MAX_MESSAGE_CHARS;
        char* text = tmp.MallocTyped<char>(fmtLen);

        va_list args;
        va_start(args, fmt);
        Str::PrintFmtArgs(text, fmtLen, fmt, args);
        va_end(args);

        _DispatchLogEntry({
            .type = LogLevel::Error,
            .channels = channels,
            .textLen = Str::Len(text),
            .sourceFileLen = sourceFile ? Str::Len(sourceFile) : 0,
            .line = line,
            .text = text,
            .sourceFile = sourceFile        
        });
    }

    void RegisterCallback(LogCallback callback, void* userData)
    {
        ASSERT(callback);
        ASSERT_MSG(gLog.callbacks.FindIf([callback](const Pair<LogCallback, void*>& p) { return p.first == callback; }) == UINT32_MAX, "Callback already added");
        gLog.callbacks.Push(Pair<LogCallback, void*>(callback, userData));
    }

    void UnregisterCallback(LogCallback callback)
    {
        uint32 index = gLog.callbacks.FindIf([callback](const Pair<LogCallback, void*>& p) { return p.first == callback; });
        if (index != UINT32_MAX)
            gLog.callbacks.RemoveAndSwap(index);
    }
} // Log
