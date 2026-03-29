#include "../UnityBuild.inl"

#include "../Core/Settings.h"
#include "../Core/Log.h"
#include "../Core/TracyHelper.h"
#include "../Core/Jobs.h"
#include "../Core/MathAll.h"
#include "../Core/System.h"

#include "../Common/Application.h"
#include "../Common/JunkyardSettings.h"

#include "../Assets/AssetManager.h"
#include "../Assets/Model.h"
#include "../Assets/Shader.h"

#include "../Graphics/GfxBackend.h"
#include "../Graphics/Geometry.h"
#include "../Renderer/Render.h"

void GatherGeometries(const GeometryData& geo, RView& view, const Mat4& localToWorldMat)
{
    RGeometryChunk* chunk = view.NewGeometryChunk();
    chunk->localToWorldMat = localToWorldMat;
    chunk->posVertexBuffer = geo.vertexBuffers[0];
    chunk->lightingVertexBuffer = geo.vertexBuffers[1];
    chunk->indexBuffer = geo.indexBuffer;

    RGeometrySubChunk subchunk = {
        .startIndex = 0,
        .numIndices = geo.numIndices,
        .baseColorImg = Image::GetWhite1x1()
    };
    chunk->AddSubChunk(subchunk);
}

struct AppImpl final : AppCallbacks
{
    bool Initialize() override
    {
        return true;
    }

    void Cleanup() override
    {
    }

    void Update(float dt) override
    {
    }

    void OnEvent(const AppEvent& ev) override
    {
    }
};

int Main(int argc, char* argv[])
{
    SettingsJunkyard initSettings {
        .app = {
            .appName = "TestCollision"
        },
        .graphics = {
            .surfaceSRGB = true
        }
    };
    SettingsJunkyard::Initialize(initSettings);

    Settings::InitializeFromINI("TestCollision.ini");
    Settings::InitializeFromCommandLine(argc, argv);

    static AppImpl impl;
    App::Run(AppDesc { 
        .callbacks = &impl, 
        .windowTitle = "Junkyard: Collision Test"
    });

    Settings::SaveToINI("TestCollision.ini");
    Settings::Release();
    return 0;
}

