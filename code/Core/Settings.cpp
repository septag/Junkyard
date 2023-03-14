#include "Settings.h"
#include "Memory.h"
#include "String.h"
#include "Log.h"
#include "Buffers.h"
#include "FileIO.h"

#define INI_IMPLEMENTATION
#define INI_MALLOC(ctx, size)       memAlloc(size, (Allocator*)ctx)
#define INI_FREE(ctx, ptr)          memFree(ptr, (Allocator*)ctx)
#define INI_MEMCPY(dst, src, cnt)   memcpy(dst, src, cnt)
#define INI_STRLEN(s)               strLen(s)
#define INI_STRNICMP(s1, s2, cnt)   (strIsEqualNoCaseCount(s1, s2, cnt) ? 0 : 1)

PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wsign-compare")
#include "../External/mgustavsson/ini.h"
PRAGMA_DIAGNOSTIC_POP()

#define SOKOL_ARGS_IMPL
#define SOKOL_ASSERT(c)     ASSERT(c)
#define SOKOL_LOG(msg)      logDebug(msg)
#define SOKOL_CALLOC(n,s)   memAllocZero((n)*(s))
#define SOKOL_FREE(p)       memFree(p)
#define SOKOL_ARGS_API_DECL 
#define SOKOL_API_IMPL      
PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4505)
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wunused-function")
#include "../External/sokol/sokol_args.h"
PRAGMA_DIAGNOSTIC_POP()

// TODO: gotta remove the dependency to application
#if PLATFORM_ANDROID
#include "../Application.h"    // appGetNativeAssetManagerHandle
#include <android/asset_manager.h>
#endif

struct SettingsKeyValue
{
    String32 key;
    String32 value;
};

struct SettingsContext
{
    SettingsAll predefined;
    Array<SettingsKeyValue> keyValuePairs;
    bool initialized;
};

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

static SettingsContext gSettings;

static SettingsEngine::LogLevel settingsParseEngineLogLevel(const char* strLogLevel)
{
    if (strIsEqualNoCase(strLogLevel, "Error"))           return SettingsEngine::LogLevel::Error;
    else if (strIsEqualNoCase(strLogLevel, "Warning"))    return SettingsEngine::LogLevel::Warning;
    else if (strIsEqualNoCase(strLogLevel, "Info"))       return SettingsEngine::LogLevel::Info;
    else if (strIsEqualNoCase(strLogLevel, "Verbose"))    return SettingsEngine::LogLevel::Verbose;
    else if (strIsEqualNoCase(strLogLevel, "Debug"))      return SettingsEngine::LogLevel::Debug;
    else                                                  return SettingsEngine::LogLevel::Default;
}

