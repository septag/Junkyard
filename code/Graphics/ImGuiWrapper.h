#pragma once

#include "../Core/Base.h"
#include "../Math/MathTypes.h"

#include "../External/imgui/imgui.h"

struct MemTlsfAllocator;

struct ImGuiBudgetStats
{
    size_t initHeapStart;
    size_t initHeapSize;
    size_t runtimeHeapSize;
    size_t runtimeHeapMax;
    uint32 maxVertices;
    uint32 maxIndices;
    uint32 lastFrameVertices;
    uint32 lastFrameIndices;
    MemTlsfAllocator* runtimeHeap;
};

API bool imguiRender();
API bool imguiIsEnabled();

API void imguiLabel(const char* name, const char* fmt, ...);
API void imguiLabel(Color nameColor, Color textColor, const char* name, const char* fmt, ...);
API void imguiLabel(float offset, float spacing, const char* name, const char* fmt, ...);
API void imguiLabel(Color nameColor, Color textColor, float offset, float spacing, const char* name, const char* fmt, ...);

API const char* imguiGetSetting(const char* key);
API void imguiSetSetting(const char* key, bool b);
API void imguiSetSetting(const char* key, int i);

API void imguiGetBudgetStats(ImGuiBudgetStats* stats);

namespace _private 
{
    bool imguiInitialize();
    void imguiRelease();
    void imguiBeginFrame(float dt);

    void imguiControlAlphaWithScroll(float* alpha);
}

