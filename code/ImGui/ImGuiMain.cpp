#include "ImGuiMain.h"

#include <stdarg.h>

#include "SegoeFont.h"

#include "../External/imgui/imgui.h"
#include "../External/imgui/imgui_internal.h"

#include "../Core/StringUtil.h"
#include "../Core/Hash.h"
#include "../Core/Log.h"
#include "../Core/MathAll.h"
#include "../Core/IniParser.h"

#include "../Assets/Shader.h"
#include "../Assets/AssetManager.h"

#include "../Common/Application.h"
#include "../Common/VirtualFS.h"
#include "../Common/JunkyardSettings.h"

#include "../Graphics/GfxBackend.h"

#include "../Engine.h"

// Extra modules
#include "ImGuizmo.h"

static constexpr size_t IMGUI_RUNTIME_HEAP_SIZE = 4*SIZE_MB;
static constexpr uint32 IMGUI_VERTICES_POOL_SIZE = 32*1000;
static constexpr uint32 IMGUI_INDICES_POOL_SIZE =  IMGUI_VERTICES_POOL_SIZE*3; 

enum ImGuiDescriptorSet : uint32
{
    IMGUI_DESCRIPTORSET_FONT_IMAGE = 0,
    IMGUI_DESCRIPTORSET_NO_IMAGE,
    _IMGUI_DESCRIPTORSET_COUNT
};

struct ImGuiShaderTransform
{
    Mat4 projMat;
};

// Renderer side of a secondary viewport. Each one owns a swapchain and its own geometry buffers,
// because viewports are rendered in separate command buffers
struct ImGuiViewportRenderData
{
    GfxSwapchainHandle swapchain;
    GfxBufferHandle vertexBuffer;
    GfxBufferHandle indexBuffer;
    uint32 maxVertices;
    uint32 maxIndices;
};

struct ImGuiState
{
    MemProxyAllocator alloc;
    MemTlsfAllocator runtimeAlloc;

    ImGuiContext* ctx;

    bool mouseButtonDown[(uint32)InputMouseButton::_Count];
    bool mouseButtonUp[(uint32)InputMouseButton::_Count];
    float mouseWheelH;
    float mouseWheel;
    ImGuiMouseCursor lastCursor;
    bool viewportsEnabled;
    bool frameRendered;     // Render() ran this frame, so the platform windows can be updated
    
    uint32 maxVertices;
    uint32 maxIndices;

    GfxBufferHandle vertexBuffer;
    GfxBufferHandle indexBuffer;
    GfxPipelineLayoutHandle pipelineLayout;
    GfxPipelineHandle pipeline;
    GfxSamplerHandle sampler;
    AssetHandleShader shader;
    GfxMultiSampleCount msaa = GfxMultiSampleCount::SampleCount1;
    float* alphaControl;      // alpha value that will be modified by mouse-wheel + ALT

    HashTable<const char*> settingsCacheTable;
    INIFileContext settingsIni;
};

ImGuiState gImGui;

namespace ImGui
{
    [[maybe_unused]] INLINE ImVec4 _ToImVec4(Float4 v)
    {
        return ImVec4 { v.x, v.y, v.z, v.w };
    }

    [[maybe_unused]] INLINE ImVec2 _ToImVec2(Float2 v)
    {
        return ImVec2 { v.x, v.y };
    }

    [[maybe_unused]] INLINE Float2 _ToFloat2(ImVec2 v)
    {
        return Float2(v.x, v.y);
    }

    static void _InitializeSettings()
    {
        gImGui.settingsCacheTable.SetAllocator(&gImGui.runtimeAlloc);
        gImGui.settingsCacheTable.Reserve(256);

        // Load extra control settings
        {
            MemTempAllocator tmpAlloc;
            char iniFilename[64];
            Str::PrintFmt(iniFilename, sizeof(iniFilename), "%s_imgui_controls.ini", App::GetName());
            Blob data = Vfs::ReadFile(iniFilename, VfsFlags::TextFile|VfsFlags::AbsolutePath, &tmpAlloc);
            if (data.IsValid())
                gImGui.settingsIni = INIFile::LoadFromString((const char*)data.Data());
        }

        // populate the settings cache
        if (gImGui.settingsIni.IsValid()) {
            const INIFileContext& ini = gImGui.settingsIni;
            for (uint32 s = 0; s < ini.GetSectionCount(); s++) {
                INIFileSection section = ini.GetSection(s);

                String64 keyParent(section.GetName());
                for (uint32 p = 0; p < section.GetPropertyCount(); p++) {
                    INIFileProperty prop = section.GetProperty(p);
                    String64 key(keyParent);
                    key.Append(".");
                    key.Append(prop.GetName());

                    gImGui.settingsCacheTable.Add(Hash::Fnv32Str(key.CStr()), prop.GetValue());
                }
            }
        }
        else {
            gImGui.settingsIni = INIFile::Create();
        }
    }

    static void _ReleaseSettings()
    {
        if (gImGui.settingsIni.IsValid()) {
            char iniFilename[64];
            Str::PrintFmt(iniFilename, sizeof(iniFilename), "%s_imgui_controls.ini", App::GetName());
            INIFile::Save(gImGui.settingsIni, iniFilename);
            gImGui.settingsIni.Destroy();
        }

        gImGui.settingsCacheTable.Free();
    }

