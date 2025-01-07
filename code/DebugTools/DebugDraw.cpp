#include "DebugDraw.h"

#include "../Engine.h"
#include "../Assets/Shader.h"
#include "../Assets/AssetManager.h"

#include "../Core/Log.h"
#include "../Core/MathAll.h"

#include "../Common/Camera.h"

static constexpr uint32 DEBUGDRAW_MAX_VERTICES = 32*1000;

struct DebugDrawItem
{
    uint32 vertexCount;
    uint32 firstVertex;
};

struct DebugDrawShaderTransform
{
    Mat4 viewProjMat;
};

struct DebugDrawShaderVertex
{
    Float3 pos;
    Color color;
};

struct DebugDrawContext
{
    GfxPipelineHandle pipeline;
    GfxPipelineLayoutHandle pipelineLayout;
    AssetHandleShader shaderAsset;
    GfxBufferHandle vertexBuffer;

    GfxCommandBuffer cmd;
    DebugDrawShaderVertex* mappedVertices;
    uint32 vertexIndex;
    Int2 viewExtents;
    Array<DebugDrawItem> drawItems;
    GfxBufferHandle stagingVertexBuffer;
    bool isDrawing;
};

static DebugDrawContext gDebugDraw;

namespace DebugDraw
{
    static void _InitializeGraphicsResources(void*)
    {
        // Wireframe 
        GfxVertexBufferBindingDesc vertexBufferBindingDesc = {
            .binding = 0,
            .stride = sizeof(DebugDrawShaderVertex),
            .inputRate = GfxVertexInputRate::Vertex
        };

        GfxVertexInputAttributeDesc vertexInputAttDescs[] = {
            {
                .semantic = "POSITION",
                .binding = 0,
                .format  = GfxFormat::R32G32B32_SFLOAT,
                .offset = offsetof(DebugDrawShaderVertex, pos)
            },
            {
                .semantic = "COLOR",
                .binding = 0,
                .format = GfxFormat::R8G8B8A8_UNORM,
                .offset = offsetof(DebugDrawShaderVertex, color)
            }
        };

        GfxPipelineLayoutDesc::PushConstant pushConstant {
            .name = "Transform",
            .stagesUsed = GfxShaderStage::Vertex,
            .size = sizeof(DebugDrawShaderTransform)
        };

        AssetObjPtrScope<GfxShader> shader(gDebugDraw.shaderAsset);
        ASSERT(shader);

        GfxPipelineLayoutDesc pipelineLayoutDesc {
            .numPushConstants = 1,
            .pushConstants = &pushConstant
        };

        gDebugDraw.pipelineLayout = GfxBackend::CreatePipelineLayout(*shader, pipelineLayoutDesc);

        GfxGraphicsPipelineDesc pipelineDesc {
            .inputAssemblyTopology = GfxPrimitiveTopology::LineList,
            .numVertexInputAttributes = CountOf(vertexInputAttDescs),
            .vertexInputAttributes = vertexInputAttDescs,
            .numVertexBufferBindings = 1,
            .vertexBufferBindings = &vertexBufferBindingDesc,
            .blend = {
                .numAttachments = 1,
                .attachments = GfxBlendAttachmentDesc::GetDefault()
            },
            .depthStencil = {
                .depthTestEnable = true,
                .depthWriteEnable = false,
                .depthCompareOp = GfxCompareOp::Less
            },
            .numColorAttachments = 1,
            .colorAttachmentFormats = {GfxBackend::GetSwapchainFormat()},
            .depthAttachmentFormat = GfxFormat::D24_UNORM_S8_UINT
        };
        
        gDebugDraw.pipeline = GfxBackend::CreateGraphicsPipeline(*shader, gDebugDraw.pipelineLayout, pipelineDesc);
        ASSERT(gDebugDraw.pipeline.IsValid());

        GfxBufferDesc vertexBufferDesc {
            .sizeBytes = sizeof(DebugDrawShaderVertex)*DEBUGDRAW_MAX_VERTICES,
            .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Vertex,
            .arena = GfxMemoryArena::PersistentGPU
        };
        gDebugDraw.vertexBuffer = GfxBackend::CreateBuffer(vertexBufferDesc);       
    }
} // DebugDraw

void DebugDraw::BeginDraw(GfxCommandBuffer cmd, uint16 viewWidth, uint16 viewHeight)
{
    ASSERT(viewWidth > 0);
    ASSERT(viewHeight > 0);
    ASSERT(!gDebugDraw.isDrawing);
    ASSERT(!gDebugDraw.stagingVertexBuffer.IsValid());
    ASSERT(gDebugDraw.drawItems.IsEmpty());
    gDebugDraw.isDrawing = true;

    gDebugDraw.viewExtents = Int2(viewWidth, viewHeight);
    gDebugDraw.vertexIndex = 0;

    size_t vertexBufferSize = sizeof(DebugDrawShaderVertex)*DEBUGDRAW_MAX_VERTICES;
    GfxBufferDesc stagingVertexBufferDesc {
        .sizeBytes = vertexBufferSize,
        .usageFlags = GfxBufferUsageFlags::TransferSrc,
        .arena = GfxMemoryArena::TransientCPU
    };
    gDebugDraw.stagingVertexBuffer = GfxBackend::CreateBuffer(stagingVertexBufferDesc);
    ASSERT(gDebugDraw.stagingVertexBuffer.IsValid());

    cmd.MapBuffer(gDebugDraw.stagingVertexBuffer, (void**)&gDebugDraw.mappedVertices);
}

