#include "DebugDraw.h"

#include "../Engine.h"
#include "../Assets/Shader.h"
#include "../Assets/AssetManager.h"

#include "../Core/Log.h"
#include "../Core/MathAll.h"

#include "../Common/Camera.h"

static constexpr uint32 DEBUGDRAW_MAX_VERTICES = 32*1000;

struct DebugDrawShaderPerObjectData
{
    Mat4 localToWorldMat;
    Float4 colorTint;
};

struct DebugDrawItem
{
    Mat4 localToWorldMat;
    uint32 vertexCount;
    uint32 firstVertex;
    Float4 color;
};

struct DebugDrawVertex
{
    Float3 pos;
    Color4u color;
};

struct DebugDrawSphereCacheItem
{
    uint32 numRings;
    uint32 numSectors;
    uint32 numVertices;
    DebugDrawVertex* vertices;
};

struct DebugDrawContext
{
    GfxPipelineHandle pipeline;
    GfxPipelineLayoutHandle pipelineLayout;
    AssetHandleShader shaderAsset;
    GfxBufferHandle vertexBuffer;
    GfxBufferHandle ubPerFrameData;

    GfxCommandBuffer cmd;
    Array<DebugDrawVertex> vertices;      // Mapped vertices from the staging buffer. We stream all verts into this
    uint32 vertexIndex;
    Int2 viewExtents;
    Array<DebugDrawItem> drawItems;
    Array<DebugDrawSphereCacheItem> sphereCache;
    GfxBufferHandle stagingVertexBuffer;
    bool isDrawing;
    bool isInDrawItem;
};

static DebugDrawContext gDebugDraw;

namespace DebugDraw
{
    static void _InitializeGraphicsResources(void*)
    {
        // Wireframe 
        GfxVertexBufferBindingDesc vertexBufferBindingDesc = {
            .binding = 0,
            .stride = sizeof(DebugDrawVertex),
            .inputRate = GfxVertexInputRate::Vertex
        };

        GfxVertexInputAttributeDesc vertexInputAttDescs[] = {
            {
                .semantic = "POSITION",
                .binding = 0,
                .format  = GfxFormat::R32G32B32_SFLOAT,
                .offset = offsetof(DebugDrawVertex, pos)
            },
            {
                .semantic = "COLOR",
                .binding = 0,
                .format = GfxFormat::R8G8B8A8_UNORM,
                .offset = offsetof(DebugDrawVertex, color)
            }
        };

        GfxPipelineLayoutDesc::PushConstant pushConstants[] = {
            {
                .name = "PerObjectData",
                .stagesUsed = GfxShaderStage::Vertex,
                .size = sizeof(DebugDrawShaderPerObjectData)
            }
        };

        GfxPipelineLayoutDesc::Binding bindings[] = {
            {
                .name = "PerFrameData",
                .type = GfxDescriptorType::UniformBuffer,
                .stagesUsed = GfxShaderStage::Vertex,
            }
        };

        AssetObjPtrScope<GfxShader> shader(gDebugDraw.shaderAsset);
        ASSERT(shader);

        GfxPipelineLayoutDesc pipelineLayoutDesc {
            .numBindings = CountOf(bindings),
            .bindings = bindings,
            .numPushConstants = CountOf(pushConstants),
            .pushConstants = pushConstants
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
            .depthAttachmentFormat = GfxBackend::GetValidDepthStencilFormat(),
            .stencilAttachmentFormat = GfxBackend::GetValidDepthStencilFormat()
        };
        
        gDebugDraw.pipeline = GfxBackend::CreateGraphicsPipeline(*shader, gDebugDraw.pipelineLayout, pipelineDesc);
        ASSERT(gDebugDraw.pipeline.IsValid());

        GfxBufferDesc vertexBufferDesc {
            .sizeBytes = sizeof(DebugDrawVertex)*DEBUGDRAW_MAX_VERTICES,
            .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Vertex,
            .arena = GfxMemoryArena::PersistentGPU
        };
        gDebugDraw.vertexBuffer = GfxBackend::CreateBuffer(vertexBufferDesc);      
        
        GfxBufferDesc uniformBufferDesc {
            .sizeBytes = sizeof(Mat4),
            .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Uniform
        };
        gDebugDraw.ubPerFrameData = GfxBackend::CreateBuffer(uniformBufferDesc);
    }

