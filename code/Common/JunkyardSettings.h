#pragma once

#include "../Core/Base.h"
#include "../Core/StringUtil.h"

#ifndef DEFAULT_LOG_LEVEL
    #if !CONFIG_FINAL_BUILD
        #define DEFAULT_LOG_LEVEL LogLevel::Debug;       // Log filter. LogLevel below this value will not be shown
    #else
        #define DEFAULT_LOG_LEVEL LogLevel::Info;        // Log filter. LogLevel below this value will not be shown
    #endif
#endif

#ifndef DEFAULT_CACHE_USAGE 
    #if CONFIG_FINAL_BUILD
        #define DEFAULT_CACHE_USAGE true
    #else
        #define DEFAULT_CACHE_USAGE false
    #endif
#endif

struct SettingsGraphics
{
    bool enable = true;             // Enable graphics subsystem. (cmdline="enableGraphics")
    bool validate = false;          // Enable validation layers. (cmdline="validateGraphics")
    bool headless = false;          // Device is created, but with no views/swapchain/gfx-queue. only used for comput. (cmdline="headlessGraphics")
    bool surfaceSRGB = false;       // SRGB surface for Swap-chain
    bool listExtensions = false;    // Show device extensions upon initialization
    bool validateBestPractices = false;   // see VK_EXT_validation_features
    bool validateSynchronization = false;   // see VK_EXT_validation_features
    bool shaderDumpIntermediates = false;   // Dumps all shader intermediates (glsl/spv/asm) in the current working dir
    bool shaderDumpProperties = false;      // Dumps all internal shader properties, if device supports VK_KHR_pipeline_executable_properties
    bool shaderDebug = false;               // Adds debugging information to all shaders
    bool enableGpuProfile = false;          // Enables GPU Profiling with Tracy and other tools
    bool enableImGui = true;                // Enables ImGui GUI
    bool enableVsync = true;                // Enables Vsync. Some hardware doesn't support this feature
    bool trackResourceLeaks = false;        // Store buffers/image/etc. resource stacktraces and shows leakage information at exit
};

struct SettingsTooling
{
    bool enableServer = false;          // Starts server service (ShaderCompiler/Baking/etc.)
    uint16 serverPort = 6006;           // Local server port number       
};

struct SettingsApp
{
    bool launchMinimized = false;       // Launch application minimized (Desktop builds only)
};

struct SettingsEngine
{
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

    bool connectToServer = false;               // Connects to server
    String<256> remoteServicesUrl = "127.0.0.1:6006";   // Url to server. Divide port number with colon
    LogLevel logLevel = DEFAULT_LOG_LEVEL;
    uint32 jobsNumShortTaskThreads = 0;         // Number of threads to spawn for short task jobs
    uint32 jobsNumLongTaskThreads = 0;          // Number of threads to spawn for long task jobs
    bool debugAllocations = false;              // Use heap allocator instead for major allocators, like temp/budget/etc.
    bool breakOnErrors = false;                 // Break when LOG_ERROR happens
    bool treatWarningsAsErrors = false;         // Break when LOG_WARNING happens
    bool enableMemPro = false;                  // Enables MemPro instrumentation (https://www.puredevsoftware.com/mempro/index.htm)
    bool useCacheOnly = DEFAULT_CACHE_USAGE;    // This option only uses cache to load assets and bypasses Remote or Local disk assets
};

struct SettingsDebug
{
    bool captureStacktraceForFiberProtector = false;    // Capture stacktraces for Fiber protector (see Debug.cpp)
    bool captureStacktraceForTempAllocator = false;     // Capture stacktraces for Temp allocators (see Memory.cpp)
};

struct SettingsJunkyard
{
    SettingsApp app;
    SettingsEngine engine;
    SettingsGraphics graphics;
    SettingsTooling tooling;
    SettingsDebug debug;

    API static bool IsInitialized();
    API static void Initialize(const SettingsJunkyard& initSettings);
    API static const SettingsJunkyard& Get();
};