    static void _SetColorTheme()
    {
        ImGuiStyle& style = GetStyle();
        StyleColorsDark(&style);
    
        style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    
        style.ScrollbarSize = 10;
        style.GrabMinSize = 12;
        style.WindowBorderSize = 1;
        style.ChildBorderSize = 0;
        style.PopupBorderSize = 0;
        style.FrameBorderSize = 0;
        style.TabBorderSize = 0;
    
        style.WindowRounding = 0;
        style.ChildRounding = 3;
        style.FrameRounding = 3;
        style.PopupRounding = 3;
        style.ScrollbarRounding = 3;
        style.GrabRounding = 3;
        style.TabRounding = 2;
    
        style.AntiAliasedFill = true;
        style.AntiAliasedLines = true;
    
        style.Colors[ImGuiCol_Text]                   = ImVec4(1.00f, 1.00f, 1.00f, 0.89f);
        style.Colors[ImGuiCol_TextDisabled]           = ImVec4(1.00f, 1.00f, 1.00f, 0.39f);
        style.Colors[ImGuiCol_WindowBg]               = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        style.Colors[ImGuiCol_ChildBg]                = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
        style.Colors[ImGuiCol_PopupBg]                = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        style.Colors[ImGuiCol_Border]                 = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);
        style.Colors[ImGuiCol_BorderShadow]           = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
        style.Colors[ImGuiCol_FrameBg]                = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
        style.Colors[ImGuiCol_FrameBgHovered]         = ImVec4(1.00f, 1.00f, 1.00f, 0.08f);
        style.Colors[ImGuiCol_FrameBgActive]          = ImVec4(1.00f, 1.00f, 1.00f, 0.12f);
        style.Colors[ImGuiCol_TitleBg]                = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
        style.Colors[ImGuiCol_TitleBgActive]          = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
        style.Colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
        style.Colors[ImGuiCol_MenuBarBg]              = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
        style.Colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
        style.Colors[ImGuiCol_CheckMark]              = ImVec4(0.80f, 0.47f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_SliderGrab]             = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
        style.Colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.80f, 0.47f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_Button]                 = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
        style.Colors[ImGuiCol_ButtonHovered]          = ImVec4(1.00f, 1.00f, 1.00f, 0.39f);
        style.Colors[ImGuiCol_ButtonActive]           = ImVec4(1.00f, 1.00f, 1.00f, 0.55f);
        style.Colors[ImGuiCol_Header]                 = ImVec4(0.00f, 0.00f, 0.00f, 0.39f);
        style.Colors[ImGuiCol_HeaderHovered]          = ImVec4(1.00f, 1.00f, 1.00f, 0.16f);
        style.Colors[ImGuiCol_HeaderActive]           = ImVec4(1.00f, 1.00f, 1.00f, 0.16f);
        style.Colors[ImGuiCol_Separator]              = ImVec4(1.00f, 1.00f, 1.00f, 0.15f);
        style.Colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.80f, 0.47f, 0.00f, 0.50f);
        style.Colors[ImGuiCol_SeparatorActive]        = ImVec4(0.80f, 0.47f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_ResizeGrip]             = ImVec4(1.00f, 1.00f, 1.00f, 0.25f);
        style.Colors[ImGuiCol_ResizeGripHovered]      = ImVec4(1.00f, 1.00f, 1.00f, 0.31f);
        style.Colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.80f, 0.47f, 0.00f, 0.86f);
        style.Colors[ImGuiCol_Tab]                    = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
        style.Colors[ImGuiCol_TabHovered]             = ImVec4(0.80f, 0.47f, 0.00f, 0.25f);
        style.Colors[ImGuiCol_TabSelected]            = ImVec4(0.80f, 0.47f, 0.00f, 0.59f);
        style.Colors[ImGuiCol_TabDimmed]              = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
        style.Colors[ImGuiCol_TabDimmedSelected]      = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        style.Colors[ImGuiCol_PlotLines]              = ImVec4(0.86f, 0.86f, 0.86f, 1.00f);
        style.Colors[ImGuiCol_PlotLinesHovered]       = ImVec4(0.80f, 0.47f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_PlotHistogram]          = ImVec4(0.80f, 0.47f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.89f, 0.62f, 1.00f);
        style.Colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.80f, 0.47f, 0.00f, 0.25f);
        style.Colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 0.86f, 0.00f, 0.86f);
        style.Colors[ImGuiCol_NavCursor]              = ImVec4(0.80f, 0.47f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.71f);
        style.Colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        style.Colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
    }

    static ImGuiKey _MapKeycode(InputKeycode keycode)
    {
        switch (keycode) {
        case InputKeycode::Space:           return ImGuiKey_Space;
        case InputKeycode::Apostrophe:      return ImGuiKey_Apostrophe;
        case InputKeycode::Comma:           return ImGuiKey_Comma;
        case InputKeycode::Minus:           return ImGuiKey_Minus;
        case InputKeycode::Period:          return ImGuiKey_Period;
        case InputKeycode::Slash:           return ImGuiKey_Slash;
        case InputKeycode::NUM0:            return ImGuiKey_0;
        case InputKeycode::NUM1:            return ImGuiKey_1;
        case InputKeycode::NUM2:            return ImGuiKey_2;
        case InputKeycode::NUM3:            return ImGuiKey_3;
        case InputKeycode::NUM4:            return ImGuiKey_4;
        case InputKeycode::NUM5:            return ImGuiKey_5;
        case InputKeycode::NUM6:            return ImGuiKey_6;
        case InputKeycode::NUM7:            return ImGuiKey_7;
        case InputKeycode::NUM8:            return ImGuiKey_8;
        case InputKeycode::NUM9:            return ImGuiKey_9;
        case InputKeycode::Semicolon:       return ImGuiKey_Semicolon;
        case InputKeycode::Equal:           return ImGuiKey_Equal;
        case InputKeycode::A:               return ImGuiKey_A;
        case InputKeycode::B:               return ImGuiKey_B;
        case InputKeycode::C:               return ImGuiKey_C;
        case InputKeycode::D:               return ImGuiKey_D;
        case InputKeycode::E:               return ImGuiKey_E;
        case InputKeycode::F:               return ImGuiKey_F;
        case InputKeycode::G:               return ImGuiKey_G;
        case InputKeycode::H:               return ImGuiKey_H;
        case InputKeycode::I:               return ImGuiKey_I;
        case InputKeycode::J:               return ImGuiKey_J;
        case InputKeycode::K:               return ImGuiKey_K;
        case InputKeycode::L:               return ImGuiKey_L;
        case InputKeycode::M:               return ImGuiKey_M;
        case InputKeycode::N:               return ImGuiKey_N;
        case InputKeycode::O:               return ImGuiKey_O;
        case InputKeycode::P:               return ImGuiKey_P;
        case InputKeycode::Q:               return ImGuiKey_Q;
        case InputKeycode::R:               return ImGuiKey_R;
        case InputKeycode::S:               return ImGuiKey_S;
        case InputKeycode::T:               return ImGuiKey_T;
        case InputKeycode::U:               return ImGuiKey_U;
        case InputKeycode::V:               return ImGuiKey_V;
        case InputKeycode::W:               return ImGuiKey_W;
        case InputKeycode::X:               return ImGuiKey_X;
        case InputKeycode::Y:               return ImGuiKey_Y;
        case InputKeycode::Z:               return ImGuiKey_Z;
        case InputKeycode::LeftBracket:     return ImGuiKey_LeftBracket;
        case InputKeycode::Backslash:       return ImGuiKey_Backslash;
        case InputKeycode::RightBracket:    return ImGuiKey_RightBracket;
        case InputKeycode::GraveAccent:     return ImGuiKey_GraveAccent;
        case InputKeycode::World1:          return ImGuiKey_Oem102;
        case InputKeycode::Escape:          return ImGuiKey_Escape;
        case InputKeycode::Enter:           return ImGuiKey_Enter;
        case InputKeycode::Tab:             return ImGuiKey_Tab;
        case InputKeycode::Backspace:       return ImGuiKey_Backspace;
        case InputKeycode::Insert:          return ImGuiKey_Insert;
        case InputKeycode::Delete:          return ImGuiKey_Delete;
        case InputKeycode::Right:           return ImGuiKey_RightArrow;
        case InputKeycode::Left:            return ImGuiKey_LeftArrow;
        case InputKeycode::Down:            return ImGuiKey_DownArrow;
        case InputKeycode::Up:              return ImGuiKey_UpArrow;
        case InputKeycode::PageUp:          return ImGuiKey_PageUp;
        case InputKeycode::PageDown:        return ImGuiKey_PageDown;
        case InputKeycode::Home:            return ImGuiKey_Home;
        case InputKeycode::End:             return ImGuiKey_End;
        case InputKeycode::CapsLock:        return ImGuiKey_CapsLock;
        case InputKeycode::ScrollLock:      return ImGuiKey_ScrollLock;
        case InputKeycode::NumLock:         return ImGuiKey_NumLock;
        case InputKeycode::PrintScreen:     return ImGuiKey_PrintScreen;
        case InputKeycode::Pause:           return ImGuiKey_Pause;
        case InputKeycode::F1:              return ImGuiKey_F1;
        case InputKeycode::F2:              return ImGuiKey_F2;
        case InputKeycode::F3:              return ImGuiKey_F3;
        case InputKeycode::F4:              return ImGuiKey_F4;
        case InputKeycode::F5:              return ImGuiKey_F5;
        case InputKeycode::F6:              return ImGuiKey_F6;
        case InputKeycode::F7:              return ImGuiKey_F7;
        case InputKeycode::F8:              return ImGuiKey_F8;
        case InputKeycode::F9:              return ImGuiKey_F9;
        case InputKeycode::F10:             return ImGuiKey_F10;
        case InputKeycode::F11:             return ImGuiKey_F11;
        case InputKeycode::F12:             return ImGuiKey_F12;
        case InputKeycode::F13:             return ImGuiKey_F13;
        case InputKeycode::F14:             return ImGuiKey_F14;
        case InputKeycode::F15:             return ImGuiKey_F15;
        case InputKeycode::F16:             return ImGuiKey_F16;
        case InputKeycode::F17:             return ImGuiKey_F17;
        case InputKeycode::F18:             return ImGuiKey_F18;
        case InputKeycode::F19:             return ImGuiKey_F19;
        case InputKeycode::F20:             return ImGuiKey_F20;
        case InputKeycode::F21:             return ImGuiKey_F21;
        case InputKeycode::F22:             return ImGuiKey_F22;
        case InputKeycode::F23:             return ImGuiKey_F23;
        case InputKeycode::F24:             return ImGuiKey_F24;
        case InputKeycode::KP0:             return ImGuiKey_Keypad0;
        case InputKeycode::KP1:             return ImGuiKey_Keypad1;
        case InputKeycode::KP2:             return ImGuiKey_Keypad2;
        case InputKeycode::KP3:             return ImGuiKey_Keypad3;
        case InputKeycode::KP4:             return ImGuiKey_Keypad4;
        case InputKeycode::KP5:             return ImGuiKey_Keypad5;
        case InputKeycode::KP6:             return ImGuiKey_Keypad6;
        case InputKeycode::KP7:             return ImGuiKey_Keypad7;
        case InputKeycode::KP8:             return ImGuiKey_Keypad8;
        case InputKeycode::KP9:             return ImGuiKey_Keypad9;
        case InputKeycode::KPDecimal:       return ImGuiKey_KeypadDecimal;
        case InputKeycode::KPDivide:        return ImGuiKey_KeypadDivide;
        case InputKeycode::KPMultiply:      return ImGuiKey_KeypadMultiply;
        case InputKeycode::KPSubtract:      return ImGuiKey_KeypadSubtract;
        case InputKeycode::KPAdd:           return ImGuiKey_KeypadAdd;
        case InputKeycode::KPEnter:         return ImGuiKey_KeypadEnter;
        case InputKeycode::KPEqual:         return ImGuiKey_KeypadEqual;
        case InputKeycode::LeftShift:       return ImGuiKey_LeftShift;
        case InputKeycode::LeftControl:     return ImGuiKey_LeftCtrl;
        case InputKeycode::LeftAlt:         return ImGuiKey_LeftAlt;
        case InputKeycode::LeftSuper:       return ImGuiKey_LeftSuper;
        case InputKeycode::RightShift:      return ImGuiKey_RightShift;
        case InputKeycode::RightControl:    return ImGuiKey_RightCtrl;
        case InputKeycode::RightAlt:        return ImGuiKey_RightAlt;
        case InputKeycode::RightSuper:      return ImGuiKey_RightSuper;
        case InputKeycode::Menu:            return ImGuiKey_Menu;
        default:                            return ImGuiKey_None;
        }
    }

    // Feeds a key press/release plus the matching modifier state into the ImGui event queue.
    // Events are pushed as they arrive, so press/release ordering within a frame is preserved.
    static void _DispatchKeyEvent(InputKeycode keycode, bool down)
    {
        ImGuiIO& io = GetIO();

        switch (keycode) {
        case InputKeycode::LeftShift:
        case InputKeycode::RightShift:      io.AddKeyEvent(ImGuiMod_Shift, down);   break;
        case InputKeycode::LeftControl:
        case InputKeycode::RightControl:    io.AddKeyEvent(ImGuiMod_Ctrl, down);    break;
        case InputKeycode::LeftAlt:
        case InputKeycode::RightAlt:        io.AddKeyEvent(ImGuiMod_Alt, down);     break;
        case InputKeycode::LeftSuper:
        case InputKeycode::RightSuper:      io.AddKeyEvent(ImGuiMod_Super, down);   break;
        default: break;
        }

        ImGuiKey key = _MapKeycode(keycode);
        if (key != ImGuiKey_None)
            io.AddKeyEvent(key, down);
    }

    static void _UpdateCursor()
    {
        static_assert(ImGuiMouseCursor_None == static_cast<ImGuiMouseCursor>(AppMouseCursor::None));
        static_assert(ImGuiMouseCursor_Arrow == static_cast<ImGuiMouseCursor>(AppMouseCursor::Arrow));
        static_assert(ImGuiMouseCursor_TextInput == static_cast<ImGuiMouseCursor>(AppMouseCursor::TextInput));
        static_assert(ImGuiMouseCursor_ResizeAll == static_cast<ImGuiMouseCursor>(AppMouseCursor::ResizeAll));
        static_assert(ImGuiMouseCursor_ResizeEW == static_cast<ImGuiMouseCursor>(AppMouseCursor::ResizeWE));
        static_assert(ImGuiMouseCursor_ResizeNS == static_cast<ImGuiMouseCursor>(AppMouseCursor::ResizeNS));
        static_assert(ImGuiMouseCursor_ResizeNESW == static_cast<ImGuiMouseCursor>(AppMouseCursor::ResizeNESW));
        static_assert(ImGuiMouseCursor_ResizeNWSE == static_cast<ImGuiMouseCursor>(AppMouseCursor::ResizeNWSE));
        static_assert(ImGuiMouseCursor_Hand == static_cast<ImGuiMouseCursor>(AppMouseCursor::Hand));
        static_assert(ImGuiMouseCursor_Wait == static_cast<ImGuiMouseCursor>(AppMouseCursor::Wait));
        static_assert(ImGuiMouseCursor_Progress == static_cast<ImGuiMouseCursor>(AppMouseCursor::Progress));
        static_assert(ImGuiMouseCursor_NotAllowed == static_cast<ImGuiMouseCursor>(AppMouseCursor::NotAllowed));
        static_assert(ImGuiMouseCursor_COUNT == static_cast<ImGuiMouseCursor>(AppMouseCursor::_Count));
    
        ImGuiIO& io = GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)
            return;
    
        ImGuiMouseCursor imCursor = GetMouseCursor();
        if (io.MouseDrawCursor)
            App::SetCursor(AppMouseCursor::None);
        else
            App::SetCursor(static_cast<AppMouseCursor>(imCursor));
    }

    //------------------------------------------------------------------------------------------------------------
    // Multi-viewport platform backend
    // Maps ImGui's platform window requests onto the App window API. The renderer side is not implemented yet,
    // so secondary viewports get a real OS window but nothing draws into them
    static AppWindowHandle _ViewportWindow(ImGuiViewport* viewport)
    {
        return AppWindowHandle { PtrToInt<uint32>(viewport->PlatformUserData) };
    }

    static ImGuiViewport* _FindViewportForWindow(AppWindowHandle window)
    {
        if (!window.IsValid())
            return nullptr;
        return FindViewportByPlatformHandle(IntToPtr<uint32>(window.mId));
    }


    static void _PlatformCreateWindow(ImGuiViewport* viewport)
    {
        AppWindowFlags flags = AppWindowFlags::None;
        if (viewport->Flags & ImGuiViewportFlags_NoDecoration)          flags |= AppWindowFlags::NoDecoration;
        if (viewport->Flags & ImGuiViewportFlags_NoTaskBarIcon)         flags |= AppWindowFlags::NoTaskBarIcon;
        if (viewport->Flags & ImGuiViewportFlags_TopMost)               flags |= AppWindowFlags::TopMost;
        if (viewport->Flags & ImGuiViewportFlags_NoFocusOnAppearing)    flags |= AppWindowFlags::NoFocusOnAppearing;
        if (viewport->Flags & ImGuiViewportFlags_NoFocusOnClick)        flags |= AppWindowFlags::NoFocusOnClick;

        AppWindowHandle parent {};
        if (viewport->ParentViewportId != 0) {
            if (ImGuiViewport* parentViewport = FindViewportByID(viewport->ParentViewportId))
                parent = _ViewportWindow(parentViewport);
        }

        AppWindowDesc desc {
            .title = "Untitled",
            .geometry = {int(viewport->Pos.x), int(viewport->Pos.y), int(viewport->Size.x), int(viewport->Size.y)},
            .flags = flags,
            .parent = parent
        };

        AppWindowHandle window = App::CreateWindowHandle(desc);
        viewport->PlatformUserData = IntToPtr<uint32>(window.mId);
        viewport->PlatformHandle = IntToPtr<uint32>(window.mId);
        viewport->PlatformHandleRaw = window.IsValid() ? App::GetNativeWindowHandle(window) : nullptr;
    }

    static void _PlatformDestroyWindow(ImGuiViewport* viewport)
    {
        AppWindowHandle window = _ViewportWindow(viewport);

        // The main viewport borrows the main window, which is owned by App::Run
        if (window.IsValid() && window != App::GetMainWindow())
            App::DestroyWindowHandle(window);

        viewport->PlatformUserData = nullptr;
        viewport->PlatformHandle = nullptr;
        viewport->PlatformHandleRaw = nullptr;
    }

    static void _PlatformShowWindow(ImGuiViewport* viewport)
    {
        App::ShowWindow(_ViewportWindow(viewport));
    }

    static ImVec2 _PlatformGetWindowPos(ImGuiViewport* viewport)
    {
        AppRect rect = App::GetWindowGeometry(_ViewportWindow(viewport));
        return ImVec2(float(rect.x), float(rect.y));
    }

    static void _PlatformSetWindowPos(ImGuiViewport* viewport, ImVec2 pos)
    {
        App::SetWindowPos(_ViewportWindow(viewport), int(pos.x), int(pos.y));
    }

    static ImVec2 _PlatformGetWindowSize(ImGuiViewport* viewport)
    {
        AppRect rect = App::GetWindowGeometry(_ViewportWindow(viewport));
        return ImVec2(float(rect.width), float(rect.height));
    }

    static void _PlatformSetWindowSize(ImGuiViewport* viewport, ImVec2 size)
    {
        App::SetWindowSize(_ViewportWindow(viewport), int(size.x), int(size.y));
    }

    static ImVec2 _PlatformGetWindowFramebufferScale(ImGuiViewport*)
    {
        // Our window geometry is already in physical pixels, DPI is reported separately
        return ImVec2(1.0f, 1.0f);
    }

    static void _PlatformSetWindowFocus(ImGuiViewport* viewport)
    {
        App::FocusWindow(_ViewportWindow(viewport));
    }

    static bool _PlatformGetWindowFocus(ImGuiViewport* viewport)
    {
        return App::IsWindowFocused(_ViewportWindow(viewport));
    }

    static bool _PlatformGetWindowMinimized(ImGuiViewport* viewport)
    {
        return App::IsWindowMinimized(_ViewportWindow(viewport));
    }

    static void _PlatformSetWindowTitle(ImGuiViewport* viewport, const char* title)
    {
        App::SetWindowTitle(_ViewportWindow(viewport), title);
    }

    static void _PlatformSetWindowAlpha(ImGuiViewport* viewport, float alpha)
    {
        App::SetWindowAlpha(_ViewportWindow(viewport), alpha);
    }

    static float _PlatformGetWindowDpiScale(ImGuiViewport* viewport)
    {
        return App::GetWindowDPIScale(_ViewportWindow(viewport));
    }

    // Defined further down, next to the rest of the geometry handling
    static void _UploadDrawData(GfxCommandBuffer cmd, ImDrawData* drawData, 
                                GfxBufferHandle* vertexBuffer, uint32* maxVertices,
                                GfxBufferHandle* indexBuffer, uint32* maxIndices);
    static void _RecordDrawCommands(GfxCommandBuffer cmd, ImDrawData* drawData, 
                                    GfxBufferHandle vertexBuffer, GfxBufferHandle indexBuffer);

    static ImGuiViewportRenderData* _ViewportRenderData(ImGuiViewport* viewport)
    {
        return reinterpret_cast<ImGuiViewportRenderData*>(viewport->RendererUserData);
    }

    static void _RendererCreateWindow(ImGuiViewport* viewport)
    {
        void* nativeWindow = viewport->PlatformHandleRaw;
        if (nativeWindow == nullptr)
            return;

        Int2 size(int(viewport->Size.x), int(viewport->Size.y));
        GfxSwapchainHandle swapchain = GfxBackend::CreateSwapchain(nativeWindow, size);
        if (!swapchain.IsValid())
            return;

        ImGuiViewportRenderData* data = Mem::AllocZeroTyped<ImGuiViewportRenderData>(1, &gImGui.runtimeAlloc);
        data->swapchain = swapchain;
        viewport->RendererUserData = data;
    }

    static void _RendererDestroyWindow(ImGuiViewport* viewport)
    {
        ImGuiViewportRenderData* data = _ViewportRenderData(viewport);
        if (data == nullptr)
            return;

        GfxBackend::DestroySwapchain(data->swapchain);
        GfxBackend::DestroyBuffer(data->vertexBuffer);
        GfxBackend::DestroyBuffer(data->indexBuffer);

        Mem::Free(data, &gImGui.runtimeAlloc);
        viewport->RendererUserData = nullptr;
    }

    static void _RendererSetWindowSize(ImGuiViewport* viewport, ImVec2 size)
    {
        if (ImGuiViewportRenderData* data = _ViewportRenderData(viewport))
            GfxBackend::ResizeSwapchain(data->swapchain, Int2(int(size.x), int(size.y)));
    }

    static void _RendererRenderWindow(ImGuiViewport* viewport, void*)
    {
        ImGuiViewportRenderData* data = _ViewportRenderData(viewport);
        ImDrawData* drawData = viewport->DrawData;
        if (data == nullptr || drawData == nullptr || drawData->CmdLists.Size == 0)
            return;

        GfxCommandBuffer cmd = GfxBackend::BeginCommandBuffer(GfxQueueType::Graphics);

        // GpuProfilerScope holds a GfxCommandBuffer& and asserts on destruction that it is still
        // recording, so the zone has to close before EndCommandBuffer
        {
            GPU_PROFILE_ZONE(cmd, "ImGuiViewport");

            _UploadDrawData(cmd, drawData, &data->vertexBuffer, &data->maxVertices,
                            &data->indexBuffer, &data->maxIndices);

            // Viewport swapchains are plain color targets: no MSAA, no depth, no resolve
            GfxBackendRenderPass pass {
                .colorAttachments = {{ .clear = !(viewport->Flags & ImGuiViewportFlags_NoRendererClear) }},
                .swapchain = data->swapchain
            };
            cmd.BeginRenderPass(pass);
            _RecordDrawCommands(cmd, drawData, data->vertexBuffer, data->indexBuffer);
            cmd.EndRenderPass();
        }

        GfxBackend::EndCommandBuffer(cmd);
    }

    static void _UpdateMonitors()
    {
        AppMonitorInfo monitors[16];
        uint32 numMonitors = App::GetMonitors(monitors, CountOf(monitors));

        ImGuiPlatformIO& platformIO = GetPlatformIO();
        platformIO.Monitors.resize(0);
        for (uint32 i = 0; i < numMonitors; i++) {
            const AppMonitorInfo& src = monitors[i];
            ImGuiPlatformMonitor mon;
            mon.MainPos = ImVec2(float(src.mainRect.x), float(src.mainRect.y));
            mon.MainSize = ImVec2(float(src.mainRect.width), float(src.mainRect.height));
            mon.WorkPos = ImVec2(float(src.workRect.x), float(src.workRect.y));
            mon.WorkSize = ImVec2(float(src.workRect.width), float(src.workRect.height));
            mon.DpiScale = src.dpiScale;
            platformIO.Monitors.push_back(mon);
        }
    }

    static void _InitializeViewports()
    {
        // The ImGui pipeline is built once with gImGui.msaa. Viewport swapchains are plain non-MSAA
        // color targets, so an MSAA pipeline would not match their render pass
        ASSERT_MSG(gImGui.msaa == GfxMultiSampleCount::SampleCount1,
                   "ImGui multi-viewport requires MSAA to be off, or a second non-MSAA pipeline for viewports");

        ImGuiPlatformIO& platformIO = GetPlatformIO();
        platformIO.Platform_CreateWindow = _PlatformCreateWindow;
        platformIO.Platform_DestroyWindow = _PlatformDestroyWindow;
        platformIO.Platform_ShowWindow = _PlatformShowWindow;
        platformIO.Platform_GetWindowPos = _PlatformGetWindowPos;
        platformIO.Platform_SetWindowPos = _PlatformSetWindowPos;
        platformIO.Platform_GetWindowSize = _PlatformGetWindowSize;
        platformIO.Platform_SetWindowSize = _PlatformSetWindowSize;
        platformIO.Platform_GetWindowFramebufferScale = _PlatformGetWindowFramebufferScale;
        platformIO.Platform_SetWindowFocus = _PlatformSetWindowFocus;
        platformIO.Platform_GetWindowFocus = _PlatformGetWindowFocus;
        platformIO.Platform_GetWindowMinimized = _PlatformGetWindowMinimized;
        platformIO.Platform_SetWindowTitle = _PlatformSetWindowTitle;
        platformIO.Platform_SetWindowAlpha = _PlatformSetWindowAlpha;
        platformIO.Platform_GetWindowDpiScale = _PlatformGetWindowDpiScale;

        platformIO.Renderer_CreateWindow = _RendererCreateWindow;
        platformIO.Renderer_DestroyWindow = _RendererDestroyWindow;
        platformIO.Renderer_SetWindowSize = _RendererSetWindowSize;
        platformIO.Renderer_RenderWindow = _RendererRenderWindow;

        _UpdateMonitors();

        // Main viewport borrows the window that App::Run already created
        AppWindowHandle mainWindow = App::GetMainWindow();
        ImGuiViewport* mainViewport = GetMainViewport();
        mainViewport->PlatformUserData = IntToPtr<uint32>(mainWindow.mId);
        mainViewport->PlatformHandle = IntToPtr<uint32>(mainWindow.mId);
        mainViewport->PlatformHandleRaw = App::GetNativeWindowHandle(mainWindow);
    }

    // With viewports enabled ImGui works in desktop space, so viewport positions, monitor bounds and
    // the mouse all share one coordinate system. Otherwise we stay in main-window client space
    static void _UpdateMousePos(const AppEvent& ev)
    {
        ImGuiIO& io = GetIO();

        if (gImGui.viewportsEnabled) {
            io.AddMousePosEvent(ev.mouseDesktopX, ev.mouseDesktopY);
        }
        else {
            Float2 scale(io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
            io.AddMousePosEvent(ev.mouseX * scale.x, ev.mouseY * scale.y);
        }
    }

    static void _OnEventCallback(const AppEvent& ev, [[maybe_unused]] void* userData)
    {
        ImGuiIO& io = GetIO();

        switch (ev.type) {
        case AppEventType::MouseDown: {
                _UpdateMousePos(ev);
                gImGui.mouseButtonDown[uint32(ev.mouseButton)] = true;
            }
            break;
        case AppEventType::MouseUp: {
                _UpdateMousePos(ev);
                gImGui.mouseButtonUp[uint32(ev.mouseButton)] = true;
            }
            break;

        case AppEventType::MouseMove:
            _UpdateMousePos(ev);
            break;
        
        case AppEventType::MouseEnter:
        case AppEventType::MouseLeave:
            for (int i = 0; i < 3; i++) {
                gImGui.mouseButtonDown[i] = false;
                gImGui.mouseButtonUp[i] = false;
                io.MouseDown[i] = false;
            }
            break;

        case AppEventType::MouseScroll:
            gImGui.mouseWheelH = ev.scrollX;
            gImGui.mouseWheel += ev.scrollY;
            if (gImGui.alphaControl && App::GetKeyMods() == InputKeyModifiers::Ctrl)
                *gImGui.alphaControl = Clamp(*gImGui.alphaControl + M::Sign(ev.scrollY)*0.2f, 0.1f, 1.0f);
            
            break;
        
        case AppEventType::KeyDown:
            _DispatchKeyEvent(ev.keycode, true);
            break;
        
        case AppEventType::KeyUp:
            _DispatchKeyEvent(ev.keycode, false);
            break;
        
        case AppEventType::Char:
            io.AddInputCharacter(ev.charcode);
            break;
        
        case AppEventType::UpdateCursor:
            _UpdateCursor();
            break;
        
        case AppEventType::Resized: {
                if (ev.window == App::GetMainWindow()) {
                    io.DisplaySize = ImVec2(ev.framebufferWidth, ev.framebufferHeight);
                    float frameBufferScale = App::GetWindowDPIScale();
                    io.DisplayFramebufferScale = ImVec2(frameBufferScale, frameBufferScale);
                }
                else if (ImGuiViewport* viewport = _FindViewportForWindow(ev.window)) {
                    viewport->PlatformRequestResize = true;
                }
            }
            break;

        case AppEventType::Moved:
            if (ImGuiViewport* viewport = _FindViewportForWindow(ev.window))
                viewport->PlatformRequestMove = true;
            break;

        case AppEventType::WindowClose:
            if (ImGuiViewport* viewport = _FindViewportForWindow(ev.window))
                viewport->PlatformRequestClose = true;
            break;

        default:
            break;
        }
    }

    // Uploads the whole pixel buffer of `tex` into `image`. GfxBackend has no sub-rectangle copy,
    // so partial (ImTextureStatus_WantUpdates) requests re-upload the full texture. Atlases are
    // small and updates are rare, so this stays cheap.
    static void _UploadTexture(GfxCommandBuffer cmd, ImTextureData* tex, GfxImageHandle image)
    {
        GfxBufferDesc stagingBufferDesc {
            .sizeBytes = size_t(tex->GetSizeInBytes()),
            .usageFlags = GfxBufferUsageFlags::TransferSrc,
            .arena = GfxMemoryArena::TransientCPU
        };
        GfxBufferHandle stagingBuffer = GfxBackend::CreateBuffer(stagingBufferDesc);

        void* stagingData;
        size_t stagingDataSize;
        cmd.MapBuffer(stagingBuffer, &stagingData, &stagingDataSize);
        memcpy(stagingData, tex->GetPixels(), stagingBufferDesc.sizeBytes);
        cmd.FlushBuffer(stagingBuffer);
        cmd.CopyBufferToImage(stagingBuffer, image, GfxShaderStage::Fragment);

        GfxBackend::DestroyBuffer(stagingBuffer);
    }

    // Services the create/update/destroy requests that ImGui queues on ImDrawData::Textures.
    // Must run outside of a RenderPass, because it records buffer->image copies.
    static void _UpdateTextures(GfxCommandBuffer cmd, ImDrawData* drawData)
    {
        if (drawData->Textures == nullptr)
            return;

        bool hasSRGBTarget = SettingsJunkyard::Get().graphics.surfaceSRGB;

        for (ImTextureData* tex : *drawData->Textures) {
            if (tex->Status == ImTextureStatus_OK)
                continue;

            switch (tex->Status) {
            case ImTextureStatus_WantCreate: {
                    ASSERT(tex->Format == ImTextureFormat_RGBA32);
                    ASSERT(tex->TexID == ImTextureID_Invalid);

                    GfxImageDesc imageDesc {
                        .width = uint16(tex->Width),
                        .height = uint16(tex->Height),
                        .format = hasSRGBTarget ? GfxFormat::R8G8B8A8_SRGB : GfxFormat::R8G8B8A8_UNORM,
                        .usageFlags = GfxImageUsageFlags::TransferDst|GfxImageUsageFlags::Sampled
                    };
                    GfxImageHandle image = GfxBackend::CreateImage(imageDesc);
                    _UploadTexture(cmd, tex, image);

                    tex->SetTexID(ImTextureID(uint32(image)));
                    tex->SetStatus(ImTextureStatus_OK);
                }
                break;

            case ImTextureStatus_WantUpdates:
                _UploadTexture(cmd, tex, GfxImageHandle(uint32(tex->TexID)));
                tex->SetStatus(ImTextureStatus_OK);
                break;

            case ImTextureStatus_WantDestroy: {
                    GfxImageHandle image(uint32(tex->TexID));
                    GfxBackend::DestroyImage(image);
                    tex->SetTexID(ImTextureID_Invalid);
                    tex->SetStatus(ImTextureStatus_Destroyed);
                }
                break;

            default:
                break;
            }
        }
    }

    static void _InitializeGraphicsResources(void*)
    {
        // Graphics pipeline
        GfxVertexBufferBindingDesc vertexBufferBindingDesc {
            .binding = 0,
            .stride = sizeof(ImDrawVert),
            .inputRate = GfxVertexInputRate::Vertex
        };

        GfxVertexInputAttributeDesc vertexInputAttDescs[] = {
            {
                .semantic = "POSITION",
                .binding = 0,
                .format = GfxFormat::R32G32_SFLOAT,
                .offset = offsetof(ImDrawVert, pos)
            },
            {
                .semantic = "TEXCOORD",
                .binding = 0,
                .format = GfxFormat::R32G32_SFLOAT,
                .offset = offsetof(ImDrawVert, uv)
            },
            {
                .semantic = "COLOR",
                .binding = 0,
                .format = GfxFormat::R8G8B8A8_UNORM,
                .offset = offsetof(ImDrawVert, col)
            }
        };

        AssetObjPtrScope<GfxShader> shader(gImGui.shader);
        ASSERT(shader);
        const GfxPipelineLayoutDesc::Binding layoutBindings[] {
            {
                .name = "MainTexture",
                .type = GfxDescriptorType::CombinedImageSampler,
                .stagesUsed = GfxShaderStage::Fragment
            }
        };

        const GfxPipelineLayoutDesc::PushConstant pushConstant {
            .name = "Transform",
            .stagesUsed = GfxShaderStage::Vertex,
            .size = sizeof(ImGuiShaderTransform)
        };

        GfxPipelineLayoutDesc layoutDesc {
            .type = GfxPipelineLayoutType::PushDescriptor,
            .numBindings = 1,
            .bindings = layoutBindings,
            .numPushConstants = 1,
            .pushConstants = &pushConstant
        };
        gImGui.pipelineLayout = GfxBackend::CreatePipelineLayout(*shader, layoutDesc);

        GfxGraphicsPipelineDesc pipelineDesc {
            .numVertexInputAttributes = CountOf(vertexInputAttDescs),
            .vertexInputAttributes = vertexInputAttDescs,
            .numVertexBufferBindings = 1,
            .vertexBufferBindings = &vertexBufferBindingDesc,
            .rasterizer = {
                .frontFace = GfxFrontFace::Clockwise
            },
            .blend {
                .numAttachments = 1,
                .attachments = GfxBlendAttachmentDesc::GetAlphaBlending()
            },
            .msaa = {
                .sampleCount = gImGui.msaa,
            },
            .numColorAttachments = 1,
            .colorAttachmentFormats = {GfxBackend::GetSwapchainFormat()}
        };

        gImGui.pipeline = GfxBackend::CreateGraphicsPipeline(*shader, gImGui.pipelineLayout, pipelineDesc);

        ASSERT(gImGui.pipeline.IsValid());

        // Geometry Buffers
        {
            GfxBufferDesc vertexBufferDesc {
                .sizeBytes = IMGUI_VERTICES_POOL_SIZE*sizeof(ImDrawVert),
                .usageFlags = GfxBufferUsageFlags::TransferDst|GfxBufferUsageFlags::Vertex,
                .perFrameUpdates = true
            };
            gImGui.vertexBuffer = GfxBackend::CreateBuffer(vertexBufferDesc);

            GfxBufferDesc indexBufferDesc {
                .sizeBytes = IMGUI_INDICES_POOL_SIZE*sizeof(ImDrawIdx),
                .usageFlags = GfxBufferUsageFlags::TransferDst|GfxBufferUsageFlags::Index,
                .perFrameUpdates = true
            };
            gImGui.indexBuffer = GfxBackend::CreateBuffer(indexBufferDesc);
        }

        // Default Font
        // Since 1.92 the atlas is dynamic: ImGui rasterizes on demand and asks us to create/update
        // the texture through ImTextureData. See _UpdateTextures().
        {
            ImFontConfig fontConfig;
            fontConfig.OversampleH = 3;
            fontConfig.RasterizerMultiply = 1.5f;
            GetIO().Fonts->AddFontFromMemoryCompressedTTF(SEGOE_CUSTOM_compressed_data, SEGOE_CUSTOM_compressed_size,
                                                          16.0f, &fontConfig, nullptr);
        }

        // Sampler
        GfxSamplerDesc samplerDesc {
            .samplerFilter = GfxSamplerFilterMode::Linear,
            .samplerWrap = GfxSamplerWrapMode::ClampToEdge,
        };
        gImGui.sampler = GfxBackend::CreateSampler(samplerDesc);
    }

    static void _SetSetting(const char* key, const char* value)
    {
        const char* dot = Str::FindChar(key, '.');
        ASSERT_MSG(dot, "ImGui settings should come with Control.Name pattern");
    
        char sectionName[64];
        char propertyName[64];

        Str::CopyCount(sectionName, sizeof(sectionName), key, PtrToInt<uint32>((void*)(dot - key)));
        Str::Copy(propertyName, sizeof(propertyName), dot + 1);

        INIFileSection section = gImGui.settingsIni.FindSection(sectionName);
        if (!section.IsValid())
        section = gImGui.settingsIni.NewSection(sectionName);

        INIFileProperty property = section.FindProperty(propertyName);
        if (!property.IsValid())
            property = section.NewProperty(propertyName, value);
        else
            property.SetValue(value);
    
        uint32 hash = Hash::Fnv32Str(key);
        gImGui.settingsCacheTable.AddUnique(hash, property.GetValue());
    }

    static void _GrowGeometryBuffers(GfxBufferHandle* vertexBuffer, uint32* maxVertices,
                                     GfxBufferHandle* indexBuffer, uint32* maxIndices,
                                     uint32 numVertices, uint32 numIndices)
    {
        if (numVertices > *maxVertices) {
            *maxVertices = AlignValue(numVertices, IMGUI_VERTICES_POOL_SIZE);
            GfxBackend::DestroyBuffer(*vertexBuffer);
            GfxBufferDesc vertexBufferDesc {
                .sizeBytes = (*maxVertices)*sizeof(ImDrawVert),
                .usageFlags = GfxBufferUsageFlags::TransferDst|GfxBufferUsageFlags::Vertex,
                .perFrameUpdates = true
            };
            *vertexBuffer = GfxBackend::CreateBuffer(vertexBufferDesc);

            LOG_VERBOSE("ImGui vertex capacity increased to maximum %u vertices", *maxVertices);
        }

        if (numIndices > *maxIndices) {
            *maxIndices = AlignValue(numIndices, IMGUI_INDICES_POOL_SIZE);
            GfxBackend::DestroyBuffer(*indexBuffer);
            GfxBufferDesc indexBufferDesc {
                .sizeBytes = (*maxIndices)*sizeof(ImDrawIdx),
                .usageFlags = GfxBufferUsageFlags::TransferDst|GfxBufferUsageFlags::Index,
                .perFrameUpdates = true
            };
            *indexBuffer = GfxBackend::CreateBuffer(indexBufferDesc);

            LOG_VERBOSE("ImGui index capacity increased to maximum %u indices", *maxIndices);
        }
    }

    // Grows and fills the geometry buffers. Must run outside of a RenderPass
    static void _UploadDrawData(GfxCommandBuffer cmd, ImDrawData* drawData,
                                GfxBufferHandle* vertexBuffer, uint32* maxVertices,
                                GfxBufferHandle* indexBuffer, uint32* maxIndices)
    {
        if (drawData->TotalVtxCount == 0)
            return;

        _GrowGeometryBuffers(vertexBuffer, maxVertices, indexBuffer, maxIndices,
                             drawData->TotalVtxCount, drawData->TotalIdxCount);

        uint32 indexSize = drawData->TotalIdxCount * sizeof(ImDrawIdx);
        uint32 vertexSize = drawData->TotalVtxCount * sizeof(ImDrawVert);

        GfxHelperBufferUpdateScope vertexBufferUpdate(cmd, *vertexBuffer, vertexSize, GfxShaderStage::Vertex);
        GfxHelperBufferUpdateScope indexBufferUpdate(cmd, *indexBuffer, indexSize, GfxShaderStage::Vertex);

        ImDrawVert* vertices = (ImDrawVert*)vertexBufferUpdate.mData;
        ImDrawIdx* indices = (ImDrawIdx*)indexBufferUpdate.mData;

        for (int i = 0; i < drawData->CmdLists.Size; i++) {
            const ImDrawList* cmdList = drawData->CmdLists[i];
            memcpy(vertices, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
            memcpy(indices, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));

            vertices += cmdList->VtxBuffer.Size;
            indices += cmdList->IdxBuffer.Size;
        }
    }

    // Records the actual draw calls. Must run inside a RenderPass targeting `drawData`'s viewport
    static void _RecordDrawCommands(GfxCommandBuffer cmd, ImDrawData* drawData,
                                    GfxBufferHandle vertexBuffer, GfxBufferHandle indexBuffer)
    {
        Float2 displayPos = Float2(drawData->DisplayPos.x, drawData->DisplayPos.y);
        Float2 displaySize = Float2(drawData->DisplaySize.x, drawData->DisplaySize.y);

        // Origin stays at 0: the projection below already folds displayPos in, and with viewports enabled
        // displayPos is the window's desktop position, which must not offset the render target viewport
        GfxViewport viewport {
            .x = 0,
            .y = 0,
            .width = displaySize.x,
            .height = displaySize.y
        };

        uint64 offsets[] = {0};
        cmd.BindPipeline(gImGui.pipeline);
        cmd.SetViewports(0, 1, &viewport);
        cmd.BindVertexBuffers(0, 1, &vertexBuffer, offsets);
        cmd.BindIndexBuffer(indexBuffer, 0, GfxIndexType::Uint16);

        ImGuiShaderTransform transform {
            .projMat = GfxBackend::GetSwapchainTransformMat() * Mat4::OrthoOffCenter(displayPos.x, displayPos.y + displaySize.y,
                                                                                     displayPos.x + displaySize.x, displayPos.y,
                                                                                     -1.0f, 1.0f)
        };
        cmd.PushConstants<ImGuiShaderTransform>(gImGui.pipelineLayout, "Transform", transform);

        GfxImageHandle boundImage;
        uint32 globalVertexOffset = 0;
        uint32 globalIndexOffset = 0;
        for (int i = 0; i < drawData->CmdLists.Size; i++) {
            const ImDrawList* cmdList = drawData->CmdLists[i];

            for (int k = 0; k < cmdList->CmdBuffer.Size; k++) {
                const ImDrawCmd* drawCmd = &cmdList->CmdBuffer[k];

                if (drawCmd->UserCallback) {
                    drawCmd->UserCallback(cmdList, drawCmd);
                }
                else {
                    Float4 clipRect((drawCmd->ClipRect.x - displayPos.x), (drawCmd->ClipRect.y - displayPos.y),
                                    (drawCmd->ClipRect.z - displayPos.x), (drawCmd->ClipRect.w - displayPos.y));

                    if (clipRect.x < 0.0f) { clipRect.x = 0.0f; }
                    if (clipRect.y < 0.0f) { clipRect.y = 0.0f; }
                    if (clipRect.z > displaySize.x) { clipRect.z = displaySize.x; }
                    if (clipRect.w > displaySize.y) { clipRect.w = displaySize.y; }
                    if (clipRect.z <= clipRect.x || clipRect.w <= clipRect.y)
                        continue;

                    RectInt scissor(int(clipRect.x), int(clipRect.y), int(clipRect.z), int(clipRect.w));

                    GfxImageHandle img(uint32(drawCmd->GetTexID()));
                    if (img != boundImage) {
                        GfxBindingDesc bindings[] = {
                            {
                                .name = "MainTexture",
                                .image = img,
                                .sampler = gImGui.sampler
                            }
                        };
                        cmd.PushBindings(gImGui.pipelineLayout, CountOf(bindings), bindings);
                        boundImage = img;
                    }

                    cmd.SetScissors(0, 1, &scissor);
                    cmd.DrawIndexed(drawCmd->ElemCount, 1, drawCmd->IdxOffset + globalIndexOffset, drawCmd->VtxOffset + globalVertexOffset, 0);
                }
            }

            globalIndexOffset += cmdList->IdxBuffer.Size;
            globalVertexOffset += cmdList->VtxBuffer.Size;
        }

        RectInt rc(0, 0, int(displaySize.x), int(displaySize.y));
        cmd.SetScissors(0, 1, &rc);
    }
} // ImGui