    static void _BeginDrawItem()
    {
        ASSERT(gDebugDraw.isDrawing);
        ASSERT(!gDebugDraw.isInDrawItem);

        gDebugDraw.isInDrawItem = true;
        gDebugDraw.vertexIndex = gDebugDraw.vertices.Count();
    }

    static void _EndDrawItem(const Mat4& localToWorldMat = MAT4_IDENT, Color4u tintColor = COLOR4U_WHITE)
    {
        ASSERT(gDebugDraw.isDrawing);
        ASSERT(gDebugDraw.isInDrawItem);

        gDebugDraw.isInDrawItem = false;

        uint32 numSubmittedVerts = gDebugDraw.vertices.Count() - gDebugDraw.vertexIndex;
        ASSERT(numSubmittedVerts);

        DebugDrawItem item {
            .localToWorldMat = localToWorldMat,
            .vertexCount = numSubmittedVerts,
            .firstVertex = gDebugDraw.vertexIndex,
            .color = Color4u::ToFloat4(tintColor)
        };
        gDebugDraw.drawItems.Push(item);
    }

    static Span<DebugDrawVertex> _GetVerticesForCurrentItem()
    {
        ASSERT(gDebugDraw.isDrawing);
        ASSERT(gDebugDraw.isInDrawItem);

        return Span<DebugDrawVertex>(&gDebugDraw.vertices[gDebugDraw.vertexIndex], gDebugDraw.vertices.Count() - gDebugDraw.vertexIndex);

    }
} // DebugDraw

void DebugDraw::BeginDraw(GfxCommandBuffer cmd, const Camera& cam, uint16 viewWidth, uint16 viewHeight)
{
    ASSERT(viewWidth > 0);
    ASSERT(viewHeight > 0);
    ASSERT(!gDebugDraw.isDrawing);
    ASSERT(!gDebugDraw.stagingVertexBuffer.IsValid());
    ASSERT(gDebugDraw.drawItems.IsEmpty());
    gDebugDraw.isDrawing = true;

    gDebugDraw.viewExtents = Int2(viewWidth, viewHeight);

    size_t vertexBufferSize = sizeof(DebugDrawVertex)*DEBUGDRAW_MAX_VERTICES;
    GfxBufferDesc stagingVertexBufferDesc {
        .sizeBytes = vertexBufferSize,
        .usageFlags = GfxBufferUsageFlags::TransferSrc,
        .arena = GfxMemoryArena::TransientCPU
    };
    gDebugDraw.stagingVertexBuffer = GfxBackend::CreateBuffer(stagingVertexBufferDesc);
    ASSERT(gDebugDraw.stagingVertexBuffer.IsValid());

    void* mapped = nullptr;
    cmd.MapBuffer(gDebugDraw.stagingVertexBuffer, (void**)&mapped);
    gDebugDraw.vertices.Reserve(DEBUGDRAW_MAX_VERTICES, mapped, vertexBufferSize);

    {
        GfxHelperBufferUpdateScope bufferUpdater(cmd, gDebugDraw.ubPerFrameData, sizeof(Mat4), GfxShaderStage::Vertex);
        Mat4 worldToClipMat = cam.GetPerspectiveMat(float(viewWidth), float(viewHeight)) * cam.GetViewMat();
        if (cmd.mDrawsToSwapchain) 
            worldToClipMat = GfxBackend::GetSwapchainTransformMat()*worldToClipMat;
        *((Mat4*)bufferUpdater.mData) = worldToClipMat;
    }
}

