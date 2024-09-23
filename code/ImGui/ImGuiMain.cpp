#include "ImGuiMain.h"

#include <stdarg.h>

#include "CousineFont.h"

#include "../External/imgui/imgui.h"
#include "../External/imgui/imgui_internal.h"

#include "../Core/StringUtil.h"
#include "../Core/Hash.h"
#include "../Core/Settings.h"
#include "../Core/Log.h"
#include "../Core/TracyHelper.h"
#include "../Core/MathAll.h"
#include "../Core/IniParser.h"

#include "../Assets/Image.h"
#include "../Assets/Shader.h"
#include "../Assets/AssetManager.h"

#include "../Common/Application.h"
#include "../Common/VirtualFS.h"
#include "../Common/JunkyardSettings.h"

#include "../Engine.h"

// Extra modules
#include "ImGuizmo.h"

namespace _limits 
{
    static constexpr uint32 IMGUI_MAX_VERTICES = 30000;
    static constexpr uint32 IMGUI_MAX_INDICES =  IMGUI_MAX_VERTICES*3; 
    static constexpr size_t IMGUI_RUNTIME_HEAP_SIZE = 2*SIZE_MB;
}

enum ImGuiDescriptorSet : uint32
{
    IMGUI_DESCRIPTORSET_FONT_IMAGE = 0,
    IMGUI_DESCRIPTORSET_NO_IMAGE,
    _IMGUI_DESCRIPTORSET_COUNT
};

struct ImGuiState
{
    MemTlsfAllocator runtimeHeap;

    ImGuiContext* ctx;

    bool mouseButtonDown[(uint32)InputMouseButton::_Count];
    bool mouseButtonUp[(uint32)InputMouseButton::_Count];
    float mouseWheelH;
    float mouseWheel;
    bool keysDown[(uint32)InputKeycode::_Count];
    StaticArray<ImWchar, 128> charInput;
    ImGuiMouseCursor lastCursor;
    
    ImDrawVert*  vertices;
    uint16*      indices;
    GfxBufferHandle    vertexBuffer;
    GfxBufferHandle    indexBuffer;
    GfxDescriptorSetLayoutHandle dsLayout;
    GfxPipelineHandle  pipeline;
    GfxImageHandle     fontImage;
    AssetHandleShader imguiShader;
    size_t       initHeapStart;
    size_t       initHeapSize;
    uint32       lastFrameVertices;
    uint32       lastFrameIndices;
    float*       alphaControl;      // alpha value that will be modified by mouse-wheel + ALT

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
        gImGui.settingsCacheTable.SetAllocator(&gImGui.runtimeHeap);
        gImGui.settingsCacheTable.Reserve(256);