bool ImGui::InitializeSubsystem()
{
    const SettingsJunkyard& settings = SettingsJunkyard::Get();
    Engine::HelperInitializeProxyAllocator(&gImGui.alloc, "ImGui");
    Engine::RegisterProxyAllocator(&gImGui.alloc);

    gImGui.runtimeAlloc.Initialize(&gImGui.alloc, IMGUI_RUNTIME_HEAP_SIZE, settings.engine.debugAllocations);
    
    SetAllocatorFunctions(
        [](size_t size, void*)->void* { return Mem::Alloc(size, &gImGui.runtimeAlloc); },
        [](void* ptr, void*) { Mem::Free(ptr, &gImGui.runtimeAlloc); });
    
    gImGui.lastCursor = ImGuiMouseCursor_COUNT;
    gImGui.ctx = CreateContext();
    if (!gImGui.ctx) {
        LOG_ERROR("ImGui: CreateContext failed");
        return false;
    }

    ImGuiIO& conf = GetIO();

    static char iniFilename[64];
    Str::PrintFmt(iniFilename, sizeof(iniFilename), "%s_imgui.ini", App::GetName());
    conf.IniFilename = iniFilename;

    float frameBufferScale = App::GetWindowDPIScale();
    conf.DisplayFramebufferScale = ImVec2(frameBufferScale, frameBufferScale);

    // We service ImTextureData create/update/destroy requests in DrawFrame, which lets ImGui
    // re-rasterize fonts whenever the scale changes instead of stretching a fixed atlas.
    conf.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    // Multi-viewport is only wired up for platforms that can actually make secondary windows
    gImGui.viewportsEnabled = App::IsMultiWindowSupported();
    if (gImGui.viewportsEnabled) {
        conf.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        // Deliberately not claiming ImGuiBackendFlags_HasMouseHoveredViewport: it requires honoring
        // ImGuiViewportFlags_NoInputs (click-through windows), which the App layer cannot do yet.
        // Without that flag ImGui works the hovered viewport out from its own window stack instead
        conf.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports;
    }

    gImGui.maxVertices = IMGUI_VERTICES_POOL_SIZE;
    gImGui.maxIndices = IMGUI_INDICES_POOL_SIZE;

    // Application events
    App::RegisterEventsCallback(_OnEventCallback);

    _SetColorTheme();
    _InitializeSettings();

    if (gImGui.viewportsEnabled) {
        // Secondary viewports are real OS windows: rounded corners and a translucent background
        // would show the desktop through them
        ImGuiStyle& style = GetStyle();
        style.WindowRounding = 0;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;

        _InitializeViewports();
    }

    // Register graphics resources callback so we can continue when the resources are loaded
    ShaderLoadParams shaderParams {
        .compileDesc { 
            .numDefines = 1,
            .defines = {
                {
                    .define = "SRGB_TARGET",
                    .value = String32::Format("%d", settings.graphics.surfaceSRGB ? 1 : 0)
                }
            }
        }
    };
    gImGui.shader = Shader::Load("/shaders/ImGui.hlsl", shaderParams, Engine::RegisterInitializeResources(_InitializeGraphicsResources));

    return true;
}

