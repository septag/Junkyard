#include "Settings.h"
#include "System.h"
#include "Debug.h"
#include "Arrays.h"
#include "Blobs.h"
#include "Allocators.h"

#include "External/mgustavsson/ini.h"

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
#include "External/sokol/sokol_args.h"
PRAGMA_DIAGNOSTIC_POP()

#if PLATFORM_ANDROID
#include <android/asset_manager.h>
#endif

#define SETTINGS_NONE_PREDEFINED "_UNKNOWN_"

struct SettingsContext
{
    Array<SettingsKeyValue> keyValuePairs;  // Container to save none-predefined settings
    StaticArray<SettingsCustomCallbacks*, 8> customCallbacks;
};

static SettingsContext gSettings;

void settingsAddCustomCallbacks(SettingsCustomCallbacks* callbacks)
{
    ASSERT(callbacks);

    uint32 index = gSettings.customCallbacks.Find(callbacks);
    if (index == UINT32_MAX) 
        gSettings.customCallbacks.Add(callbacks);
}

void settingsRemoveCustomCallbacks(SettingsCustomCallbacks* callbacks)
{
    ASSERT(callbacks);

    uint32 index = gSettings.customCallbacks.Find(callbacks);
    if (index != UINT32_MAX) 
        gSettings.customCallbacks.RemoveAndSwap(index);
}

static bool settingsLoadFromINIInternal(const Blob& blob)
{
    ASSERT(blob.IsValid());

    ini_t* ini = ini_load(reinterpret_cast<const char*>(blob.Data()), memDefaultAlloc());
    if (!ini)
        return false;

    char keyTrimmed[64];
    char valueTrimmed[256];
    uint32 count = 0;

    for (int i = 0; i < ini_section_count(ini); i++) {
        const char* sectionName = ini_section_name(ini, i);

        for (uint32 c = 0; c < gSettings.customCallbacks.Count(); c++) {
            SettingsCustomCallbacks* callbacks = gSettings.customCallbacks[c];

            uint32 foundCatId = UINT32_MAX;
            for (uint32 catId = 0, catIdCount = callbacks->GetCategoryCount(); catId < catIdCount; catId++) {
                if (strIsEqualNoCase(sectionName, callbacks->GetCategory(catId))) {
                    foundCatId = catId;
                    break;
                }
            }

            for (int j = 0; j < ini_property_count(ini, i); j++) {
                const char* key = ini_property_name(ini, i, j);
                const char* value = ini_property_value(ini, i, j);
                strTrim(keyTrimmed, sizeof(keyTrimmed), key);
                strTrim(valueTrimmed, sizeof(valueTrimmed), value);

                bool predefined = foundCatId != UINT32_MAX ? callbacks->ParseSetting(foundCatId, keyTrimmed, valueTrimmed) : false;

                // if doesn't exist in the predefined settings, add to the general settings
                if (!predefined)
                    settingsSetValue(keyTrimmed, valueTrimmed);

                char msg[256];
                strPrintFmt(msg, sizeof(msg), "\t%u) %s%s = %s\n", ++count, keyTrimmed, !predefined ? "(*)" : "", valueTrimmed);
                debugPrint(msg);
            }
        } // for each custom settings parser
    }

    // Try to load none-predefined settings
    int sectionId = ini_find_section(ini, SETTINGS_NONE_PREDEFINED, strLen(SETTINGS_NONE_PREDEFINED));
    if (sectionId != -1) {
        for (int i = 0; i < ini_property_count(ini, sectionId); i++) {
            settingsSetValue(ini_property_name(ini, sectionId, i), ini_property_value(ini, sectionId, i));
        }
    }

    ini_destroy(ini);
    return true;
}

#if PLATFORM_ANDROID
bool settingsInitializeFromAndroidAsset(AAssetManager* assetMgr, const char* iniFilepath)
{
    char msg[256];
    strPrintFmt(msg, sizeof(msg), "Loading settings from assets: %s\n", iniFilepath);
    debugPrint(msg);

    Blob blob;
    AAsset* asset = AAssetManager_open(assetMgr, iniFilepath, AASSET_MODE_BUFFER);
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

    if (!blob.IsValid()) {
        strPrintFmt(msg, sizeof(msg), "Opening ini file '%s' failed\n", iniFilepath);
        debugPrint(msg);
        return false;
    }

    bool r = settingsLoadFromINIInternal(blob);
    blob.Free();

    if (!r) {
        strPrintFmt(msg, sizeof(msg), "Parsing ini file '%s' failed\n", iniFilepath);
        debugPrint(msg);
    }
    return r;
}
#endif  // PLATFORM_ANDROID