        // Load extra control settings
        {
            MemTempAllocator tmpAlloc;
            char iniFilename[64];
            strPrintFmt(iniFilename, sizeof(iniFilename), "%s_imgui_controls.ini", App::GetName());
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
            strPrintFmt(iniFilename, sizeof(iniFilename), "%s_imgui_controls.ini", App::GetName());
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
        style.Colors[ImGuiCol_TabActive]              = ImVec4(0.80f, 0.47f, 0.00f, 0.59f);
        style.Colors[ImGuiCol_TabUnfocused]           = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
        style.Colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        style.Colors[ImGuiCol_PlotLines]              = ImVec4(0.86f, 0.86f, 0.86f, 1.00f);
        style.Colors[ImGuiCol_PlotLinesHovered]       = ImVec4(0.80f, 0.47f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_PlotHistogram]          = ImVec4(0.80f, 0.47f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.89f, 0.62f, 1.00f);
        style.Colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.80f, 0.47f, 0.00f, 0.25f);
        style.Colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 0.86f, 0.00f, 0.86f);
        style.Colors[ImGuiCol_NavHighlight]           = ImVec4(0.80f, 0.47f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.71f);
        style.Colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        style.Colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
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
        static_assert(ImGuiMouseCursor_NotAllowed == static_cast<ImGuiMouseCursor>(AppMouseCursor::NotAllowed));
    
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
                io.MousePos = ImVec2(ev.mouseX * scale.x, ev.mouseY * scale.y);
                gImGui.mouseButtonDown[uint32(ev.mouseButton)] = true;
            }
            break;
        case AppEventType::MouseUp: {
                Float2 scale(io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
                io.MousePos = ImVec2(ev.mouseX * scale.x, ev.mouseY * scale.y);
                gImGui.mouseButtonUp[uint32(ev.mouseButton)] = true;
            }
            break;
        
        case AppEventType::MouseMove: {
                Float2 scale(io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
                io.MousePos = ImVec2(ev.mouseX * scale.x, ev.mouseY * scale.y);
            }
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
                *gImGui.alphaControl = Clamp(*gImGui.alphaControl + mathSign(ev.scrollY)*0.2f, 0.1f, 1.0f);
            
            break;
        
        case AppEventType::KeyDown:
            gImGui.keysDown[(uint32)ev.keycode] = true;
            if (ev.keycode == InputKeycode::RightShift || ev.keycode == InputKeycode::LeftShift)
                io.KeyShift = true;
            if (ev.keycode == InputKeycode::RightControl || ev.keycode == InputKeycode::LeftControl)
                io.KeyCtrl = true;
            if (ev.keycode == InputKeycode::RightAlt || ev.keycode == InputKeycode::LeftAlt)
                io.KeyAlt = true;
            if (ev.keycode == InputKeycode::RightSuper || ev.keycode == InputKeycode::LeftSuper)
                io.KeySuper = true;
            break;
        
        case AppEventType::KeyUp:
            gImGui.keysDown[(uint32)ev.keycode] = false;
            if (ev.keycode == InputKeycode::RightShift || ev.keycode == InputKeycode::LeftShift)
                io.KeyShift = false;
            if (ev.keycode == InputKeycode::RightControl || ev.keycode == InputKeycode::LeftControl)
                io.KeyCtrl = false;
            if (ev.keycode == InputKeycode::RightAlt || ev.keycode == InputKeycode::LeftAlt)
                io.KeyAlt = false;
            if (ev.keycode == InputKeycode::RightSuper || ev.keycode == InputKeycode::LeftSuper)
                io.KeySuper = false;
            break;
        
        case AppEventType::Char:
            gImGui.charInput.Push((ImWchar)ev.charcode);
            break;
        
        case AppEventType::UpdateCursor:
            _UpdateCursor();
            break;
        
        case AppEventType::Resized: {
                io.DisplaySize = ImVec2(ev.framebufferWidth, ev.framebufferHeight);
                float frameBufferScale = App::GetDisplayInfo().dpiScale;
                io.DisplayFramebufferScale = ImVec2(frameBufferScale, frameBufferScale);
            }
            break;
    
        default:
            break;
        }
    }

    static void _InitializeGraphicsResources(void*)
    {
        // Graphics Objects
        const GfxDescriptorSetLayoutBinding dsetBindings[] = {
            {
                .name = "MainTexture",
                .type = GfxDescriptorType::CombinedImageSampler,
                .stages = GfxShaderStage::Fragment
            }
        };

        GfxPushConstantDesc pushConstant = {
            .name = "Transform",
            .stages = GfxShaderStage::Vertex,
            .range = {0, sizeof(Mat4)}
        };

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

        AssetObjPtrScope<GfxShader> shader(gImGui.imguiShader);
        ASSERT(shader);

        gImGui.dsLayout = gfxCreateDescriptorSetLayout(*shader, dsetBindings, CountOf(dsetBindings), GfxDescriptorSetLayoutFlags::PushDescriptor);

        gImGui.pipeline = gfxCreatePipeline(GfxPipelineDesc {
            .shader = shader,
            .inputAssemblyTopology = GfxPrimitiveTopology::TriangleList,
            .numDescriptorSetLayouts = 1,
            .descriptorSetLayouts = &gImGui.dsLayout,
            .numPushConstants = 1,
            .pushConstants = &pushConstant,
            .numVertexInputAttributes = CountOf(vertexInputAttDescs),
            .vertexInputAttributes = vertexInputAttDescs,
            .numVertexBufferBindings = 1,
            .vertexBufferBindings = &vertexBufferBindingDesc,
            .rasterizer = GfxRasterizerDesc {
                .cullMode = GfxCullModeFlags::None,
                .frontFace = GfxFrontFace::Clockwise
            },
            .blend = {
                .numAttachments = 1,
                .attachments = GfxBlendAttachmentDesc::GetAlphaBlending()
            }
        });
        ASSERT(gImGui.pipeline.IsValid());
    }

    static void _SetSetting(const char* key, const char* value)
    {
        const char* dot = strFindChar(key, '.');
        ASSERT_MSG(dot, "ImGui settings should come with Control.Name pattern");
    
        char sectionName[64];
        char propertyName[64];

        strCopyCount(sectionName, sizeof(sectionName), key, PtrToInt<uint32>((void*)(dot - key)));
        strCopy(propertyName, sizeof(propertyName), dot + 1);

        INIFileSection section = gImGui.settingsIni.FindSection(sectionName);
        if (!section.IsValid())
        section = gImGui.settingsIni.NewSection(sectionName);

        INIFileProperty property = section.FindProperty(propertyName);
        if (!property.IsValid())
        property = section.NewProperty(propertyName, value);
        else
        property.SetValue(value);
    
        uint32 hash = Hash::Fnv32Str(key);
        gImGui.settingsCacheTable.AddIfNotFound(hash, property.GetValue());
    }

} // ImGui

