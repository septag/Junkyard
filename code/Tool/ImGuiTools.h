#pragma once

#include "../Core/Base.h"

#include "../Graphics/ImGuiWrapper.h"

#include "../External/ImGuizmo/ImGuizmo.h"

#define IMGUI_ALPHA_WINDOW(_id) \
    static float CONCAT(_id, _alpha) = 1.0f;   \
    ImGui::SetNextWindowBgAlpha(CONCAT(_id, _alpha));

#define IMGUI_ALPHA_CONTROL(_id) \
    _private::imguiControlAlphaWithScroll(ImGui::IsWindowHovered() ? &CONCAT(_id, _alpha) : nullptr)

API void imguiBudgetHub(float dt, bool* pOpen = nullptr);
API void imguiQuickInfoHud(float dt, bool *pOpen = nullptr);

struct LogEntry; // Log.h
namespace _private
{
    void imguiQuickInfoHud_SetTargetFps(uint32 targetFps);
    void imguiQuickInfoHud_Log(const LogEntry& entry, void*);
}
