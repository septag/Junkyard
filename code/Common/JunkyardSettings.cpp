#include "JunkyardSettings.h"

#include "../Core/Settings.h"
#include "../Core/Log.h"
#include "../Core/Arrays.h"

static_assert(uint32(LogLevel::Error) == uint32(SettingsEngine::LogLevel::Error));
static_assert(uint32(LogLevel::Warning) == uint32(SettingsEngine::LogLevel::Warning));
static_assert(uint32(LogLevel::Verbose) == uint32(SettingsEngine::LogLevel::Verbose));
static_assert(uint32(LogLevel::Debug) == uint32(SettingsEngine::LogLevel::Debug));
static_assert(uint32(LogLevel::Info) == uint32(SettingsEngine::LogLevel::Info));

enum class SettingsCategory : uint32
{
    App = 0,
    Engine,
    Graphics,
    Tooling,
    Debug,
    _Count
};

static constexpr const char* kSettingCategoryNames[uint32(SettingsCategory::_Count)] = {
    "App",
    "Engine",
    "Graphics",
    "Tooling",
    "Debug"
};

static_assert(CountOf(kSettingCategoryNames) == uint32(SettingsCategory::_Count));

struct SettingsJunkyardParser final : SettingsCustomCallbacks
{
    uint32 GetCategoryCount() const override;
    const char* GetCategory(uint32 id) const override;
    bool ParseSetting(uint32 categoryId, const char* key, const char* value) override;
    void SaveCategory(uint32, Array<SettingsKeyValue>&) override {}
};

struct SettingsJunkyardContext
{
    SettingsJunkyardParser parser;
    SettingsJunkyard settings;
    bool initialized;
};

static SettingsJunkyardContext gSettingsJunkyard;

static SettingsEngine::LogLevel settingsParseEngineLogLevel(const char* strLogLevel)
{
    if (Str::IsEqualNoCase(strLogLevel, "Error"))           return SettingsEngine::LogLevel::Error;
    else if (Str::IsEqualNoCase(strLogLevel, "Warning"))    return SettingsEngine::LogLevel::Warning;
    else if (Str::IsEqualNoCase(strLogLevel, "Info"))       return SettingsEngine::LogLevel::Info;
    else if (Str::IsEqualNoCase(strLogLevel, "Verbose"))    return SettingsEngine::LogLevel::Verbose;
    else if (Str::IsEqualNoCase(strLogLevel, "Debug"))      return SettingsEngine::LogLevel::Debug;
    else                                                  return SettingsEngine::LogLevel::Default;
}