void ImGui::BeginFrame(float dt)
{
    if (gImGui.ctx == nullptr)
        return;

    ImGuiIO& io = GetIO();
    io.DisplaySize = ImVec2(float(App::GetFramebufferWidth()), float(App::GetFramebufferHeight()));
    GetStyle().FontScaleDpi = App::GetWindowDPIScale();
    io.DeltaTime = dt;
    if (io.DeltaTime == 0) 
        io.DeltaTime = 0.033f;

    for (uint32 i = 0; i < (uint32)InputMouseButton::_Count; i++) {
        if (gImGui.mouseButtonDown[i]) {
            gImGui.mouseButtonDown[i] = false;
            io.MouseDown[i] = true;
        }
        else if (gImGui.mouseButtonUp[i]) {
            gImGui.mouseButtonUp[i] = false;
            io.MouseDown[i] = false;
        }
    }

    io.MouseWheel = gImGui.mouseWheel;
    io.MouseWheelH = gImGui.mouseWheelH;
    gImGui.mouseWheelH = gImGui.mouseWheel = 0;

    // Update OS mouse cursor with the cursor requested by imgui
    ImGuiMouseCursor mouseCursor =  io.MouseDrawCursor ? ImGuiMouseCursor_None : GetMouseCursor();
    if (gImGui.lastCursor != mouseCursor) {
        gImGui.lastCursor = mouseCursor;
        _UpdateCursor();
    }
    
    // Monitor layout can change at any time (hotplug, DPI change), ImGui expects it fresh every frame
    if (gImGui.viewportsEnabled)
        _UpdateMonitors();

    gImGui.frameRendered = false;
    NewFrame();
    ImGuizmo::BeginFrame();
    const ImGuiViewport* mainViewport = GetMainViewport();
    ImGuizmo::SetRect(mainViewport->Pos.x, mainViewport->Pos.y, mainViewport->Size.x, mainViewport->Size.y);
}