bool ImGui::Initialize()
{
    MemBumpAllocatorBase* initHeap = Engine::GetInitHeap();
    gImGui.initHeapStart = initHeap->GetOffset();

    {
        size_t poolSize = MemTlsfAllocator::GetMemoryRequirement(_limits::IMGUI_RUNTIME_HEAP_SIZE);
        gImGui.runtimeHeap.Initialize(_limits::IMGUI_RUNTIME_HEAP_SIZE, Mem::Alloc(poolSize, initHeap), poolSize,
                                      SettingsJunkyard::Get().engine.debugAllocations);
    }
    
    SetAllocatorFunctions(
        [](size_t size, void*)->void* { return Mem::Alloc(size, &gImGui.runtimeHeap); },
        [](void* ptr, void*) { Mem::Free(ptr, &gImGui.runtimeHeap); });
    
    gImGui.lastCursor = ImGuiMouseCursor_COUNT;
    gImGui.ctx = CreateContext();
    if (!gImGui.ctx) {
        LOG_ERROR("ImGui: CreateContext failed");
        return false;
    }

    ImGuiIO& conf = GetIO();

    static char iniFilename[64];
    strPrintFmt(iniFilename, sizeof(iniFilename), "%s_imgui.ini", App::GetName());
    conf.IniFilename = iniFilename;

    float frameBufferScale = App::GetDisplayInfo().dpiScale;
    conf.DisplayFramebufferScale = ImVec2(frameBufferScale, frameBufferScale);

    conf.KeyMap[ImGuiKey_Tab]           = static_cast<int>(InputKeycode::Tab);
    conf.KeyMap[ImGuiKey_LeftArrow]     = static_cast<int>(InputKeycode::Left);
    conf.KeyMap[ImGuiKey_RightArrow]    = static_cast<int>(InputKeycode::Right);
    conf.KeyMap[ImGuiKey_UpArrow]       = static_cast<int>(InputKeycode::Up);
    conf.KeyMap[ImGuiKey_DownArrow]     = static_cast<int>(InputKeycode::Down);
    conf.KeyMap[ImGuiKey_PageUp]        = static_cast<int>(InputKeycode::PageUp);
    conf.KeyMap[ImGuiKey_PageDown]      = static_cast<int>(InputKeycode::PageDown);
    conf.KeyMap[ImGuiKey_Home]          = static_cast<int>(InputKeycode::Home);
    conf.KeyMap[ImGuiKey_End]           = static_cast<int>(InputKeycode::End);
    conf.KeyMap[ImGuiKey_Insert]        = static_cast<int>(InputKeycode::Insert);
    conf.KeyMap[ImGuiKey_Delete]        = static_cast<int>(InputKeycode::Delete);
    conf.KeyMap[ImGuiKey_Backspace]     = static_cast<int>(InputKeycode::Backspace);
    conf.KeyMap[ImGuiKey_Space]         = static_cast<int>(InputKeycode::Space);
    conf.KeyMap[ImGuiKey_Enter]         = static_cast<int>(InputKeycode::Enter);
    conf.KeyMap[ImGuiKey_KeyPadEnter]   = static_cast<int>(InputKeycode::KPEnter);
    conf.KeyMap[ImGuiKey_Escape]        = static_cast<int>(InputKeycode::Escape);
    conf.KeyMap[ImGuiKey_A]             = static_cast<int>(InputKeycode::A);
    conf.KeyMap[ImGuiKey_C]             = static_cast<int>(InputKeycode::C);
    conf.KeyMap[ImGuiKey_V]             = static_cast<int>(InputKeycode::V);
    conf.KeyMap[ImGuiKey_X]             = static_cast<int>(InputKeycode::X);
    conf.KeyMap[ImGuiKey_Y]             = static_cast<int>(InputKeycode::Y);
    conf.KeyMap[ImGuiKey_Z]             = static_cast<int>(InputKeycode::Z);

    gImGui.vertices = Mem::AllocTyped<ImDrawVert>(_limits::IMGUI_MAX_VERTICES, initHeap);
    gImGui.indices = Mem::AllocTyped<uint16>(_limits::IMGUI_MAX_INDICES, initHeap);

    gImGui.vertexBuffer = gfxCreateBuffer({
        .size = _limits::IMGUI_MAX_VERTICES*sizeof(ImDrawVert),
        .type = GfxBufferType::Vertex,
        .usage = GfxBufferUsage::Stream,
    });

    gImGui.indexBuffer = gfxCreateBuffer({
        .size = _limits::IMGUI_MAX_INDICES*sizeof(uint16),
        .type = GfxBufferType::Index,
        .usage = GfxBufferUsage::Stream
    });

    if (!gImGui.vertexBuffer.IsValid() || !gImGui.indexBuffer.IsValid()) {
        LOG_ERROR("ImGui: Creating gpu buffers failed");
        return false;
    }
    
    // Application events
    App::RegisterEventsCallback(_OnEventCallback);
    
    // Default Font
    {
        ImFontConfig fontConfig;
        fontConfig.OversampleH = 3;
        fontConfig.RasterizerMultiply = 1.5f;
        conf.Fonts->AddFontFromMemoryCompressedTTF(kCousineFont_compressed_data, kCousineFont_compressed_size, 
                                                   14.0f, &fontConfig, nullptr);

        uint8* fontPixels;
        int fontWidth, fontHeight, fontBpp;
        conf.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight, &fontBpp);
    
        gImGui.fontImage = gfxCreateImage(GfxImageDesc {
            .width = static_cast<uint32>(fontWidth),
            .height = static_cast<uint32>(fontHeight),
            .format = GfxFormat::R8G8B8A8_UNORM,
            .samplerFilter = GfxSamplerFilterMode::Linear,
            .samplerWrap = GfxSamplerWrapMode::ClampToEdge,
            .sampled = true,
            .size = static_cast<uint32>(fontWidth)*static_cast<uint32>(fontHeight)*4,
            .content = fontPixels 
        });
        conf.Fonts->SetTexID( reinterpret_cast<ImTextureID>((uintptr_t)uint32(gImGui.fontImage)));
    }

    _SetColorTheme();
    _InitializeSettings();

    gImGui.initHeapSize = initHeap->GetOffset() - gImGui.initHeapStart;

    // Register graphics resources callback so we can continue when the resources are loaded
    gImGui.imguiShader = Asset::LoadShader("/shaders/ImGui.hlsl", ShaderLoadParams(),
                                           Engine::RegisterInitializeResources(_InitializeGraphicsResources));

    
    return true;
}

