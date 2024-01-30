#ifdef BUILD_UNITY

// Core library
#include "Core/Base.cpp"
#include "Core/Allocators.cpp"
#include "Core/Buffers.cpp"
#include "Core/System.cpp"
#include "Core/StringUtil.cpp"
#include "Core/Log.cpp"
#include "Core/Hash.cpp"
#include "Core/Debug.cpp"
#include "Core/Jobs.cpp"
#include "Core/JsonParser.cpp"
#include "Core/Settings.cpp"
#include "Core/MathAll.cpp"
#include "Core/IniParser.cpp"
#include "Core/TracyHelper.cpp"

// Common
#include "Common/JunkyardSettings.cpp"
#include "Common/Application.cpp"
#include "Common/VirtualFS.cpp"
#include "Common/RemoteServices.cpp"
#include "Common/Camera.cpp"

// Assets
#include "Assets/AssetManager.cpp"
#include "Assets/Image.cpp"
#include "Assets/Model.cpp"
#include "Assets/Shader.cpp"

// DebugTools
#include "DebugTools/BudgetViewer.cpp"
#include "DebugTools/DebugDraw.cpp"
#include "DebugTools/FrameInfoHud.cpp"

// Graphics
#include "Graphics/Graphics.cpp"

// Tool
#include "Tool/ShaderCompiler.cpp"
#include "Tool/ImageEncoder.cpp"
#include "Tool/MeshOptimizer.cpp"
#include "Tool/Console.cpp"

// Graphics/ImGui
#include "External/imgui/imgui.cpp"
#include "External/imgui/imgui_draw.cpp"
#include "External/imgui/imgui_tables.cpp"
#include "External/imgui/imgui_widgets.cpp"
#include "ImGui/ImGuiWrapper.cpp"
#include "ImGui/ImGuizmo.cpp"

#include "Engine.cpp"

#endif // BUILD_UNITY