bool ImGui::DrawFrame(GfxCommandBuffer cmd, GfxImageHandle colorImage)
{
    if (gImGui.ctx == nullptr) 
        return false;

    ASSERT_MSG(cmd.mIsRecording && !cmd.mIsInRenderPass, "%s must be called while CommandBuffer is recording and not in the RenderPass", __FUNCTION__);

    ImGui::Render();
    gImGui.frameRendered = true;

    ImDrawData* drawData = GetDrawData();

    // Honor pending texture requests before we open the RenderPass.
    // Done before the early-out below, because destroy requests can arrive on frames that draw nothing.
    _UpdateTextures(cmd, drawData);

    if (drawData->CmdLists.Size == 0)
        return false;

    _UploadDrawData(cmd, drawData, &gImGui.vertexBuffer, &gImGui.maxVertices,
                    &gImGui.indexBuffer, &gImGui.maxIndices);

    GPU_PROFILE_ZONE(cmd, "ImGui");

    bool isMSAA = false;
    if (colorImage.IsValid()) {
        GfxMultiSampleCount sampleCount = GfxBackend::GetImageDesc(colorImage).multisampleFlags;
        isMSAA = sampleCount != GfxMultiSampleCount::SampleCount1;
        ASSERT_MSG(sampleCount == gImGui.msaa, "ImGui MSAA does not match the provided render target image sample count");
    }
    else {
        ASSERT_MSG(gImGui.msaa == GfxMultiSampleCount::SampleCount1, "If MSAA is set for ImGui, then you should provide a matching render target image");
    }

    // Begin Drawing to the swapchain 
    // Note: We cannot BeginRenderPass while updating the buffers
    GfxBackendRenderPass pass { 
        .colorAttachments = {{ 
            .image = colorImage,
            .load = true,
            .resolveToSwapchain = isMSAA,
        }},
        .swapchain = colorImage.IsValid() ? GfxSwapchainHandle() : GfxBackend::GetMainSwapchain()
    };
    cmd.BeginRenderPass(pass);
    _RecordDrawCommands(cmd, drawData, gImGui.vertexBuffer, gImGui.indexBuffer);
    cmd.EndRenderPass();
    return true;
}