static bool settingsParsePredefinedSetting(SettingsCategory category, const char* key, const char* value)
{
    ASSERT(category != SettingsCategory::_Count);

    if (category == SettingsCategory::App) {
        SettingsApp* app = &gSettings.predefined.app;
        if (strIsEqualNoCase(key, "launchMinimized")) {
            app->launchMinimized = strToBool(value);
            return true;
        }
    }
    else if (category == SettingsCategory::Engine) {
        SettingsEngine* engine = &gSettings.predefined.engine;
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
        else if (strIsEqualNoCase(key, "jobsThreadCount")) {
            engine->jobsThreadCount = strToBool(value);
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
    }
    else if (category == SettingsCategory::Graphics) {
        SettingsGraphics* graphics = &gSettings.predefined.graphics;
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
    }
    else if (category == SettingsCategory::Tooling) {
        SettingsTooling* tooling = &gSettings.predefined.tooling;
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
        SettingsDebug* debug = &gSettings.predefined.debug;
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

bool settingsLoadFromINI(const char* iniFilepath)
{
    ASSERT(gSettings.initialized);
    logDebug("Loading settings from: %s", iniFilepath);

    File f;
    Blob blob;
#if PLATFORM_ANDROID
    AAsset* asset = AAssetManager_open(appAndroidGetAssetManager(), iniFilepath, AASSET_MODE_BUFFER);
    if (asset && AAsset_getLength(asset) > 0) {
        off_t size = AAsset_getLength(asset);
        blob.Reserve(size + 1);
        int bytesRead = AAsset_read(asset, const_cast<void*>(blob.Data()), size);
        
        if (bytesRead > 0) {
            blob.SetSize(bytesRead);
            blob.Write<char>('\0');
        }

        AAsset_close(asset);
    }
#else
    if (f.Open(iniFilepath, FileIOFlags::Read | FileIOFlags::SeqScan)) {
        uint64 size = f.GetSize();
        if (size) {
            blob.Reserve(size + 1);
            size_t bytesRead = f.Read(const_cast<void*>(blob.Data()), size);
            blob.SetSize(bytesRead);
            blob.Write<char>('\0');
        }
        f.Close();
    }
#endif

    if (!blob.IsValid()) {
        logError("Opening ini file '%s' failed", iniFilepath);
        return false;
    }

    ini_t* ini = ini_load(reinterpret_cast<const char*>(blob.Data()), memDefaultAlloc());
    if (!ini) {
        blob.Free();
        logError("Parsing INI file '%s' failed", iniFilepath);
        return false;
    }

    char keyTrimmed[64];
    char valueTrimmed[256];
    uint32 count = 0;

    for (int i = 0; i < ini_section_count(ini); i++) {
        const char* sectionName = ini_section_name(ini, i);
        SettingsCategory cat = SettingsCategory::_Count;
        for (uint32 c = 0; c < static_cast<uint32>(SettingsCategory::_Count); c++) {
            if (strIsEqualNoCase(sectionName, kSettingCategoryNames[c])) {
                cat = static_cast<SettingsCategory>(c);
                break;
            }
        }

        for (int j = 0; j < ini_property_count(ini, i); j++) {
            const char* key = ini_property_name(ini, i, j);
            const char* value = ini_property_value(ini, i, j);
            strTrim(keyTrimmed, sizeof(keyTrimmed), key);
            strTrim(valueTrimmed, sizeof(valueTrimmed), value);
            bool predefined = cat != SettingsCategory::_Count ? 
                settingsParsePredefinedSetting(cat, keyTrimmed, valueTrimmed) : false;
            
            if (!predefined)
                settingsSetValue(keyTrimmed, valueTrimmed);

            logDebug("\t%u) %s%s = %s", ++count, keyTrimmed, !predefined ? "(*)" : "", valueTrimmed);
        }
    }
    ini_destroy(ini);
    blob.Free();
    return true;
}

bool settingsLoadFromCommandLine(int argc, char* argv[])
{
    ASSERT(gSettings.initialized);

    sargs_state* args = sargs_create(sargs_desc {
        .argc = argc,
        .argv = argv
    });

    if (sargs_num_args(args) > 0) 
        logDebug("Loading settings from CommandLine:");
    for (int i = 0; i < sargs_num_args(args); i++) {
        const char* key = sargs_key_at(args, i);
        const char* value = sargs_value_at(args, i);

        // skip keys with no dash '-'
        if (key[0] != '-')
            continue;
        ++key;

        // check predefined settings. For that, first check the first word for predefined categories
        bool predefined = false;
        for (uint32 c = 0; c < static_cast<uint32>(SettingsCategory::_Count); c++) {
            const char* catName = kSettingCategoryNames[c];
            uint32 catNameLen = strLen(catName);
            if (strIsEqualNoCaseCount(key, catName, catNameLen)) 
                predefined = settingsParsePredefinedSetting(static_cast<SettingsCategory>(c), key + catNameLen, value);
        }
        
        // if doesn't exist in the predefined settings, add to the custom settings
        if (!predefined)
            settingsSetValue(key, value);

        logDebug("\t%d) %s%s = %s", i+1, key, !predefined ? "(*)" : "", value);
    }

    sargs_destroy(args);

    return true;
}

void settingsSetValue(const char* key, const char* value)
{
    uint32 index = gSettings.keyValuePairs.FindIf([key](const SettingsKeyValue& keyval) {
        return keyval.key.IsEqual(key);
    });

    if (index != UINT32_MAX)
        gSettings.keyValuePairs[index].value = value;
    else
        gSettings.keyValuePairs.Push(SettingsKeyValue {.key = key, .value = value});
}

const char* settingsGetValue(const char* key, const char* defaultValue)
{
    uint32 index = gSettings.keyValuePairs.FindIf([key](const SettingsKeyValue& keyval) {
        return keyval.key.IsEqual(key);
    });
    
    return index != UINT32_MAX ? gSettings.keyValuePairs[index].value.CStr() : defaultValue;
}

bool settingsInitialize(const SettingsAll& conf)
{
    Allocator* alloc = memDefaultAlloc();

    gSettings.predefined = conf;
    gSettings.keyValuePairs.SetAllocator(alloc);
    
    gSettings.initialized = true;
    return false;
}

void settingsRelease()
{
    gSettings.keyValuePairs.Free();
    gSettings.initialized = false;
}

const SettingsAll& settingsGet()
{
    return gSettings.predefined;  
}

const SettingsApp& settingsGetApp()
{
    return gSettings.predefined.app;
}

const SettingsGraphics& settingsGetGraphics()
{
    return gSettings.predefined.graphics;
}

const SettingsTooling& settingsGetTooling()
{
    return gSettings.predefined.tooling;
}

const SettingsEngine& settingsGetEngine()
{
    return gSettings.predefined.engine;
}

const SettingsDebug& settingsGetDebug()
{
    return gSettings.predefined.debug;
}

bool settingsIsInitialized()
{
    return gSettings.initialized;
}
