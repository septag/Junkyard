#include "DebugDraw.h"

#include "../Engine.h"
#include "../Assets/Shader.h"
#include "../Assets/AssetManager.h"
#include "../Assets/Font.h"

#include "../Core/Log.h"
#include "../Core/MathAll.h"

#include "../Common/Camera.h"

#include "../Graphics/TextBuilder.h"

static constexpr uint32 DEBUGDRAW_MAX_VERTICES = 32*1000;
static constexpr uint32 DEBUGDRAW_MAX_TEXT_CHARACTERS = 1000;

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

struct DebugDrawAABB
{
    DebugDrawVertex vertices[24];   // 12 Edges
};

struct DebugDrawContext
{
    Array<DebugDrawVertex> vertices;      // Mapped vertices from the staging buffer. We stream all verts into this
    uint32 vertexIndex;
    AssetHandleShader shaderAsset;
    GfxPipelineHandle pipeline;
    GfxPipelineLayoutHandle pipelineLayout;
    GfxBufferHandle vertexBuffer;
    GfxBufferHandle ubPerFrameData;

    Array<TextVertex> textVertices;
    Array<uint32> textIndices;
    AssetHandleShader textShaderAsset;
    AssetHandleFont textFont;
    GfxPipelineHandle textPipeline;
    GfxPipelineLayoutHandle textPipelineLayout;
    GfxSamplerHandle textSampler;
    GfxBufferHandle textVertexBuffer;
    GfxBufferHandle textIndexBuffer;

    GfxMultiSampleCount msaa = GfxMultiSampleCount::SampleCount1;

    GfxCommandBuffer cmd;