void ImGui::UpdateViewports()
{
    // Requires Render() to have run this frame, otherwise ImGui asserts on the frame counter
    if (gImGui.ctx == nullptr || !gImGui.viewportsEnabled || !gImGui.frameRendered)
        return;

    UpdatePlatformWindows();

    // Records a command buffer per viewport. The app already submitted its own work by now,
    // so these need a submit of their own before GfxBackend::End() presents everything
    RenderPlatformWindowsDefault();

    if (GetPlatformIO().Viewports.Size > 1)
        GfxBackend::SubmitQueue(GfxQueueType::Graphics);
}

void ImGui::ReleaseSubsystem()
{
    ImGuizmo::Destruct();

    if (gImGui.ctx) {
        if (gImGui.viewportsEnabled)
            DestroyPlatformWindows();

        // Textures are owned by ImGui, we only own the GPU side of them
        for (ImTextureData* tex : GetPlatformIO().Textures) {
            if (tex->TexID != ImTextureID_Invalid) {
                GfxImageHandle image(uint32(tex->TexID));
                GfxBackend::DestroyImage(image);
                tex->SetTexID(ImTextureID_Invalid);
                tex->SetStatus(ImTextureStatus_Destroyed);
            }
        }

        GfxBackend::DestroyBuffer(gImGui.vertexBuffer);
        GfxBackend::DestroyBuffer(gImGui.indexBuffer);
        GfxBackend::DestroyPipeline(gImGui.pipeline);
        GfxBackend::DestroyPipelineLayout(gImGui.pipelineLayout);
        GfxBackend::DestroySampler(gImGui.sampler);
        App::UnregisterEventsCallback(_OnEventCallback);
        DestroyContext(gImGui.ctx);
        gImGui.ctx = nullptr;
    }

    _ReleaseSettings();
    gImGui.runtimeAlloc.Release();
    gImGui.alloc.Release();
}

