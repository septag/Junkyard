#pragma once

#include "../Core/Base.h"
#include "../Core/MathTypes.h"

// Note: External dependency to imgui.h
#include "../External/imgui/imgui.h"

#define IMGUI_ALPHA_WINDOW(_id) \
    static float CONCAT(_id, _alpha) = 1.0f;   \
    ImGui::SetNextWindowBgAlpha(CONCAT(_id, _alpha));

#define IMGUI_ALPHA_CONTROL(_id) \
    ImGui::ControlAlphaWithScroll(ImGui::IsWindowHovered() ? &CONCAT(_id, _alpha) : nullptr)

struct MemTlsfAllocator;

namespace ImGui
{
    namespace _private
    {
        void DisableSkipItems();
    }

    template <typename F> void Align(float align, const F& f) 
    {
        const ImVec2 containerSize = ImGui::GetContentRegionAvail();
        const ImVec2 cp = ImGui::GetCursorScreenPos();
        ImGuiStyle& style = ImGui::GetStyle();
        float alpha_backup = style.DisabledAlpha;
        style.DisabledAlpha = 0;
        ImGui::BeginDisabled();
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking;
        const char* id = "imgui_measure__";
        ImGui::Begin(id, nullptr, flags);
        ImGui::_private::DisableSkipItems();
        
        //
        ImGui::BeginGroup();
        f();
        ImGui::EndGroup();
        const ImVec2 size = ImGui::GetItemRectSize();
        ImGui::End();
        ImGui::EndDisabled();
        style.DisabledAlpha = alpha_backup;
        ImGui::SetCursorScreenPos(ImVec2(cp.x + (containerSize.x - size.x) * align, cp.y));
        f();
    }

    template <typename F> void AlignRight(const F& f) { Align(1, f); }
    template <typename F> void AlignCenter(const F& f) { Align(0.5f, f); }

    inline ImVec4 ColorToImVec4(const Color& c) { return ImVec4(c.r, c.g, c.b, c.a); }

    API void SeparatorVertical(float thickness = 1.0f);
    API bool ToggleButton(const char* label, bool* toggled, const ImVec2& size_arg = ImVec2(0, 0));
    API void ControlAlphaWithScroll(float* alpha);

    API const char* GetSetting(const char* key);
    API void SetSetting(const char* key, bool b);
    API void SetSetting(const char* key, int i);

    API bool IsEnabled();
    API void BeginFrame(float dt);
    API bool DrawFrame();

    API bool Initialize();
    API void Release();
}