    Int2 viewExtents;
    Mat4 worldToClipMat;
    DebugDrawAABB aabb;
    Array<DebugDrawItem> drawItems;
    Array<DebugDrawSphereCacheItem> sphereCache;
    GfxBufferHandle stagingVertexBuffer;
    GfxBufferHandle stagingTextVertexBuffer;
    GfxBufferHandle stagingTextIndexBuffer;
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
            .type = GfxPipelineLayoutType::PushDescriptor,
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
            .msaa = {
                .sampleCount = gDebugDraw.msaa
            },
            .numColorAttachments = 1,
            .colorAttachmentFormats = {GfxBackend::GetSwapchainFormat()},
            .depthAttachmentFormat = GfxBackend::GetValidDepthStencilFormat(),
            .stencilAttachmentFormat = GfxBackend::GetValidDepthStencilFormat()
        };
        
        gDebugDraw.pipeline = GfxBackend::CreateGraphicsPipeline(*shader, gDebugDraw.pipelineLayout, pipelineDesc);
        ASSERT(gDebugDraw.pipeline.IsValid());

        {
            GfxBufferDesc vertexBufferDesc {
                .sizeBytes = sizeof(DebugDrawVertex)*DEBUGDRAW_MAX_VERTICES,
                .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Vertex,
                .arena = GfxMemoryArena::PersistentGPU,
                .perFrameUpdates = true
            };
            gDebugDraw.vertexBuffer = GfxBackend::CreateBuffer(vertexBufferDesc);      
        }

        {        
            GfxBufferDesc uniformBufferDesc {
                .sizeBytes = sizeof(Mat4),
                .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Uniform,
                .perFrameUpdates = true
            };
            gDebugDraw.ubPerFrameData = GfxBackend::CreateBuffer(uniformBufferDesc);
        }

        // Text Stuff
        AssetObjPtrScope<GfxShader> textShader(gDebugDraw.textShaderAsset);
        ASSERT(textShader);
        TextDrawGraphicsObjects textObjects = TextBuilder::HelperCreateGraphicsObjects(*textShader, TextEffect::Outline,
                                                                                       GfxBackend::GetSwapchainFormat(), 
                                                                                       GfxBackend::GetValidDepthStencilFormat());
        gDebugDraw.textPipeline = textObjects.pipeline;
        gDebugDraw.textPipelineLayout = textObjects.pipelineLayout;
        gDebugDraw.textSampler = textObjects.sampler;

        {
            GfxBufferDesc bufferDesc {
                .sizeBytes = DEBUGDRAW_MAX_TEXT_CHARACTERS*sizeof(TextVertex)*4,
                .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Vertex,
                .arena = GfxMemoryArena::PersistentGPU,
                .perFrameUpdates = true
            };

            gDebugDraw.textVertexBuffer = GfxBackend::CreateBuffer(bufferDesc);
        }

        {
            GfxBufferDesc bufferDesc {
                .sizeBytes = DEBUGDRAW_MAX_TEXT_CHARACTERS*sizeof(uint32)*6,
                .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Index,
                .arena = GfxMemoryArena::PersistentGPU,
                .perFrameUpdates = true
            };

            gDebugDraw.textIndexBuffer = GfxBackend::CreateBuffer(bufferDesc);
        }
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
    ASSERT(!gDebugDraw.stagingTextVertexBuffer.IsValid());
    ASSERT(!gDebugDraw.stagingTextIndexBuffer.IsValid());
    ASSERT(gDebugDraw.drawItems.IsEmpty());
    gDebugDraw.isDrawing = true;

    gDebugDraw.viewExtents = Int2(viewWidth, viewHeight);
    gDebugDraw.worldToClipMat = cam.GetPerspectiveMat(float(viewWidth), float(viewHeight)) * cam.GetViewMat();
    if (cmd.mDrawsToSwapchain) 
        gDebugDraw.worldToClipMat = GfxBackend::GetSwapchainTransformMat()*gDebugDraw.worldToClipMat;

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
        *((Mat4*)bufferUpdater.mData) = gDebugDraw.worldToClipMat;
    }

    // Text
    {
        size_t textVertexBufferSize = sizeof(TextVertex)*DEBUGDRAW_MAX_TEXT_CHARACTERS*4;
        size_t textIndexBufferSize = sizeof(uint32)*DEBUGDRAW_MAX_TEXT_CHARACTERS*6;
        GfxBufferDesc vertexBufferDesc {
            .sizeBytes = vertexBufferSize,
            .usageFlags = GfxBufferUsageFlags::TransferSrc,
            .arena = GfxMemoryArena::TransientCPU
        };
        gDebugDraw.stagingTextVertexBuffer = GfxBackend::CreateBuffer(vertexBufferDesc);
        cmd.MapBuffer(gDebugDraw.stagingTextVertexBuffer, (void**)&mapped);
        gDebugDraw.textVertices.Reserve(DEBUGDRAW_MAX_TEXT_CHARACTERS*4, mapped, textVertexBufferSize);
        
        GfxBufferDesc indexBufferDesc {
            .sizeBytes = textIndexBufferSize,
            .usageFlags = GfxBufferUsageFlags::TransferSrc,
            .arena = GfxMemoryArena::TransientCPU
        };
        gDebugDraw.stagingTextIndexBuffer = GfxBackend::CreateBuffer(indexBufferDesc);
        cmd.MapBuffer(gDebugDraw.stagingTextIndexBuffer, (void**)&mapped);
        gDebugDraw.textIndices.Reserve(DEBUGDRAW_MAX_TEXT_CHARACTERS*6, mapped, textIndexBufferSize);
    }
}