void DebugDraw::EndDraw(GfxCommandBuffer cmd, const Camera& cam, GfxImageHandle depthImage)
{
    ASSERT_MSG(cmd.mIsRecording && !cmd.mIsInRenderPass, "%s must be called while CommandBuffer is recording and not in the RenderPass", __FUNCTION__);
    ASSERT(gDebugDraw.isDrawing);
    ASSERT(gDebugDraw.stagingVertexBuffer.IsValid());
    ASSERT_MSG(!gDebugDraw.drawItems.IsEmpty(), "No DrawXXX commands are submitted");

    cmd.FlushBuffer(gDebugDraw.stagingVertexBuffer);
    cmd.TransitionBuffer(gDebugDraw.vertexBuffer, GfxBufferTransition::TransferWrite);
    cmd.CopyBufferToBuffer(gDebugDraw.stagingVertexBuffer, gDebugDraw.vertexBuffer, GfxShaderStage::Vertex);

    Int2 viewExtents = gDebugDraw.viewExtents;
    Mat4 viewProjMat = cam.GetPerspectiveMat(float(viewExtents.x), float(viewExtents.y)) * cam.GetViewMat();
    DebugDrawShaderTransform transform {
        .viewProjMat = cmd.mDrawsToSwapchain ? GfxBackend::GetSwapchainTransformMat()*viewProjMat : viewProjMat
    };

    GfxViewport viewport {
        .width = float(viewExtents.x),
        .height = float(viewExtents.y)
    };

    // Begin Drawing to the swapchain 
    // Note: We cannot BeginRenderPass while updating the buffers
    GfxBackendRenderPass pass { 
        .colorAttachments = {{ .load = true }},
        .depthAttachment = { 
            .image = depthImage,
            .load = true
        },
        .swapchain = true,
        .hasDepth = true
    };
    cmd.BeginRenderPass(pass);

    cmd.SetViewports(0, 1, &viewport);

    RectInt scissor(0, 0, viewExtents.x, viewExtents.y);
    cmd.SetScissors(0, 1, &scissor);

    cmd.BindPipeline(gDebugDraw.pipeline);
    cmd.PushConstants(gDebugDraw.pipelineLayout, "Transform", &transform, sizeof(viewProjMat));

    uint64 vertexBufferOffset = 0;
    cmd.BindVertexBuffers(0, 1, &gDebugDraw.vertexBuffer, &vertexBufferOffset);

    for (const DebugDrawItem& item : gDebugDraw.drawItems)
        cmd.Draw(item.vertexCount, 1, item.firstVertex, 0);

    cmd.EndRenderPass();
    GfxBackend::DestroyBuffer(gDebugDraw.stagingVertexBuffer);

    gDebugDraw.drawItems.Clear();

    gDebugDraw.stagingVertexBuffer = {};
    gDebugDraw.isDrawing = false;
}

void DebugDraw::DrawGroundGrid(const Camera& cam, const DebugDrawGridProperties& props)
{
    ASSERT(gDebugDraw.isDrawing);

    Color color = props.lineColor;
    Color boldColor = props.boldLineColor;

    float spacing = M::Ceil(Max(props.spacing, 0.0001f));
    float boldSpacing = props.boldSpacing;
    ASSERT(boldSpacing >= spacing);
    ASSERT(props.distance > 0);

    Int2 viewExtents = gDebugDraw.viewExtents;
    CameraFrustumPoints frustumPts = cam.GetFrustumPoints(float(viewExtents.x), float(viewExtents.y), -props.distance, props.distance);
    AABB bb = AABB_EMPTY;

    // extrude near plane
    Float3 nearPlaneN = Plane::CalcNormal(frustumPts[0], frustumPts[1], frustumPts[2]);
    for (uint32 i = 0; i < frustumPts.Count(); i++) {
        if (i < 4) {
            Float3 offsetPt = frustumPts[i] - nearPlaneN*spacing;
            AABB::AddPoint(&bb, Float3(offsetPt.x, offsetPt.y, 0));
        } 
        else {
            AABB::AddPoint(&bb, Float3(frustumPts[i].x, frustumPts[i].y, 0));
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
    if (M::IsEqual(w, 0, 0.00001f) || M::IsEqual(h, 0, 0.00001f))
        return;
    ASSERT(w > 0);
    ASSERT(h > 0);

    uint32 xlines = (uint32)w / nspace + 1;
    uint32 ylines = (uint32)h / nspace + 1;
    uint32 numVerts = (xlines + ylines) * 2;

    DebugDrawShaderVertex* vertices = gDebugDraw.mappedVertices;
    ASSERT(vertices);

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
                ? (!M::IsEqual(M::Mod(yoffset, boldSpacing), 0.0f, 0.0001f) ? color : boldColor)
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
                ? (!M::IsEqual(M::Mod(xoffset, boldSpacing), 0.0f, 0.0001f) ? color : boldColor)
                : COLOR_GREEN;
    }

    DebugDrawItem item {
        .vertexCount = numVerts,
        .firstVertex = gDebugDraw.vertexIndex
    };
    gDebugDraw.drawItems.Push(item);
    gDebugDraw.vertexIndex += numVerts;
    ASSERT(gDebugDraw.vertexIndex <= DEBUGDRAW_MAX_VERTICES);
}

bool DebugDraw::Initialize()
{
    gDebugDraw.shaderAsset = Asset::LoadShader("/shaders/DebugDraw.hlsl", ShaderLoadParams(),
                                               Engine::RegisterInitializeResources(_InitializeGraphicsResources));

    LOG_INFO("(init) DebugDraw initialized");
    return true;
}

void DebugDraw::Release()
{
    GfxBackend::DestroyBuffer(gDebugDraw.vertexBuffer);
    GfxBackend::DestroyPipeline(gDebugDraw.pipeline);
    GfxBackend::DestroyPipelineLayout(gDebugDraw.pipelineLayout);
}

