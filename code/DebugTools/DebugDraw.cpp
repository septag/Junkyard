#include "DebugDraw.h"

#include "../Graphics/Graphics.h"

#include "../Engine.h"
#include "../Assets/Shader.h"
#include "../Assets/AssetManager.h"

#include "../Core/Log.h"
#include "../Core/MathVector.h"

#include "../Common/Camera.h"

static constexpr uint32 kDebugDrawMaxVerts = 32*1000;

struct DebugDrawContext
{
    GfxPipeline pipeline;
    AssetHandleShader shaderAsset;
    GfxBuffer vertexBuffer;

    struct Vertex
    {
        Float3 pos;
        Color color;
    };
};

static DebugDrawContext gDebugDraw;

namespace DebugDraw
{
    static void _InitializeGraphicsResources(void*)
    {
        // Wireframe 
        GfxVertexBufferBindingDesc vertexBufferBindingDesc = {
            .binding = 0,
            .stride = sizeof(DebugDrawContext::Vertex),
            .inputRate = GfxVertexInputRate::Vertex
        };

        GfxVertexInputAttributeDesc vertexInputAttDescs[] = {
            {
                .semantic = "POSITION",
                .binding = 0,
                .format  = GfxFormat::R32G32B32_SFLOAT,
                .offset = offsetof(DebugDrawContext::Vertex, pos)
            },
            {
                .semantic = "COLOR",
                .binding = 0,
                .format = GfxFormat::R8G8B8A8_UNORM,
                .offset = offsetof(DebugDrawContext::Vertex, color)
            }
        };

        GfxPushConstantDesc pushConstant = GfxPushConstantDesc {
            .name = "Transform",
            .stages = GfxShaderStage::Vertex,
            .range = { 0, sizeof(Mat4) }
        };

        gDebugDraw.pipeline = gfxCreatePipeline(GfxPipelineDesc {
            .shader = Asset::GetShader(gDebugDraw.shaderAsset),
            .inputAssemblyTopology = GfxPrimitiveTopology::LineList,
            .numPushConstants = 1,
            .pushConstants = &pushConstant,
            .numVertexInputAttributes = CountOf(vertexInputAttDescs),
            .vertexInputAttributes = vertexInputAttDescs,
            .numVertexBufferBindings = 1,
            .vertexBufferBindings = &vertexBufferBindingDesc,
            .blend = {
                .numAttachments = 1,
                .attachments = GfxBlendAttachmentDesc::GetDefault()
            },
            .depthStencil = GfxDepthStencilDesc {
                .depthTestEnable = true,
                .depthWriteEnable = false,
                .depthCompareOp = GfxCompareOp::Less
            }
        });

        ASSERT(gDebugDraw.pipeline.IsValid());
    }
} // DebugDraw

