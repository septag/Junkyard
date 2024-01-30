#include "JunkyardSettings.h"

#include "../Core/Settings.h"
#include "../Core/Log.h"

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
    if (strIsEqualNoCase(strLogLevel, "Error"))           return SettingsEngine::LogLevel::Error;
    else if (strIsEqualNoCase(strLogLevel, "Warning"))    return SettingsEngine::LogLevel::Warning;
    else if (strIsEqualNoCase(strLogLevel, "Info"))       return SettingsEngine::LogLevel::Info;
    else if (strIsEqualNoCase(strLogLevel, "Verbose"))    return SettingsEngine::LogLevel::Verbose;
    else if (strIsEqualNoCase(strLogLevel, "Debug"))      return SettingsEngine::LogLevel::Debug;
    else                                                  return SettingsEngine::LogLevel::Default;
}

bool SettingsJunkyardParser::ParseSetting(uint32 categoryId, const char* key, const char* value)
{
    SettingsCategory category = static_cast<SettingsCategory>(categoryId);
    ASSERT(category != SettingsCategory::_Count);

    if (category == SettingsCategory::App) {
        SettingsApp* app = &gSettingsJunkyard.settings.app;
        if (strIsEqualNoCase(key, "launchMinimized")) {
            app->launchMinimized = strToBool(value);
            return true;
        }
    }
    else if (category == SettingsCategory::Engine) {
        SettingsEngine* engine = &gSettingsJunkyard.settings.engine;
        if (strIsEqualNoCase(key, "connectToServer")) {
            engine->connectToServer = strToBool(value);
            return true;
        }
        else if (strIsEqualNoCase(key, "remoteServicesUrl")) {
            engine->remoteServicesUrl = value;
            return true;
        }
        else if (strIsEqualNoCase(key, "logLevel")) {
            engine->logLevel = settingsParseEngineLogLevel(value);
            return true;
        }
        else if (strIsEqualNoCase(key, "jobsNumShortTaskThreads")) {
            engine->jobsNumShortTaskThreads = strToUint(value);
            return true;
        }
        else if (strIsEqualNoCase(key, "jobsNumLongTaskThreads")) {
            engine->jobsNumLongTaskThreads = strToUint(value);
            return true;
        }
        else if (strIsEqualNoCase(key, "debugAllocations")) {
            engine->debugAllocations = strToBool(value);
            return true;
        }
        else if (strIsEqualNoCase(key, "breakOnErrors")) {
            engine->breakOnErrors = strToBool(value);
            return true;
        }
        else if (strIsEqualNoCase(key, "treatWarningsAsErrors")) {
            engine->treatWarningsAsErrors = strToBool(value);
            return true;
        }
        else if (strIsEqualNoCase(key, "enableMemPro")) {
            engine->enableMemPro = strToBool(value);
            return true;            
        }
        else if (strIsEqualNoCase(key, "useCacheOnly")) {
            engine->useCacheOnly = strToBool(value);
            return true;
        }
    }
    else if (category == SettingsCategory::Graphics) {
        SettingsGraphics* graphics = &gSettingsJunkyard.settings.graphics;
        if (strIsEqualNoCase(key, "enable")) {
            graphics->enable = strToBool(value);
            return true;
        }
        else if (strIsEqualNoCase(key, "validate")) {
            graphics->validate = strToBool(value);
            return true;
        }
        else if (strIsEqualNoCase(key, "headless")) {
            graphics->headless = strToBool(value);
            return true;
        }
        else if (strIsEqualNoCase(key, "SurfaceSRGB")) {
            graphics->surfaceSRGB = strToBool(value);
            return true;
        }
        else if (strIsEqualNoCase(key, "listExtensions")) {
            graphics->listExtensions = strToBool(value);
            return true;
        }
        else if (strIsEqualNoCase(key, "enableAdrenoDebug")) {
            graphics->enableAdrenoDebug = strToBool(value);
            return true;
        }
        else if (strIsEqualNoCase(key, "validateBestPractices")) {
            graphics->validateBestPractices = strToBool(value);
            return true;
        }
        else if (strIsEqualNoCase(key, "validateSynchronization")) {
            graphics->validateSynchronization = strToBool(value);
            return true;
        }
        else if (strIsEqualNoCase(key, "shaderDumpIntermediates")) {
            graphics->shaderDumpIntermediates = strToBool(value);
            return true;
        }
        else if (strIsEqualNoCase(key, "shaderDumpProperties")) {
            graphics->shaderDumpProperties = strToBool(value);
            return true;
        }
        else if (strIsEqualNoCase(key, "shaderDebug")) {
            graphics->shaderDebug = strToBool(value);
            return true;
        }
        else if (strIsEqualNoCase(key, "enableGpuProfile")) {
            graphics->enableGpuProfile = strToBool(value);
            return true;
        }
        else if (strIsEqualNoCase(key, "enableImGui")) {
            graphics->enableImGui = strToBool(value);
            return true;
        }
        else if (strIsEqualNoCase(key, "enableVsync")) {
            graphics->enableVsync = strToBool(value);
            return true;
        }
        else if (strIsEqualNoCase(key, "trackResourceLeaks")) {
            graphics->trackResourceLeaks = strToBool(value);
            return true;
        }
    }
    else if (category == SettingsCategory::Tooling) {
        SettingsTooling* tooling = &gSettingsJunkyard.settings.tooling;
        if (strIsEqualNoCase(key, "enableServer")) {
            tooling->enableServer = strToBool(value);
            return true;
        }
        else if (strIsEqualNoCase(key, "serverPort")) {
            tooling->serverPort = static_cast<uint16>(strToInt(value));
            return true;
        }
    }
    else if (category == SettingsCategory::Debug) {
        SettingsDebug* debug = &gSettingsJunkyard.settings.debug;
        if (strIsEqualNoCase(key, "captureStacktraceForFiberProtector")) {
            debug->captureStacktraceForFiberProtector = true;
            return true;
        }
        else if (strIsEqualNoCase(key, "captureStacktraceForTempAllocator")) {
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

const SettingsJunkyard& settingsGet()
{
    return gSettingsJunkyard.settings;  
}

void settingsInitializeJunkyard(const SettingsJunkyard& initSettings)
{
    gSettingsJunkyard.initialized = true;
    gSettingsJunkyard.settings = initSettings;
    settingsAddCustomCallbacks(&gSettingsJunkyard.parser);
}

bool settingsIsInitializedJunkyard()
{
    return gSettingsJunkyard.initialized;
}
