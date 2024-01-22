#pragma once

#include "Core/Base.h"

struct SysInfo;
struct MemBumpAllocatorBase;

API bool engineInitialize();
API void engineRelease();

API void engineBeginFrame(float dt);
API void engineEndFrame(float dt);

API uint64 engineFrameIndex();
API const SysInfo& engineGetSysInfo();
API float engineGetCpuFrameTimeMS();

API MemBumpAllocatorBase* engineGetInitHeap();

// Shortcut string is a combination of keys joined by + sign
//  Example: "K+SHIFT+CTRL"
using EngineShortcutCallback = void(*)(void* userData);
API void engineRegisterShortcut(const char* shortcut, EngineShortcutCallback callback, void* userData = nullptr);