bool ImGui::IsEnabled()
{
    return gImGui.ctx != nullptr;
}

const char* ImGui::GetSetting(const char* key)
{
    return gImGui.settingsCacheTable.FindAndFetch(Hash::Fnv32Str(key), "");
}

void ImGui::SetSetting(const char* key, bool b)
{
    _SetSetting(key, b ? "1" : "0");
}

void ImGui::SetSetting(const char* key, int i)
{
    char istr[32];
    Str::PrintFmt(istr, sizeof(istr), "%d", i);
    _SetSetting(key, istr);
}

void ImGui::ControlAlphaWithScroll(float* alpha)
{
    gImGui.alphaControl = alpha;
}

void ImGui::SeparatorVertical(float)
{
    SeparatorEx(ImGuiSeparatorFlags_Vertical);
}

void ImGui::SetMSAA(GfxMultiSampleCount sampleCount)
{
    gImGui.msaa = sampleCount;
}

RectFloat ImGui::GetMainViewportRect()
{
    const ImGuiViewport* viewport = GetMainViewport();
    return RectFloat(viewport->Pos.x, viewport->Pos.y,
                     viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
}

ImDrawList* ImGui::BeginFullscreenView(const char* name)
{
    const uint32 flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoBringToFrontOnFocus;

    // Pinned to the main window. SetNextWindowViewport stops it from being pulled out into its own
    // platform window just because it covers the whole client area
    const ImGuiViewport* mainViewport = GetMainViewport();
    ImGui::SetNextWindowSize(mainViewport->Size, 0);
    ImGui::SetNextWindowPos(mainViewport->Pos);
    ImGui::SetNextWindowViewport(mainViewport->ID);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, 0);
    ImGui::PushStyleColor(ImGuiCol_Border, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);

    ImGui::Begin(name, nullptr, flags);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    return drawList;
}

ImVec2 ImGui::ProjectToScreen(Float3 point, const Mat4& worldToClipMat, const RectFloat& viewport)
{
    Float4 pos = Float4(point, 1);
    pos = Mat4::MulFloat4(worldToClipMat, pos);
    if (pos.w <= 0)
        return ImVec2(-1, -1);

    pos = pos * (1/pos.w);

    pos.x = 0.5f*pos.x + 0.5f;
    pos.y = 0.5f*pos.y + 0.5f;

    pos.x *= viewport.Width();
    pos.y *= viewport.Height();
    pos.x += viewport.xmin;
    pos.y += viewport.ymin;

    return ImVec2(pos.x, pos.y);
}