void DebugDraw::EndDraw(GfxCommandBuffer cmd, GfxImageHandle depthImage, GfxImageHandle colorImage)
{
    ASSERT_MSG(cmd.mIsRecording && !cmd.mIsInRenderPass, "%s must be called while CommandBuffer is recording and not in the RenderPass", __FUNCTION__);
    ASSERT(gDebugDraw.isDrawing);
    ASSERT(!gDebugDraw.isInDrawItem);
    ASSERT(gDebugDraw.stagingVertexBuffer.IsValid());
    ASSERT(gDebugDraw.stagingTextVertexBuffer.IsValid());
    ASSERT(gDebugDraw.stagingTextIndexBuffer.IsValid());

    bool hasAnything = !gDebugDraw.drawItems.IsEmpty() | !gDebugDraw.textVertices.IsEmpty();

    GPU_PROFILE_ZONE(cmd, "DebugDraw");

    if (!gDebugDraw.drawItems.IsEmpty()) {
        cmd.FlushBuffer(gDebugDraw.stagingVertexBuffer);
        cmd.TransitionBuffer(gDebugDraw.vertexBuffer, GfxBufferTransition::TransferWrite);
        cmd.CopyBufferToBuffer(gDebugDraw.stagingVertexBuffer, gDebugDraw.vertexBuffer, GfxShaderStage::Vertex);
    }

    if (!gDebugDraw.textVertices.IsEmpty()) {
        cmd.FlushBuffer(gDebugDraw.stagingTextVertexBuffer);
        cmd.TransitionBuffer(gDebugDraw.textVertexBuffer, GfxBufferTransition::TransferWrite);
        cmd.CopyBufferToBuffer(gDebugDraw.stagingTextVertexBuffer, gDebugDraw.textVertexBuffer, GfxShaderStage::Vertex);
    }

    if (!gDebugDraw.textIndices.IsEmpty()) {
        cmd.FlushBuffer(gDebugDraw.stagingTextIndexBuffer);
        cmd.TransitionBuffer(gDebugDraw.textIndexBuffer, GfxBufferTransition::TransferWrite);
        cmd.CopyBufferToBuffer(gDebugDraw.stagingTextIndexBuffer, gDebugDraw.textIndexBuffer, GfxShaderStage::Vertex);
    }

    GfxViewport viewport {
        .width = float(gDebugDraw.viewExtents.x),
        .height = float(gDebugDraw.viewExtents.y)
    };

    if (hasAnything) {
        bool isMSAA = false;
        if (colorImage.IsValid()) {
            GfxMultiSampleCount sampleCount = GfxBackend::GetImageDesc(colorImage).multisampleFlags;
            isMSAA = sampleCount != GfxMultiSampleCount::SampleCount1;
            ASSERT_MSG(sampleCount == gDebugDraw.msaa, "DebugDraw MSAA does not match the provided render target image sample count");
        }
        else {
            ASSERT_MSG(gDebugDraw.msaa == GfxMultiSampleCount::SampleCount1, "If MSAA is set for DebugDraw, then you should provide a matching render target image");
        }        

        // Begin Drawing to the swapchain 
        // Note: We cannot BeginRenderPass while updating the buffers
        GfxBackendRenderPass pass {
            .numAttachments = colorImage.IsValid() ? 1u : 0u,
            .colorAttachments = {{ 
                .image = colorImage,
                .load = true,
                .resolveToSwapchain = isMSAA,
            }},
            .depthAttachment = { 
                .image = depthImage,
                .load = true
            },
            .swapchain = !colorImage.IsValid(),
            .hasDepth = true
        };
        cmd.BeginRenderPass(pass);
        cmd.SetViewports(0, 1, &viewport);

        RectInt scissor(0, 0, gDebugDraw.viewExtents.x, gDebugDraw.viewExtents.y);
        cmd.SetScissors(0, 1, &scissor);
    }

    // Regular debug geometry
    if (!gDebugDraw.drawItems.IsEmpty()) {
        cmd.BindPipeline(gDebugDraw.pipeline);
        cmd.BindVertexBuffer(gDebugDraw.vertexBuffer, 0);

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
    }

    // Text (1 draw call)
    if (!gDebugDraw.textVertices.IsEmpty() && !gDebugDraw.textIndices.IsEmpty()) {
        cmd.BindPipeline(gDebugDraw.textPipeline);
        cmd.BindVertexBuffer(gDebugDraw.textVertexBuffer, 0);
        cmd.BindIndexBuffer(gDebugDraw.textIndexBuffer, 0, GfxIndexType::Uint32);

        // 2D projection (top-left = 0, 0)
        Mat4 worldToClipMat = Mat4::OrthoOffCenter(0, viewport.height, viewport.width, 0, -1.0f, 1.0f);
        cmd.PushConstants<Mat4>(gDebugDraw.textPipelineLayout, "PerFrameData", worldToClipMat);

        AssetObjPtrScope<FontData> font(gDebugDraw.textFont);
        AssetObjPtrScope<GfxImage> fontImage(font->atlas);
        GfxBindingDesc bindings[] = {
            {
                .name = "FontTexture",
                .image = fontImage->handle,
            },
            {
                .name = "FontSampler",
                .sampler = gDebugDraw.textSampler
            }
        };

        cmd.PushBindings(gDebugDraw.textPipelineLayout, CountOf(bindings), bindings);
        cmd.DrawIndexed(gDebugDraw.textIndices.Count(), 1, 0, 0, 0);
    }

    if (hasAnything)
        cmd.EndRenderPass();


    GfxBackend::DestroyBuffer(gDebugDraw.stagingVertexBuffer);
    GfxBackend::DestroyBuffer(gDebugDraw.stagingTextVertexBuffer);
    GfxBackend::DestroyBuffer(gDebugDraw.stagingTextIndexBuffer);

    gDebugDraw.drawItems.Clear();
    gDebugDraw.vertices.Free();
    gDebugDraw.textVertices.Free();
    gDebugDraw.textIndices.Free();
    gDebugDraw.stagingVertexBuffer = GfxBufferHandle();
    gDebugDraw.stagingTextVertexBuffer = GfxBufferHandle();
    gDebugDraw.stagingTextIndexBuffer = GfxBufferHandle();
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
            AABB::AddPoint(bb, Float3(offsetPt.x, offsetPt.y, 0));
        } 
        else {
            AABB::AddPoint(bb, Float3(frustumPts[i].x, frustumPts[i].y, 0));
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
    AssetGroup loadAssetGroup = Engine::RegisterInitializeResources(_InitializeGraphicsResources);
    gDebugDraw.shaderAsset = Shader::Load("/shaders/DebugDraw.hlsl", ShaderLoadParams(), loadAssetGroup);
    gDebugDraw.textShaderAsset = Shader::Load("/shaders/DrawText.hlsl", ShaderLoadParams(), loadAssetGroup);
    gDebugDraw.textFont = Font::Load("/data/fonts/arial.jfnt", loadAssetGroup);

    // AABB just has one shape that is gonna get transformed to different sizes
    {
        AABB unitBox = AABB(Float3(-0.5f, -0.5f, -0.5f), Float3(0.5f, 0.5f, 0.5f));
        Float3 corners[8];
        AABB::GetCorners(unitBox, corners);
        DebugDrawVertex* vertices = gDebugDraw.aabb.vertices;

        for (uint32 i = 0; i < CountOf(gDebugDraw.aabb.vertices); i++)
            vertices[i].color = COLOR4U_WHITE;
        
        vertices[0].pos = corners[0];       vertices[1].pos = corners[1];
        vertices[2].pos = corners[1];       vertices[3].pos = corners[3];
        vertices[4].pos = corners[3];       vertices[5].pos = corners[2];
        vertices[6].pos = corners[2];       vertices[7].pos = corners[0];

        vertices[8].pos = corners[6];       vertices[9].pos = corners[7];
        vertices[10].pos = corners[7];      vertices[11].pos = corners[5];
        vertices[12].pos = corners[5];      vertices[13].pos = corners[4];
        vertices[14].pos = corners[4];      vertices[15].pos = corners[6];

        vertices[16].pos = corners[3];      vertices[17].pos = corners[7];
        vertices[18].pos = corners[1];      vertices[19].pos = corners[5];
        vertices[20].pos = corners[2];      vertices[21].pos = corners[6];
        vertices[22].pos = corners[0];      vertices[23].pos = corners[4];
    }


    LOG_INFO("(init) DebugDraw initialized");
    return true;
}

