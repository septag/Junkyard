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
#include "../Graphics/RenderViewport.h"

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

struct ImGuiState
{
    MemProxyAllocator alloc;
    MemTlsfAllocator runtimeAlloc;

    ImGuiContext* ctx;

    ImGuiMouseCursor lastCursor;
    
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

    static void _OnEventCallback(const AppEvent& ev, [[maybe_unused]] void* userData)
    {
        ImGuiIO& io = GetIO();
    
        switch (ev.type) {
        case AppEventType::MouseDown: {
                Float2 scale(io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
                io.AddMousePosEvent(ev.mouseX * scale.x, ev.mouseY * scale.y);
                io.AddMouseButtonEvent(int(ev.mouseButton), true);
            }
            break;
        case AppEventType::MouseUp: {
                Float2 scale(io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
                io.AddMousePosEvent(ev.mouseX * scale.x, ev.mouseY * scale.y);
                io.AddMouseButtonEvent(int(ev.mouseButton), false);
            }
            break;
        
        case AppEventType::MouseMove: {
                Float2 scale(io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
                io.AddMousePosEvent(ev.mouseX * scale.x, ev.mouseY * scale.y);
            }
            break;
        
        case AppEventType::MouseEnter:
            break;

        case AppEventType::MouseLeave:
            io.AddMousePosEvent(-M_FLOAT32_MAX, -M_FLOAT32_MAX);
            break;

        case AppEventType::MouseScroll:
            io.AddMouseWheelEvent(ev.scrollX, ev.scrollY);
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
                io.DisplaySize = ImVec2(ev.framebufferWidth, ev.framebufferHeight);
                float frameBufferScale = App::GetWindowDPIScale();
                io.DisplayFramebufferScale = ImVec2(frameBufferScale, frameBufferScale);
            }
            break;
    
        default:
            break;
        }
    }

    // Uploads the whole pixel buffer of `tex` into `image`. GfxBackend has no sub-rectangle copy,
    // so partial (ImTextureStatus_WantUpdates) requests re-upload the full texture. Atlases are
    // small and updates are rare, so this stays cheap.
    static void _UploadTexture(GfxCommandBuffer& cmd, ImTextureData* tex, GfxImageHandle image)
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
    static void _UpdateTextures(GfxCommandBuffer& cmd, ImDrawData* drawData)
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

    static void _GrowGeometryBuffers(uint32 numVertices, uint32 numIndices)
    {
        if (numVertices > gImGui.maxVertices) {
            gImGui.maxVertices = AlignValue(numVertices, IMGUI_VERTICES_POOL_SIZE);
            GfxBackend::DestroyBuffer(gImGui.vertexBuffer);
            GfxBufferDesc vertexBufferDesc {
                .sizeBytes = gImGui.maxVertices*sizeof(ImDrawVert),
                .usageFlags = GfxBufferUsageFlags::TransferDst|GfxBufferUsageFlags::Vertex,
                .perFrameUpdates = true
            };
            gImGui.vertexBuffer = GfxBackend::CreateBuffer(vertexBufferDesc);

            LOG_VERBOSE("ImGui vertex capacity increased to maximum %u vertices", gImGui.maxVertices);
        }

        if (numIndices > gImGui.maxIndices) {
            gImGui.maxIndices = AlignValue(numIndices, IMGUI_INDICES_POOL_SIZE);
            GfxBackend::DestroyBuffer(gImGui.indexBuffer);            
            GfxBufferDesc indexBufferDesc {
                .sizeBytes = gImGui.maxIndices*sizeof(ImDrawIdx),
                .usageFlags = GfxBufferUsageFlags::TransferDst|GfxBufferUsageFlags::Index,
                .perFrameUpdates = true
            };
            gImGui.indexBuffer = GfxBackend::CreateBuffer(indexBufferDesc);

            LOG_VERBOSE("ImGui index capacity increased to maximum %u indices", gImGui.maxIndices);
        }
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
    conf.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    static char iniFilename[64];
    Str::PrintFmt(iniFilename, sizeof(iniFilename), "%s_imgui.ini", App::GetName());
    conf.IniFilename = iniFilename;

    float frameBufferScale = App::GetWindowDPIScale();
    conf.DisplayFramebufferScale = ImVec2(frameBufferScale, frameBufferScale);

    // We service ImTextureData create/update/destroy requests in DrawFrame, which lets ImGui
    // re-rasterize fonts whenever the scale changes instead of stretching a fixed atlas.
    conf.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    gImGui.maxVertices = IMGUI_VERTICES_POOL_SIZE;
    gImGui.maxIndices = IMGUI_INDICES_POOL_SIZE;

    // Application events
    App::RegisterEventsCallback(_OnEventCallback);
    
    _SetColorTheme();
    _InitializeSettings();

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

    // Update OS mouse cursor with the cursor requested by imgui
    ImGuiMouseCursor mouseCursor =  io.MouseDrawCursor ? ImGuiMouseCursor_None : GetMouseCursor();
    if (gImGui.lastCursor != mouseCursor) {
        gImGui.lastCursor = mouseCursor;
        _UpdateCursor();
    }
    
    NewFrame();
    ImGuizmo::BeginFrame();
    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
}

void ImGui::DockSpaceOverMainViewport(ImGuiDockNodeFlags dockspaceFlags)
{
    if (gImGui.ctx == nullptr)
        return;

    ImGui::DockSpaceOverViewport(ImGui::GetID("MainDockSpace"), ImGui::GetMainViewport(), dockspaceFlags);
}

void ImGui::RenderViewport(RenderViewportContext* viewport)
{
    ASSERT(viewport);
    if (!viewport->useImGuiViewport)
        return;

    ImGui::SetNextWindowDockID(ImGui::GetID("MainDockSpace"), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(viewport->name);

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    viewport->requestedWidth = Max<uint16>(uint16(avail.x), 1);
    viewport->requestedHeight = Max<uint16>(uint16(avail.y), 1);
    viewport->focused = ImGui::IsWindowFocused();
    viewport->visible = !ImGui::IsWindowCollapsed();
    viewport->imguiPos = Float2(pos.x, pos.y);
    viewport->imguiSize = Float2(avail.x, avail.y);

    ImGui::Image(ImTextureID(uint64(viewport->colorImage.mId)), avail);
    viewport->hovered = ImGui::IsItemHovered();

    ImGui::End();
    ImGui::PopStyleVar();
}

bool ImGui::CanReceiveMouseInput(const RenderViewportContext& viewport)
{
    if (!viewport.useImGuiViewport)
        return true;

    bool isInMainDockSpace = GImGui->ActiveIdWindow && Str::Compare(GImGui->ActiveIdWindow->Name, "MainDockSpace");
    return viewport.hovered && !ImGuizmo::IsOver() && (!ImGui::IsAnyItemActive() || isInMainDockSpace);
}

bool ImGui::CanReceiveMouseInput(RenderViewportContext* viewport, const AppEvent& ev)
{
    ASSERT(viewport);
    if (!viewport->useImGuiViewport)
        return true;

    InputMouseButton activeButton = InputMouseButton::Right;
    if constexpr (PLATFORM_ANDROID)
        activeButton = InputMouseButton::Left;

    const bool canStartInput = viewport->hovered && !ImGuizmo::IsOver() && !ImGui::IsAnyItemActive();

    switch (ev.type) {
    case AppEventType::MouseDown:
        if (ev.mouseButton != activeButton)
            return false;
        if (canStartInput)
            viewport->inputCaptured = true;
        return canStartInput;

    case AppEventType::MouseMove:
        return viewport->inputCaptured || canStartInput;

    case AppEventType::MouseScroll:
        return canStartInput;

    case AppEventType::MouseUp: {
        if (ev.mouseButton != activeButton)
            return false;
        bool wasCaptured = viewport->inputCaptured;
        viewport->inputCaptured = false;
        return wasCaptured || canStartInput;
    }

    case AppEventType::MouseLeave:
        viewport->inputCaptured = false;
        return false;

    default:
        return canStartInput;
    }
}


bool ImGui::DrawFrame(GfxCommandBuffer& cmd, GfxImageHandle colorImage)
{
    if (gImGui.ctx == nullptr) 
        return false;

    ASSERT_MSG(cmd.mIsRecording && !cmd.mIsInRenderPass, "%s must be called while CommandBuffer is recording and not in the RenderPass", __FUNCTION__);

    ImGui::Render();

    ImDrawData* drawData = GetDrawData();

    // Honor pending texture requests before we open the RenderPass.
    // Done before the early-out below, because destroy requests can arrive on frames that draw nothing.
    _UpdateTextures(cmd, drawData);

    if (drawData->CmdLists.Size == 0)
        return false;

    // Fill the buffers
    if (drawData->TotalVtxCount) {
        _GrowGeometryBuffers(drawData->TotalVtxCount, drawData->TotalIdxCount);

        uint32 indexSize = drawData->TotalIdxCount * sizeof(ImDrawIdx);
        uint32 vertexSize = drawData->TotalVtxCount * sizeof(ImDrawVert);

        GfxHelperBufferUpdateScope vertexBufferUpdate(cmd, gImGui.vertexBuffer, vertexSize, GfxShaderStage::Vertex);
        GfxHelperBufferUpdateScope indexBufferUpdate(cmd, gImGui.indexBuffer, indexSize, GfxShaderStage::Vertex);

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
        .swapchain = !colorImage.IsValid()
    };
    cmd.BeginRenderPass(pass);

    // Draw
    Float2 displayPos = Float2(drawData->DisplayPos.x, drawData->DisplayPos.y);
    Float2 displaySize = Float2(drawData->DisplaySize.x, drawData->DisplaySize.y);
    GfxViewport viewport {
        .x = displayPos.x,
        .y = displayPos.y,
        .width = displaySize.x,
        .height = displaySize.y
    };



    uint64 offsets[] = {0};
    cmd.BindPipeline(gImGui.pipeline);
    cmd.SetViewports(0, 1, &viewport);
    cmd.BindVertexBuffers(0, 1, &gImGui.vertexBuffer, offsets);
    cmd.BindIndexBuffer(gImGui.indexBuffer, 0, GfxIndexType::Uint16);

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

    cmd.EndRenderPass();
    return true;
}

void ImGui::ReleaseSubsystem()
{
    ImGuizmo::Destruct();

    if (gImGui.ctx) {
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

ImDrawList* ImGui::BeginFullscreenView(const char* name)
{
    ImGuiIO& io = GetIO();
    const uint32 flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::SetNextWindowSize(io.DisplaySize, 0);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
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