bool SettingsJunkyardParser::ParseSetting(uint32 categoryId, const char* key, const char* value)
{
    SettingsCategory category = static_cast<SettingsCategory>(categoryId);
    ASSERT(category != SettingsCategory::_Count);

    if (category == SettingsCategory::App) {
        SettingsApp* app = &gSettingsJunkyard.settings.app;
        if (Str::IsEqualNoCase(key, "launchMinimized")) {
            app->launchMinimized = Str::ToBool(value);
            return true;
        }
    }
    else if (category == SettingsCategory::Engine) {
        SettingsEngine* engine = &gSettingsJunkyard.settings.engine;
        if (Str::IsEqualNoCase(key, "connectToServer")) {
            engine->connectToServer = Str::ToBool(value);
            return true;
        }
        else if (Str::IsEqualNoCase(key, "remoteServicesUrl")) {
            engine->remoteServicesUrl = value;
            return true;
        }
        else if (Str::IsEqualNoCase(key, "logLevel")) {
            engine->logLevel = settingsParseEngineLogLevel(value);
            return true;
        }
        else if (Str::IsEqualNoCase(key, "jobsNumShortTaskThreads")) {
            engine->jobsNumShortTaskThreads = Str::ToUint(value);
            return true;
        }
        else if (Str::IsEqualNoCase(key, "jobsNumLongTaskThreads")) {
            engine->jobsNumLongTaskThreads = Str::ToUint(value);
            return true;
        }
        else if (Str::IsEqualNoCase(key, "debugAllocations")) {
            engine->debugAllocations = Str::ToBool(value);
            return true;
        }
        else if (Str::IsEqualNoCase(key, "breakOnErrors")) {
            engine->breakOnErrors = Str::ToBool(value);
            return true;
        }
        else if (Str::IsEqualNoCase(key, "treatWarningsAsErrors")) {
            engine->treatWarningsAsErrors = Str::ToBool(value);
            return true;
        }
        else if (Str::IsEqualNoCase(key, "enableMemPro")) {
            engine->enableMemPro = Str::ToBool(value);
            return true;            
        }
        else if (Str::IsEqualNoCase(key, "useCacheOnly")) {
            engine->useCacheOnly = Str::ToBool(value);
            return true;
        }
    }
    else if (category == SettingsCategory::Graphics) {
        SettingsGraphics* graphics = &gSettingsJunkyard.settings.graphics;
        if (Str::IsEqualNoCase(key, "enable")) {
            graphics->enable = Str::ToBool(value);
            return true;
        }
        else if (Str::IsEqualNoCase(key, "validate")) {
            graphics->validate = Str::ToBool(value);
            return true;
        }
        else if (Str::IsEqualNoCase(key, "headless")) {
            graphics->headless = Str::ToBool(value);
            return true;
        }
        else if (Str::IsEqualNoCase(key, "SurfaceSRGB")) {
            graphics->surfaceSRGB = Str::ToBool(value);
            return true;
        }
        else if (Str::IsEqualNoCase(key, "listExtensions")) {
            graphics->listExtensions = Str::ToBool(value);
            return true;
        }
        else if (Str::IsEqualNoCase(key, "validateBestPractices")) {
            graphics->validateBestPractices = Str::ToBool(value);
            return true;
        }
        else if (Str::IsEqualNoCase(key, "validateSynchronization")) {
            graphics->validateSynchronization = Str::ToBool(value);
            return true;
        }
        else if (Str::IsEqualNoCase(key, "shaderDumpIntermediates")) {
            graphics->shaderDumpIntermediates = Str::ToBool(value);
            return true;
        }
        else if (Str::IsEqualNoCase(key, "shaderDumpProperties")) {
            graphics->shaderDumpProperties = Str::ToBool(value);
            return true;
        }
        else if (Str::IsEqualNoCase(key, "shaderDebug")) {
            graphics->shaderDebug = Str::ToBool(value);
            return true;
        }
        else if (Str::IsEqualNoCase(key, "enableGpuProfile")) {
            graphics->enableGpuProfile = Str::ToBool(value);
            return true;
        }
        else if (Str::IsEqualNoCase(key, "enableImGui")) {
            graphics->enableImGui = Str::ToBool(value);
            return true;
        }
        else if (Str::IsEqualNoCase(key, "enableVsync")) {
            graphics->enableVsync = Str::ToBool(value);
            return true;
        }
        else if (Str::IsEqualNoCase(key, "trackResourceLeaks")) {
            graphics->trackResourceLeaks = Str::ToBool(value);
            return true;
        }
    }
    else if (category == SettingsCategory::Tooling) {
        SettingsTooling* tooling = &gSettingsJunkyard.settings.tooling;
        if (Str::IsEqualNoCase(key, "enableServer")) {
            tooling->enableServer = Str::ToBool(value);
            return true;
        }
        else if (Str::IsEqualNoCase(key, "serverPort")) {
            tooling->serverPort = static_cast<uint16>(Str::ToInt(value));
            return true;
        }
    }
    else if (category == SettingsCategory::Debug) {
        SettingsDebug* debug = &gSettingsJunkyard.settings.debug;
        if (Str::IsEqualNoCase(key, "captureStacktraceForFiberProtector")) {
            debug->captureStacktraceForFiberProtector = true;
            return true;
        }
        else if (Str::IsEqualNoCase(key, "captureStacktraceForTempAllocator")) {
            debug->captureStacktraceForTempAllocator = true;
            return true;
        }
    }

    return false;
}

uint32 SettingsJunkyardParser::GetCategoryCount() const
{
    return uint32(SettingsCategory::_Count);
}

const char* SettingsJunkyardParser::GetCategory(uint32 id) const
{
    ASSERT(id < uint32(SettingsCategory::_Count));
    return kSettingCategoryNames[id];
}

const SettingsJunkyard& SettingsJunkyard::Get()
{
    return gSettingsJunkyard.settings;  
}

void SettingsJunkyard::Initialize(const SettingsJunkyard& initSettings)
{
    gSettingsJunkyard.initialized = true;
    gSettingsJunkyard.settings = initSettings;
    Settings::AddCustomCallbacks(&gSettingsJunkyard.parser);
}

bool SettingsJunkyard::IsInitialized()
{
    return gSettingsJunkyard.initialized;
}