void DebugDraw::Release()
{
    GfxBackend::DestroyBuffer(gDebugDraw.vertexBuffer);
    GfxBackend::DestroyPipeline(gDebugDraw.pipeline);
    GfxBackend::DestroyPipelineLayout(gDebugDraw.pipelineLayout);
    GfxBackend::DestroyBuffer(gDebugDraw.ubPerFrameData);

    GfxBackend::DestroyBuffer(gDebugDraw.textVertexBuffer);
    GfxBackend::DestroyBuffer(gDebugDraw.textIndexBuffer);
    GfxBackend::DestroyPipelineLayout(gDebugDraw.textPipelineLayout);
    GfxBackend::DestroyPipeline(gDebugDraw.textPipeline);
    GfxBackend::DestroySampler(gDebugDraw.textSampler);

    for (DebugDrawSphereCacheItem& c : gDebugDraw.sphereCache)
        Mem::Free(c.vertices);

    gDebugDraw.textVertices.Free();
    gDebugDraw.textIndices.Free();
    gDebugDraw.vertices.Free();
    gDebugDraw.sphereCache.Free();

}

void DebugDraw::DrawBoundingSphere(Float4 sphere, Color4u color, uint32 numRings, uint32 numSectors)
{
    uint32 cacheIdx = gDebugDraw.sphereCache.FindIf([numRings, numSectors](const DebugDrawSphereCacheItem& i) { return i.numSectors == numSectors && i.numRings == numRings; });

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

void DebugDraw::DrawAxisAlignedBoundingBox(AABB aabb, Color4u color)
{
    _BeginDrawItem();

    gDebugDraw.vertices.PushBatch(gDebugDraw.aabb.vertices, CountOf(gDebugDraw.aabb.vertices));

    Mat4 transformMat = Mat4::TransformMat(aabb.Center(), QUAT_INDENT, aabb.Dimensions());
    _EndDrawItem(transformMat, color);
}

void DebugDraw::DrawBox(Float3 extents, Float3 position, Quat rotation, Color4u color)
{
    _BeginDrawItem();
    gDebugDraw.vertices.PushBatch(gDebugDraw.aabb.vertices, CountOf(gDebugDraw.aabb.vertices));

    Mat4 transformMat = Mat4::TransformMat(position, rotation, extents*2);
    _EndDrawItem(transformMat, color);

}

void DebugDraw::DrawCapsule(Float3 p0, Float3 p1, float radius, Color4u color, uint32 numRings, uint32 numSectors)
{
    _BeginDrawItem();

    Float3 axisVec = p1 - p0;
    float length = Float3::Len(axisVec);
    Float3 axisDir = length > 0.0001f ? axisVec * (1.0f / length) : Float3(0, 0, 1);
    float halfHeight = length * 0.5f;

    float numSectorsRcp = 1.0f / float(numSectors);
    float numRingsRcp = 1.0f / float(numRings);

    // Top hemisphere rings: phi from 0 (pole) to PI/2 (equator), centered at z = +halfHeight
    for (uint32 i = 0; i <= numRings; i++) {
        float phi = M_HALFPI * float(i) * numRingsRcp;
        float ringRadius = radius * M::Sin(phi);
        float z = radius * M::Cos(phi) + halfHeight;

        for (uint32 j = 0; j < numSectors; j++) {
            float theta = M_PI2 * float(j) * numSectorsRcp;
            float nextTheta = M_PI2 * float(j + 1) * numSectorsRcp;

            DebugDrawVertex* v1 = gDebugDraw.vertices.Push();
            v1->pos = Float3(ringRadius * M::Cos(theta), ringRadius * M::Sin(theta), z);

            DebugDrawVertex* v2 = gDebugDraw.vertices.Push();
            v2->pos = Float3(ringRadius * M::Cos(nextTheta), ringRadius * M::Sin(nextTheta), z);

            v1->color = v2->color = COLOR4U_WHITE;
        }
    }

    // Bottom hemisphere rings: phi from PI/2 (equator) to PI (pole), centered at z = -halfHeight
    for (uint32 i = 0; i <= numRings; i++) {
        float phi = M_HALFPI + M_HALFPI * float(i) * numRingsRcp;
        float ringRadius = radius * M::Sin(phi);
        float z = radius * M::Cos(phi) - halfHeight;

        for (uint32 j = 0; j < numSectors; j++) {
            float theta = M_PI2 * float(j) * numSectorsRcp;
            float nextTheta = M_PI2 * float(j + 1) * numSectorsRcp;

            DebugDrawVertex* v1 = gDebugDraw.vertices.Push();
            v1->pos = Float3(ringRadius * M::Cos(theta), ringRadius * M::Sin(theta), z);

            DebugDrawVertex* v2 = gDebugDraw.vertices.Push();
            v2->pos = Float3(ringRadius * M::Cos(nextTheta), ringRadius * M::Sin(nextTheta), z);

            v1->color = v2->color = COLOR4U_WHITE;
        }
    }

    // Meridians: hemisphere arcs + cylinder side line
    for (uint32 j = 0; j < numSectors; j++) {
        float theta = M_PI2 * float(j) * numSectorsRcp;
        float cosTheta = M::Cos(theta);
        float sinTheta = M::Sin(theta);

        // Top hemisphere meridian
        for (uint32 i = 0; i < numRings; i++) {
            float phi1 = M_HALFPI * float(i) * numRingsRcp;
            float phi2 = M_HALFPI * float(i + 1) * numRingsRcp;

            DebugDrawVertex* v1 = gDebugDraw.vertices.Push();
            v1->pos = Float3(radius * M::Sin(phi1) * cosTheta, radius * M::Sin(phi1) * sinTheta, radius * M::Cos(phi1) + halfHeight);

            DebugDrawVertex* v2 = gDebugDraw.vertices.Push();
            v2->pos = Float3(radius * M::Sin(phi2) * cosTheta, radius * M::Sin(phi2) * sinTheta, radius * M::Cos(phi2) + halfHeight);

            v1->color = v2->color = COLOR4U_WHITE;
        }

        // Cylinder side line (between hemisphere equators)
        if (halfHeight > 0.0f) {
            DebugDrawVertex* v1 = gDebugDraw.vertices.Push();
            v1->pos = Float3(radius * cosTheta, radius * sinTheta, halfHeight);

            DebugDrawVertex* v2 = gDebugDraw.vertices.Push();
            v2->pos = Float3(radius * cosTheta, radius * sinTheta, -halfHeight);

            v1->color = v2->color = COLOR4U_WHITE;
        }

        // Bottom hemisphere meridian
        for (uint32 i = 0; i < numRings; i++) {
            float phi1 = M_HALFPI + M_HALFPI * float(i) * numRingsRcp;
            float phi2 = M_HALFPI + M_HALFPI * float(i + 1) * numRingsRcp;

            DebugDrawVertex* v1 = gDebugDraw.vertices.Push();
            v1->pos = Float3(radius * M::Sin(phi1) * cosTheta, radius * M::Sin(phi1) * sinTheta, radius * M::Cos(phi1) - halfHeight);

            DebugDrawVertex* v2 = gDebugDraw.vertices.Push();
            v2->pos = Float3(radius * M::Sin(phi2) * cosTheta, radius * M::Sin(phi2) * sinTheta, radius * M::Cos(phi2) - halfHeight);

            v1->color = v2->color = COLOR4U_WHITE;
        }
    }

    // Rotate local Z axis to align with (p1 - p0)
    Quat rotation = QUAT_INDENT;
    if (length > 0.0001f) {
        Float3 zAxis = Float3(0, 0, 1);
        float d = Float3::Dot(zAxis, axisDir);
        if (d < -0.99999f) {
            rotation = Quat::RotateAxis(Float3(1, 0, 0), M_PI);
        }
        else if (d < 0.99999f) {
            Float3 rotAxis = Float3::Norm(Float3::Cross(zAxis, axisDir));
            rotation = Quat::RotateAxis(rotAxis, M::ACos(d));
        }
    }

    Float3 center = (p0 + p1) * 0.5f;
    Mat4 transformMat = Mat4::TransformMat(center, rotation, Float3(1.0f, 1.0f, 1.0f));
    _EndDrawItem(transformMat, color);
}

void DebugDraw::SetMSAA(GfxMultiSampleCount sampleCount)
{
    gDebugDraw.msaa = sampleCount;    
}

bool DebugDraw::DrawText3D(Float3 p, float scale, const char* text, uint32 textLen, Color4u color)
{
    ASSERT(gDebugDraw.isDrawing);

    MemTempAllocator tempAlloc;
    AssetObjPtrScope<FontData> font(gDebugDraw.textFont);
    Float2 pos = MathUtil::ProjectPointToScreenPixels(p, gDebugDraw.worldToClipMat, 
                                                      RectFloat(0, 0, float(gDebugDraw.viewExtents.x), float(gDebugDraw.viewExtents.y)));
    if (pos.x >=0 && pos.y >= 0) {
        Float2 textSize = TextBuilder::CalculateTextSize(*font, scale, text, textLen);
        pos = Float2(pos.x - textSize.x*0.5f, pos.y);
        TextGeometry textGeo = TextBuilder::CreateText(*font, pos, scale, text, textLen, color, TextType::Ascii, &tempAlloc);
        ASSERT_MSG((gDebugDraw.textVertices.Count() + textGeo.numVertices)/4 <=  DEBUGDRAW_MAX_TEXT_CHARACTERS, 
                   "Too many debug text characters. Increase DEBUGDRAW_MAX_TEXT_CHARACTERS");

        gDebugDraw.textVertices.PushBatch(textGeo.vertices, textGeo.numVertices);
        gDebugDraw.textIndices.PushBatch(textGeo.indices, textGeo.numIndices);

        return true;
    }
    else {
        return false;
    }
}