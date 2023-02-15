#pragma once

//------------------------------------------------------------------------
// Settings can get loaded from several sources
// There are two families of settings, "Predefined" and "User". 
// Predefined are the ones that are already hard coded. see the structs in `SettingsAll` 
//
// Initialization: First thing you should do before initializing the application is initializing the settings and provide your own hard-defined defaults
//                 You can also pass the current defaults which are the values you see right next to each setting field below
//
// CommandLine: each predefined setting key/value must start with a dash and it's parent subsystem name after. The rest of the setting name would be exactly like it is in the struct
//              For instance, to enable `SettingsGraphics.validate=true`, you should add `-GraphicsEnable=1` to the command-line arguments
//                            to connect to server (`SettingsEngine.connectToServer=true`), you should add `-EngineConnectToServer=1` to the args
//              Note that all arguments are case-insensitive
//              command arguments that are not detected as part of predefined ones, will go to user-defined ones and can be fetched with `settingsGetValue` function
//
// INI file: Settings can be loaded from an INI file. For predefined settings, you must add the exact field you see in the structs below under it's parent category
//           So for instance, fields in `SettingsGraphics` will go under [graphics] section and `SettingsEngine` will go under [engine]:
//               myUserDefinedSettings=1
//               [engine] 
//               enableServer=1
//               [graphics]
//               listExtensions=1
//
//           Note that all arguments are case-insensitive
//
#include "Base.h"
#include "String.h"

struct SettingsGraphics
{
    bool enable = true;             // Enable graphics subsystem. (cmdline="enableGraphics")
    bool validate = false;          // Enable validation layers. (cmdline="validateGraphics")
    bool headless = false;          // Device is created, but with no views/swapchain/gfx-queue. only used for comput. (cmdline="headlessGraphics")
    bool surfaceSRGB = false;       // SRGB surface for Swap-chain
    bool listExtensions = false;    // Show device extensions upon initialization
    bool enableAdrenoDebug = false; // Tries to enable VK_LAYER_ADRENO_debug layer if available, validate should be enabled
    bool validateBestPractices = false;   // see VK_EXT_validation_features
    bool validateSynchronization = false;   // see VK_EXT_validation_features
    bool shaderDumpIntermediates = false;   // Dumps all shader intermediates (glsl/spv/asm) in the current working dir
    bool shaderDumpProperties = false;      // Dumps all internal shader properties, if device supports VK_KHR_pipeline_executable_properties
    bool shaderDebug = false;               // Adds debugging information to all shaders
    bool enableGpuProfile = false;          // Enables GPU Profiling with Tracy and other tools
    bool enableImGui = true;                // Enables ImGui GUI
    bool enableVsync = true;                // Enables Vsync. Some hardware doesn't support this feature
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
    LogLevel logLevel = LogLevel::Info;         // Log filter. LogLevel below this value will not be shown
    uint32 jobsThreadCount = 0;                 // Number of threads to spawn for each job type (Long/Short)
    bool debugAllocations = false;              // Use heap allocator instead for major allocators, like temp/budget/etc.
    bool breakOnErrors = false;                 // Break when logError happens
    bool treatWarningsAsErrors = false;         // Break when logWarning happens
    bool enableMemPro = false;                  // Enables MemPro instrumentation (https://www.puredevsoftware.com/mempro/index.htm)
};

struct SettingsDebug
{
    bool captureStacktraceForFiberProtector = false;    // Capture stacktraces for Fiber protector (see Debug.cpp)
    bool captureStacktraceForTempAllocator = false;     // Capture stacktraces for Temp allocators (see Memory.cpp)
};

struct SettingsAll
{
    SettingsApp app;
    SettingsEngine engine;
    SettingsGraphics graphics;
    SettingsTooling tooling;
    SettingsDebug debug;
};

API bool settingsInitialize(const SettingsAll& conf);
API void settingsRelease();
API bool settingsIsInitialized();

API bool settingsLoadFromINI(const char* iniFilepath);
API bool settingsLoadFromCommandLine(int argc, char* argv[]);

API const SettingsAll& settingsGet();
API const SettingsApp& settingsGetApp();
API const SettingsGraphics& settingsGetGraphics();
API const SettingsTooling& settingsGetTooling();
API const SettingsEngine& settingsGetEngine();
API const SettingsDebug& settingsGetDebug();

// Custom key/values
API void settingsSetValue(const char* key, const char* value);
API const char* settingsGetValue(const char* key, const char* defaultValue = "");
