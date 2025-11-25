#pragma once

#include "Core/Base.h"

struct SysInfo;
struct MemProxyAllocator;
struct MemBumpAllocatorVM;
struct AssetGroup;

using EngineShortcutCallback = void(*)(void* userData);
using EngineInitializeResourcesCallback = void(*)(void* userData);

namespace Engine
{
    API bool Initialize();
    API void Release();

    API void BeginFrame(float dt);
    API void EndFrame();

    API uint64 GetFrameIndex();
    API float GetFrameTime();
    API const SysInfo& GetSysInfo();
    API float GetEngineTimeMS();

    API bool IsMainThread();

    // Shortcut string is a combination of keys joined by + sign
    //  Example: "K+SHIFT+CTRL"
    API void RegisterShortcut(const char* shortcut, EngineShortcutCallback callback, void* userData = nullptr);

    // TODO: Proper error-handling within the callback
    // Register a callback that runs right after initial asset group (returned by this function) is done loading assets
    API const AssetGroup& RegisterInitializeResources(EngineInitializeResourcesCallback callback, void* userData = nullptr);

    API void RegisterProxyAllocator(MemProxyAllocator* alloc);
    API void HelperInitializeProxyAllocator(MemProxyAllocator* alloc, const char* name, MemAllocator* baseAlloc = nullptr);

    API void RegisterVMAllocator(MemBumpAllocatorVM* alloc, const char* name);
    API void UnregisterVMAllocator(MemBumpAllocatorVM* alloc);

    namespace _private
    {
        void PostInitialize();  // Called internally by Application after everything is initialized
    }
} // Engine
    