void DebugDraw::DrawGroundGrid(const Camera& cam, float viewWidth, float viewHeight, const DebugDrawGridProperties& props)
{
    Color color = props.lineColor;
    Color boldColor = props.boldLineColor;

    float spacing = mathCeil(Max(props.spacing, 0.0001f));
    float boldSpacing = props.boldSpacing;
    ASSERT(boldSpacing >= spacing);
    ASSERT(props.distance > 0);

    CameraFrustumPoints frustumPts = cam.GetFrustumPoints(viewWidth, viewHeight, -props.distance, props.distance);
    Mat4 viewProjMat = cam.GetPerspectiveMat(viewWidth, viewHeight) * cam.GetViewMat();
    AABB bb = AABB_EMPTY;

    // extrude near plane
    Float3 nearPlaneN = planeNormal(frustumPts[0], frustumPts[1], frustumPts[2]);
    for (uint32 i = 0; i < frustumPts.Count(); i++) {
        if (i < 4) {
            Float3 offsetPt = frustumPts[i] - nearPlaneN*spacing;
            AABBAddPoint(&bb, Float3(offsetPt.x, offsetPt.y, 0));
        } 
        else {
            AABBAddPoint(&bb, Float3(frustumPts[i].x, frustumPts[i].y, 0));
        }
    }

    int nspace = int(spacing);
    AABB snapbox = AABB(float((int)bb.xmin - (int)bb.xmin % nspace),
                        float((int)bb.ymin - (int)bb.ymin % nspace), 
                        0,
                        float((int)bb.xmax - (int)bb.xmax % nspace),
                        float((int)bb.ymax - (int)bb.ymax % nspace), 
                        0);
    float w = snapbox.xmax - snapbox.xmin;
    float h = snapbox.ymax - snapbox.ymin;
    if (mathIsEqual(w, 0, 0.00001f) || mathIsEqual(h, 0, 0.00001f))
        return;
    ASSERT(w > 0);
    ASSERT(h > 0);

    uint32 xlines = (uint32)w / nspace + 1;
    uint32 ylines = (uint32)h / nspace + 1;
    uint32 numVerts = (xlines + ylines) * 2;

    MemTempAllocator tmpAlloc;
    DebugDrawContext::Vertex* vertices = tmpAlloc.MallocTyped<DebugDrawContext::Vertex>(numVerts);

    uint32 i = 0;
    for (float yoffset = snapbox.ymin; yoffset <= snapbox.ymax; yoffset += spacing, i += 2) {
        vertices[i].pos.x = snapbox.xmin;
        vertices[i].pos.y = yoffset;
        vertices[i].pos.z = 0;

        uint32 ni = i + 1;
        vertices[ni].pos.x = snapbox.xmax;
        vertices[ni].pos.y = yoffset;
        vertices[ni].pos.z = 0;

        vertices[i].color = vertices[ni].color = (yoffset != 0.0f)
                ? (!mathIsEqual(mathMod(yoffset, boldSpacing), 0.0f, 0.0001f) ? color : boldColor)
                : COLOR_RED;
    }

    for (float xoffset = snapbox.xmin; xoffset <= snapbox.xmax; xoffset += spacing, i += 2) {
        vertices[i].pos.x = xoffset;
        vertices[i].pos.y = snapbox.ymin;
        vertices[i].pos.z = 0;

        uint32 ni = i + 1;
        ASSERT(ni < numVerts);
        vertices[ni].pos.x = xoffset;
        vertices[ni].pos.y = snapbox.ymax;
        vertices[ni].pos.z = 0;

        vertices[i].color = vertices[ni].color = (xoffset != 0.0f)
                ? (!mathIsEqual(mathMod(xoffset, boldSpacing), 0.0f, 0.0001f) ? color : boldColor)
                : COLOR_GREEN;
    }

    uint64 vertexBufferOffset = 0;
    Mat4 finalTransformMat = gfxIsRenderingToSwapchain() ? gfxGetClipspaceTransform()*viewProjMat : viewProjMat;

    gfxCmdUpdateBuffer(gDebugDraw.vertexBuffer, vertices, sizeof(DebugDrawContext::Vertex)*numVerts);
    gfxCmdBindPipeline(gDebugDraw.pipeline);
    gfxCmdPushConstants(gDebugDraw.pipeline, GfxShaderStage::Vertex, &finalTransformMat, sizeof(viewProjMat));
    gfxCmdBindVertexBuffers(0, 1, &gDebugDraw.vertexBuffer, &vertexBufferOffset);

    gfxCmdDraw(numVerts, 1, 0, 0);
}

bool DebugDraw::Initialize()
{
    gDebugDraw.vertexBuffer = gfxCreateBuffer(GfxBufferDesc {
        .size = sizeof(DebugDrawContext::Vertex)*kDebugDrawMaxVerts,
        .type = GfxBufferType::Vertex,
        .usage = GfxBufferUsage::Stream
    });
    if (!gDebugDraw.vertexBuffer.IsValid())
        return false;

    gDebugDraw.shaderAsset = Asset::LoadShader("/shaders/DebugDraw.hlsl", ShaderLoadParams(),
                                               Engine::RegisterInitializeResources(_InitializeGraphicsResources));

    LOG_INFO("(init) DebugDraw initialized");
    return true;
}

void DebugDraw::Release()
{
    gfxDestroyBuffer(gDebugDraw.vertexBuffer);
    gfxDestroyPipeline(gDebugDraw.pipeline);
}

