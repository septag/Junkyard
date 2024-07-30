#pragma once
//
// Settings can get loaded from several sources
// There are two families of settings, "Predefined" and "User". 
// "Predefined" are the ones that are already hard coded. see the structs in `SettingsAll` 
//
// Initialization: First thing you should do before initializing the application is initializing the settings and provide your own hard-defined defaults
//                 You can also pass the current defaults which are the values you see right next to each setting field below
//
// CommandLine: each predefined setting key/value must start with a dash and it's parent subsystem name after. The rest of the setting name would be exactly like it is in the struct
//              For instance: 
//                  - to enable `SettingsGraphics.validate=true`, you should add `-GraphicsValidate=1` to the command-line arguments
//                  - to connect to server (`SettingsEngine.connectToServer=true`), you should add `-EngineConnectToServer=1` to the args
//              Note that all arguments are case-insensitive
//              Command arguments that are part of predefined ones, will go to user-defined ones and can be fetched with `settingsGetValue` function instead.
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
#include "StringUtil.h"

template <typename _T> struct Array;
typedef struct AAssetManager AAssetManager;

struct SettingsKeyValue
{
    String32 key;
    String<CONFIG_MAX_PATH> value;
};

struct NO_VTABLE SettingsCustomCallbacks
{
    // 'key' in FindCategoryId can be either a full key name or just the section name inside INI file
    //  in either case, the check should be performed with 
    virtual uint32 GetCategoryCount() const = 0;
    virtual const char* GetCategory(uint32 id) const = 0;
    virtual bool ParseSetting(uint32 categoryId, const char* key, const char* value) = 0;

    // Fill 'items' for each corrosponding categoryId
    // Can ignore implementing this if you don't want to save
    virtual void SaveCategory(uint32 categoryId, Array<SettingsKeyValue>& items) = 0;   
};

namespace Settings
{
    API void AddCustomCallbacks(SettingsCustomCallbacks* callbacks);
    API void RemoveCustomCallbacks(SettingsCustomCallbacks* callbacks);

    API bool InitializeFromINI(const char* iniFilepath);
    API bool InitializeFromCommandLine(int argc, char* argv[]);

    #if PLATFORM_ANDROID
    API bool InitializeFromAndroidAsset(AAssetManager* assetMgr, const char* iniFilepath);
    #endif

    API void SaveToINI(const char* iniFilepath);
    API void Release();

    // Custom key/values
    API void SetValue(const char* key, const char* value);
    API const char* GetValue(const char* key, const char* defaultValue = "");
}