bool settingsInitializeFromINI(const char* iniFilepath)
{
    char msg[256];
    strPrintFmt(msg, sizeof(msg), "Loading settings from file: %s", iniFilepath);
    debugPrint(msg);

    Blob blob;
    File f;
    if (f.Open(iniFilepath, FileOpenFlags::Read | FileOpenFlags::SeqScan)) {
        uint64 size = f.GetSize();
        if (size) {
            blob.Reserve(size + 1);
            size_t bytesRead = f.Read(const_cast<void*>(blob.Data()), size);
            blob.SetSize(bytesRead);
            blob.Write<char>('\0');
        }
        f.Close();
    }

    if (!blob.IsValid()) {
        strPrintFmt(msg, sizeof(msg), "Opening ini file '%s' failed", iniFilepath);
        debugPrint(msg);
        return false;
    }

    bool r = settingsLoadFromINIInternal(blob);
    blob.Free();

    if (!r) {
        strPrintFmt(msg, sizeof(msg), "Parsing ini file '%s' failed", iniFilepath);
        debugPrint(msg);
    }
    return r;
}

void settingsSaveToINI(const char* iniFilepath)
{
    char msg[256];
    strPrintFmt(msg, sizeof(msg), "Saving settings to file: %s", iniFilepath);
    debugPrint(msg);
    
    MemTempAllocator tmpAlloc;
    ini_t* ini = ini_create(&tmpAlloc);
    
    for (SettingsCustomCallbacks* callbacks : gSettings.customCallbacks) {
        for (uint32 cId = 0; cId < callbacks->GetCategoryCount(); cId++) {
            Array<SettingsKeyValue> items(&tmpAlloc);
            const char* catName = callbacks->GetCategory(cId);
            int sectionId = ini_section_add(ini, catName, strLen(catName));
            
            callbacks->SaveCategory(cId, items);
            
            for (SettingsKeyValue& item : items) {
                if (item.value.Length())
                    ini_property_add(ini, sectionId, item.key.CStr(), item.key.Length(), item.value.CStr(), item.value.Length());
            }

            items.Free();
        }
    }

    // Put None-predefined settings into INI as well
    if (gSettings.keyValuePairs.Count()) {
        int sectionId = ini_section_add(ini, SETTINGS_NONE_PREDEFINED, strLen(SETTINGS_NONE_PREDEFINED));
        
        for (SettingsKeyValue& item : gSettings.keyValuePairs) {
            if (item.value.Length())
                ini_property_add(ini, sectionId, item.key.CStr(), item.key.Length(), item.value.CStr(), item.value.Length());
        }
    }

    int size = ini_save(ini, nullptr, 0);
    if (size > 0) {
        char* data = tmpAlloc.MallocTyped<char>(size);
        ini_save(ini, data, size);

        File f;
        if (f.Open(iniFilepath, FileOpenFlags::Write)) {
            f.Write(data, size);
            f.Close();
        }
    }

    ini_destroy(ini);
}

bool settingsInitializeFromCommandLine(int argc, char* argv[])
{
    sargs_state* args = sargs_create(sargs_desc {
        .argc = argc,
        .argv = argv
    });

    if (sargs_num_args(args) > 0) 
        debugPrint("Loading settings from CommandLine:\n");

    for (int i = 0; i < sargs_num_args(args); i++) {
        const char* key = sargs_key_at(args, i);
        const char* value = sargs_value_at(args, i);

        // skip keys with no dash '-'
        if (key[0] != '-')
            continue;
        ++key;

        for (uint32 c = 0; c < gSettings.customCallbacks.Count(); c++) {
            SettingsCustomCallbacks* callbacks = gSettings.customCallbacks[c];

            // check predefined settings. For that, first check the first word for predefined categories
            uint32 foundCatId = UINT32_MAX;
            uint32 catLen = 0;
            for (uint32 catId = 0, catIdCount = callbacks->GetCategoryCount(); catId < catIdCount; catId++) {
                const char* cat = callbacks->GetCategory(catId);
                catLen = strLen(cat);
                if (strIsEqualNoCaseCount(key, cat, catLen)) {
                    foundCatId = catId;
                    break;
                }
            }
    
            bool predefined = foundCatId != UINT32_MAX ? callbacks->ParseSetting(foundCatId, key + catLen, value) : false;

            // if doesn't exist in the predefined settings, add to the general settings
            if (!predefined)
                settingsSetValue(key, value);

            char msg[256];
            strPrintFmt(msg, sizeof(msg), "\t%d) %s%s = %s\n", i+1, key, !predefined ? "(*)" : "", value);
            debugPrint(msg);
        }        

    }

    sargs_destroy(args);
    return true;
}

void settingsSetValue(const char* key, const char* value)
{
    if (value[0] == 0)
        return;

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

void settingsRelease()
{
    gSettings.keyValuePairs.Free();
}

