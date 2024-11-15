#pragma once

#include "Core/Base.h"

struct SysInfo;
struct MemBumpAllocatorBase;
struct MemProxyAllocator;
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
    API const SysInfo& GetSysInfo();
    API float GetEngineTimeMS();

    API MemBumpAllocatorBase* GetInitHeap();

    API bool IsMainThread();

    // Shortcut string is a combination of keys joined by + sign
    //  Example: "K+SHIFT+CTRL"
    API void RegisterShortcut(const char* shortcut, EngineShortcutCallback callback, void* userData = nullptr);

    // TODO: Proper error-handling within the callback
    API const AssetGroup& RegisterInitializeResources(EngineInitializeResourcesCallback callback, void* userData = nullptr);

    API void RegisterProxyAllocator(MemProxyAllocator* alloc);

    namespace _private
    {
        void PostInitialize();
    }
} // Engine
    