void DebugDraw::EndDraw(GfxCommandBuffer cmd, GfxImageHandle depthImage)
{
    ASSERT_MSG(cmd.mIsRecording && !cmd.mIsInRenderPass, "%s must be called while CommandBuffer is recording and not in the RenderPass", __FUNCTION__);
    ASSERT(gDebugDraw.isDrawing);
    ASSERT(!gDebugDraw.isInDrawItem);
    ASSERT(gDebugDraw.stagingVertexBuffer.IsValid());

    if (!gDebugDraw.drawItems.IsEmpty()) {
        GPU_PROFILE_ZONE(cmd, "DebugDraw");

        cmd.FlushBuffer(gDebugDraw.stagingVertexBuffer);
        cmd.TransitionBuffer(gDebugDraw.vertexBuffer, GfxBufferTransition::TransferWrite);
        cmd.CopyBufferToBuffer(gDebugDraw.stagingVertexBuffer, gDebugDraw.vertexBuffer, GfxShaderStage::Vertex);

        GfxViewport viewport {
            .width = float(gDebugDraw.viewExtents.x),
            .height = float(gDebugDraw.viewExtents.y)
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

        RectInt scissor(0, 0, gDebugDraw.viewExtents.x, gDebugDraw.viewExtents.y);
        cmd.SetScissors(0, 1, &scissor);

        cmd.BindPipeline(gDebugDraw.pipeline);

        uint64 vertexBufferOffset = 0;
        cmd.BindVertexBuffers(0, 1, &gDebugDraw.vertexBuffer, &vertexBufferOffset);

        GfxBindingDesc bindings[] = {
            {
                .name = "PerFrameData",
                .buffer = gDebugDraw.ubPerFrameData
            }
        };
        cmd.PushBindings(gDebugDraw.pipelineLayout, CountOf(bindings), bindings);

        for (const DebugDrawItem& item : gDebugDraw.drawItems) {
            DebugDrawShaderPerObjectData objData {
                .localToWorldMat = item.localToWorldMat,
                .colorTint = item.color
            };
            cmd.PushConstants<DebugDrawShaderPerObjectData>(gDebugDraw.pipelineLayout, "PerObjectData", objData);
            cmd.Draw(item.vertexCount, 1, item.firstVertex, 0);
        }

        cmd.EndRenderPass();
    }

    GfxBackend::DestroyBuffer(gDebugDraw.stagingVertexBuffer);

    gDebugDraw.drawItems.Clear();
    gDebugDraw.vertices.Free();
    gDebugDraw.stagingVertexBuffer = {};
    gDebugDraw.isDrawing = false;
}

void DebugDraw::DrawGroundGrid(const Camera& cam, const DebugDrawGridProperties& props)
{
    Color4u color = props.lineColor;
    Color4u boldColor = props.boldLineColor;

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

    _BeginDrawItem();
    for (float yoffset = snapbox.ymin; yoffset <= snapbox.ymax; yoffset += spacing) {
        DebugDrawVertex* v1 = gDebugDraw.vertices.Push();
        DebugDrawVertex* v2 = gDebugDraw.vertices.Push();
        v1->pos = Float3(snapbox.xmin, yoffset, 0);
        v2->pos = Float3(snapbox.xmax, yoffset, 0);

        v1->color = v2->color = (yoffset != 0.0f)
            ? (!M::IsEqual(M::Mod(yoffset, boldSpacing), 0.0f, 0.0001f) ? color : boldColor)
            : COLOR4U_RED;
    }

    for (float xoffset = snapbox.xmin; xoffset <= snapbox.xmax; xoffset += spacing) {
        DebugDrawVertex* v1 = gDebugDraw.vertices.Push();
        DebugDrawVertex* v2 = gDebugDraw.vertices.Push();

        v1->pos = Float3(xoffset, snapbox.ymin, 0);
        v2->pos = Float3(xoffset, snapbox.ymax, 0);

        v1->color = v2->color = (xoffset != 0.0f)
            ? (!M::IsEqual(M::Mod(xoffset, boldSpacing), 0.0f, 0.0001f) ? color : boldColor)
            : COLOR4U_GREEN;
    }
    _EndDrawItem();
}

bool DebugDraw::Initialize()
{
    gDebugDraw.shaderAsset = Shader::Load("/shaders/DebugDraw.hlsl", ShaderLoadParams(),
                                          Engine::RegisterInitializeResources(_InitializeGraphicsResources));

    LOG_INFO("(init) DebugDraw initialized");
    return true;
}

void DebugDraw::Release()
{
    GfxBackend::DestroyBuffer(gDebugDraw.vertexBuffer);
    GfxBackend::DestroyPipeline(gDebugDraw.pipeline);
    GfxBackend::DestroyPipelineLayout(gDebugDraw.pipelineLayout);
    GfxBackend::DestroyBuffer(gDebugDraw.ubPerFrameData);

    for (DebugDrawSphereCacheItem& c : gDebugDraw.sphereCache)
        Mem::Free(c.vertices);

    gDebugDraw.vertices.Free();
    gDebugDraw.sphereCache.Free();

}

void DebugDraw::DrawBoundingSphere(Float4 sphere, Color4u color, uint32 numRings, uint32 numSectors)
{
    uint32 cacheIdx = gDebugDraw.sphereCache.FindIf([numRings, numSectors](const DebugDrawSphereCacheItem& i) { return i.numSectors == numSectors && i.numRings == i.numRings; });

    _BeginDrawItem();
    if (cacheIdx != -1) {
        const DebugDrawSphereCacheItem& cache = gDebugDraw.sphereCache[cacheIdx];
        gDebugDraw.vertices.PushBatch(cache.vertices, cache.numVertices);
    }
    else {
        Float3 center = Float3(sphere.x, sphere.y, sphere.z);
        float radius = 1.0f;

        float numSectorsRcp = 1.0f / float(numSectors);
        float numRingsRcp = 1.0f / float(numRings);

        // Generate ring lines (horizontal circles)
        for (uint32 i = 0; i <= numRings; i++) {
            float phi = M_PI * float(i) * numRingsRcp;  // 0 to PI
            float y = radius * M::Cos(phi);   // height
        
            for (uint32 j = 0; j < numSectors; j++) {
                float theta = 2.0f * M_PI * float(j) * numSectorsRcp;  // 0 to 2*PI
                float nextTheta = 2.0f * M_PI * float(j + 1) * numSectorsRcp;
            
                DebugDrawVertex* v1 = gDebugDraw.vertices.Push();
                v1->pos = Float3(radius * M::Sin(phi) * M::Cos(theta), radius * M::Sin(phi) * M::Sin(theta), y);
            
                DebugDrawVertex* v2 = gDebugDraw.vertices.Push();
                v2->pos = Float3(radius * M::Sin(phi) * M::Cos(nextTheta), radius * M::Sin(phi) * M::Sin(nextTheta), y);

                v1->color = v2->color = COLOR4U_WHITE;
            }
        }
    
        // Generate sector lines (meridians)
        for (uint32 j = 0; j < numSectors; j++) {
            float theta = 2.0f * M_PI * float(j) * numSectorsRcp;
        
            for (uint32 i = 0; i < numRings; i++) {
                float phi1 = M_PI * float(i) * numRingsRcp;
                float phi2 = M_PI * float(i + 1) * numRingsRcp;
            
                DebugDrawVertex* v1 = gDebugDraw.vertices.Push();
                v1->pos = Float3(radius * M::Sin(phi1) * M::Cos(theta), radius * M::Sin(phi1) * M::Sin(theta), radius * M::Cos(phi1));
            
                DebugDrawVertex* v2 = gDebugDraw.vertices.Push();
                v2->pos = Float3(radius * M::Sin(phi2) * M::Cos(theta), radius * M::Sin(phi2) * M::Sin(theta), radius * M::Cos(phi2));

                v1->color = v2->color = COLOR4U_WHITE;
            }
        }

        Span<DebugDrawVertex> verts = _GetVerticesForCurrentItem();
        
        DebugDrawSphereCacheItem cache {
            .numRings = numRings,
            .numSectors = numSectors,
            .numVertices = verts.Count(),
            .vertices = Mem::AllocCopy<DebugDrawVertex>(verts.Ptr(), verts.Count())
        };
        gDebugDraw.sphereCache.Push(cache);
    }

    Mat4 transformMat = Mat4::TransformMat(sphere.x, sphere.y, sphere.z, 0, 0, 0, sphere.w, sphere.w, sphere.w);
    _EndDrawItem(transformMat, color);
}