void ImGui::BeginFrame(float dt)
{
    if (gImGui.ctx == nullptr)
        return;

    ImGuiIO& io = GetIO();
    io.DisplaySize = ImVec2(float(App::GetFramebufferWidth()), float(App::GetFramebufferHeight()));
    io.FontGlobalScale = App::GetDisplayInfo().dpiScale;
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

    memcpy(io.KeysDown, gImGui.keysDown, sizeof(io.KeysDown));
    memset(gImGui.keysDown, 0x0, sizeof(gImGui.keysDown));

    for (uint32 i = 0; i < gImGui.charInput.Count(); i++)
        io.AddInputCharacter(gImGui.charInput[i]);
    gImGui.charInput.Clear();


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

bool ImGui::DrawFrame()
{
    if (gImGui.ctx == nullptr) 
        return false;

    PROFILE_ZONE();
    Render();

    ImDrawData* drawData = GetDrawData();
    if (drawData->CmdListsCount == 0)
        return false;

    ASSERT_MSG(drawData->CmdListsCount, "Must call imguiRender and check if something is actually being rendered");
    if (drawData->CmdListsCount == 0)
        return false;

    // Fill the buffers
    uint32 numVerts = 0;
    uint32 numIndices = 0;
    ImDrawVert* vertices = gImGui.vertices;
    uint16* indices = gImGui.indices;

    for (int drawListIdx = 0; drawListIdx < drawData->CmdListsCount; drawListIdx++) {
        const ImDrawList* dlist = drawData->CmdLists[drawListIdx];
        const uint32 dlistNumVerts = static_cast<uint32>(dlist->VtxBuffer.size());
        const uint32 dlistNumIndices = static_cast<uint32>(dlist->IdxBuffer.size());
        
        if ((numVerts + dlistNumVerts) > _limits::IMGUI_MAX_VERTICES) {
            LOG_WARNING("ImGui: maximum vertex count of '%u' exceeded", _limits::IMGUI_MAX_VERTICES);
            numVerts = _limits::IMGUI_MAX_VERTICES - dlistNumVerts;
            ASSERT(0);
        }

        if ((numIndices + dlistNumIndices) > _limits::IMGUI_MAX_INDICES) {
            LOG_WARNING("ImGui: maximum index count of '%u' exceeded", _limits::IMGUI_MAX_INDICES);
            numIndices = _limits::IMGUI_MAX_INDICES - dlistNumIndices;
            ASSERT(0);
        }

        memcpy(&vertices[numVerts], dlist->VtxBuffer.Data, dlistNumVerts * sizeof(ImDrawVert));

        auto srcIndexPtr = (const ImDrawIdx*)dlist->IdxBuffer.Data;
        ASSERT(numVerts <= UINT16_MAX);
        const uint16 baseVertexIdx = static_cast<uint16>(numVerts);
        for (uint32 i = 0; i < dlistNumIndices; i++) 
            indices[numIndices++] = srcIndexPtr[i] + baseVertexIdx;
        numVerts += dlistNumVerts;
    }


    gfxCmdUpdateBuffer(gImGui.vertexBuffer, vertices, numVerts * sizeof(ImDrawVert));
    gfxCmdUpdateBuffer(gImGui.indexBuffer, indices, numIndices * sizeof(uint16));
    gImGui.lastFrameVertices = numVerts;
    gImGui.lastFrameIndices = numIndices;

    // Draw
    Float2 fbPos = Float2(drawData->DisplayPos.x, drawData->DisplayPos.y);
    Float2 displaySize = Float2(drawData->DisplaySize.x, drawData->DisplaySize.y);
    GfxViewport viewport {
        .x = fbPos.x,
        .y = fbPos.y,
        .width = displaySize.x,
        .height = displaySize.y
    };

    Mat4 projMat = gfxGetClipspaceTransform() * mat4OrthoOffCenter(fbPos.x, fbPos.y + displaySize.y, 
                                                                   fbPos.x + displaySize.x, fbPos.y,
                                                                   -1.0f, 1.0f);
    

    uint64 offsets[] = {0};
    gfxCmdBindVertexBuffers(0, 1, &gImGui.vertexBuffer, offsets);
    gfxCmdBindIndexBuffer(gImGui.indexBuffer, 0, GfxIndexType::Uint16);

    gfxCmdBindPipeline(gImGui.pipeline);
    gfxCmdSetViewports(0, 1, &viewport, true);
    gfxCmdPushConstants(gImGui.pipeline, GfxShaderStage::Vertex, &projMat, sizeof(projMat));

    GfxImageHandle prevImg {};
    uint32 baseElem = 0;
    for (int drawListIdx = 0; drawListIdx < drawData->CmdListsCount; drawListIdx++) {
        const ImDrawList* dlist = drawData->CmdLists[drawListIdx];
        for (const ImDrawCmd* drawCmd = (const ImDrawCmd*)dlist->CmdBuffer.Data;
             drawCmd != (const ImDrawCmd*)dlist->CmdBuffer.Data + dlist->CmdBuffer.Size; ++drawCmd)
        {
            if (drawCmd->UserCallback) {
                drawCmd->UserCallback(dlist, drawCmd);
                continue;
            }

            ASSERT_MSG(drawCmd->UserCallback != ImDrawCallback_ResetRenderState, "Not implemented");

            Float4 clipRect((drawCmd->ClipRect.x - fbPos.x), (drawCmd->ClipRect.y - fbPos.y),
                            (drawCmd->ClipRect.z - fbPos.x), (drawCmd->ClipRect.w - fbPos.y));
            if (clipRect.x < displaySize.x && clipRect.y < displaySize.y && clipRect.z >= 0.0f && clipRect.w >= 0.0f) {
                Recti scissor(int(clipRect.x), int(clipRect.y), int(clipRect.z), int(clipRect.w));
                GfxImageHandle img(PtrToInt<uint32>(drawCmd->TextureId));
                if (prevImg != img) {
                    GfxDescriptorBindingDesc descriptorBindings[] = {
                        {
                            .name = "MainTexture",
                            .type = GfxDescriptorType::CombinedImageSampler,
                            .image = img
                        }
                    };
                    gfxCmdPushDescriptorSet(gImGui.pipeline, GfxPipelineBindPoint::Graphics, 0, CountOf(descriptorBindings), descriptorBindings);

                    prevImg = img;
                }

                gfxCmdSetScissors(0, 1, &scissor, true);
                gfxCmdDrawIndexed(drawCmd->ElemCount, 1, baseElem, 0, 0);
            }

            baseElem += drawCmd->ElemCount;
        }
    }

    return true;
}

void ImGui::Release()
{
    if (gImGui.ctx) {
        gfxWaitForIdle();   // TODO: remove this

        // assetUnload(gImGui.imguiShader);

        gfxDestroyBuffer(gImGui.vertexBuffer);
        gfxDestroyBuffer(gImGui.indexBuffer);
        gfxDestroyPipeline(gImGui.pipeline);
        gfxDestroyDescriptorSetLayout(gImGui.dsLayout);
        gfxDestroyImage(gImGui.fontImage);
        App::UnregisterEventsCallback(_OnEventCallback);
        DestroyContext(gImGui.ctx);
        gImGui.ctx = nullptr;
    }

    _ReleaseSettings();
    gImGui.runtimeHeap.Release();
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
    strPrintFmt(istr, sizeof(istr), "%d", i);
    _SetSetting(key, istr);
}

void ImGui::GetBudgetStats(ImGuiBudgetStats* stats)
{
    stats->initHeapStart = gImGui.initHeapStart;
    stats->initHeapSize = gImGui.initHeapSize;
    stats->runtimeHeapSize = gImGui.runtimeHeap.GetAllocatedSize();
    stats->runtimeHeapMax = _limits::IMGUI_RUNTIME_HEAP_SIZE;
    stats->maxVertices = _limits::IMGUI_MAX_VERTICES;
    stats->maxIndices = _limits::IMGUI_MAX_INDICES;
    stats->lastFrameVertices = gImGui.lastFrameVertices;
    stats->lastFrameIndices = gImGui.lastFrameIndices;
    stats->runtimeHeap = &gImGui.runtimeHeap;
}

void ImGui::ControlAlphaWithScroll(float* alpha)
{
    gImGui.alphaControl = alpha;
}

void ImGui::SeparatorVertical(float)
{
    SeparatorEx(ImGuiSeparatorFlags_Vertical);
}

