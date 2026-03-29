#include "Geometry.h"

#include "../Core/MathAll.h"

namespace GeometryUtils
{
    template <typename _T> _T& PtrToElement(uint8* ptr, uint32 stride, uint32 index) { return *((_T*)(ptr + stride*index)); }

    static uint32 CreateVertexBuffers(const GeometryVertexLayout& layout, GeometryData& geo, MemAllocator* alloc)
    {
        uint32 numVertexBuffers = 0;
        for (uint32 i = 0; i < CountOf(layout.vertexBufferStrides); i++) {
            if (layout.vertexBufferStrides[i] == 0)
                break;

            GfxBufferDesc bufferDesc {
                .sizeBytes = 24*layout.vertexBufferStrides[i],
                .usageFlags = GfxBufferUsageFlags::Vertex | GfxBufferUsageFlags::TransferDst,
                .arena = GfxMemoryArena::DynamicBufferGPU
            };
            geo.vertexBuffers[i] = GfxBackend::CreateBuffer(bufferDesc);

            geo.cpuBuffers.vertexBuffers[i] = (uint8*)Mem::Alloc(bufferDesc.sizeBytes, alloc);
            geo.cpuBuffers.vertexBufferSizes[i] = (uint32)bufferDesc.sizeBytes;
            numVertexBuffers++;
        }
    
        {
            GfxBufferDesc bufferDesc {
                .sizeBytes = 36*sizeof(uint32),
                .usageFlags = GfxBufferUsageFlags::Index | GfxBufferUsageFlags::TransferDst,
                .arena = GfxMemoryArena::DynamicBufferGPU
            };
            geo.indexBuffer = GfxBackend::CreateBuffer(bufferDesc);

            geo.cpuBuffers.indexBuffer = (uint8*)Mem::AllocTyped<uint32>(36, alloc);
            geo.cpuBuffers.indexBufferSize = (uint32)bufferDesc.sizeBytes;
        }

        ASSERT(numVertexBuffers);
        return numVertexBuffers;
    }
};

bool GeometryVertexLayout::HasTangents() const
{
    const GfxVertexInputAttributeDesc* attr = &this->vertexAttributes[0];
    while (!attr->semantic.IsEmpty()) {
        if (attr->semantic == "TANGENT")
            return true;
        ++attr;
    }
    return false;
}

const GfxVertexInputAttributeDesc* GeometryVertexLayout::FindAttribute(const char* semantic, uint32 semanticIdx) const
{
    const GfxVertexInputAttributeDesc* attr = &this->vertexAttributes[0];
    while (!attr->semantic.IsEmpty()) {
        if (attr->semantic == semantic && attr->semanticIdx == semanticIdx)
            return attr;
        ++attr;
    }
    return nullptr;
}

bool GeometryVertexLayout::HasAttribute(const char* semantic, uint32 semanticIdx) const
{
    const GfxVertexInputAttributeDesc* attr = &this->vertexAttributes[0];
    while (!attr->semantic.IsEmpty()) {
        if (attr->semantic == semantic && attr->semanticIdx == semanticIdx)
            return true;
        ++attr;
    }
    return false;
}

uint8* GeometryVertexLayout::GetVertexAttributePointer(uint64 vertexBufferOffsets[GEOMETRY_MAX_VERTEX_BUFFERS_PER_SHADER], 
                                                       GeometryCpuBuffers& cpuBuffers, 
                                                       const char* semantic, uint32 semanticIdx, 
                                                       uint32& outVertexStride) const
{
    const GfxVertexInputAttributeDesc* attr = &this->vertexAttributes[0];
    
    while (!attr->semantic.IsEmpty()) {
        if (attr->semantic == semantic && attr->semanticIdx == semanticIdx) {
            outVertexStride = this->vertexBufferStrides[attr->binding];
            uint8* dstBuff = cpuBuffers.vertexBuffers[attr->binding] + vertexBufferOffsets[attr->binding];
            return dstBuff + attr->offset;
        }
        ++attr;
    }

    return nullptr;
}

uint32 Geometry::GetVertexStride(GfxFormat fmt)
{
    switch (fmt) {
    case GfxFormat::R32_SFLOAT:
        return sizeof(float);
    case GfxFormat::R32G32_SFLOAT:
        return sizeof(float)*2;
    case GfxFormat::R32G32B32_SFLOAT:
        return sizeof(float)*3;
    case GfxFormat::R32G32B32A32_SFLOAT:
        return sizeof(float)*4;
    case GfxFormat::R8G8B8A8_SINT:
    case GfxFormat::R8G8B8A8_SNORM:
    case GfxFormat::R8G8B8A8_UINT:
    case GfxFormat::R8G8B8A8_UNORM:
        return sizeof(uint8)*4;
    case GfxFormat::R16G16_SINT: 
    case GfxFormat::R16G16_UNORM:
    case GfxFormat::R16G16_SNORM:
    case GfxFormat::R16G16_UINT:
        return sizeof(uint16)*2;
    case GfxFormat::R16G16B16A16_SNORM:
    case GfxFormat::R16G16B16A16_UNORM:
    case GfxFormat::R16G16B16A16_SINT:
    case GfxFormat::R16G16B16A16_UINT:
        return sizeof(uint16)*4;
    default:
        return 0;
    }
}

void Geometry::CreateAxisAlignedBox(Float3 extents, const GeometryVertexLayout& layout, GeometryData& outBox, MemAllocator* alloc)
{
    ASSERT(alloc);
    memset(&outBox, 0x0, sizeof(outBox));

    auto MakeFace = [](uint8* positions, uint8* normals, uint8* uvs, uint32* indices, 
        uint32 startVertex, uint32 positionStride, uint32 normalsStride, uint32 uvStride,
        const Float3& a, const Float3& b, const Float3& c, const Float3& d, Float3 normal)
    {
        positions += positionStride*startVertex;
        normals += normalsStride*startVertex;
        uvs += uvStride*startVertex;

        // 4 Vertices per face
        GeometryUtils::PtrToElement<Float3>(positions, positionStride, 0) = a;
        GeometryUtils::PtrToElement<Float3>(positions, positionStride, 1) = b;
        GeometryUtils::PtrToElement<Float3>(positions, positionStride, 2) = c;
        GeometryUtils::PtrToElement<Float3>(positions, positionStride, 3) = d;

        GeometryUtils::PtrToElement<Float2>(uvs, uvStride, 0) = Float2(0, 0);
        GeometryUtils::PtrToElement<Float2>(uvs, uvStride, 1) = Float2(1, 0);
        GeometryUtils::PtrToElement<Float2>(uvs, uvStride, 2) = Float2(1, 1);
        GeometryUtils::PtrToElement<Float2>(uvs, uvStride, 3) = Float2(0, 1);

        GeometryUtils::PtrToElement<Float3>(normals, normalsStride, 0) = normal;
        GeometryUtils::PtrToElement<Float3>(normals, normalsStride, 1) = normal;
        GeometryUtils::PtrToElement<Float3>(normals, normalsStride, 2) = normal;
        GeometryUtils::PtrToElement<Float3>(normals, normalsStride, 3) = normal;

        // 2 triangles (CCW)
        indices[0] = startVertex;
        indices[1] = startVertex + 1;
        indices[2] = startVertex + 2;

        indices[3] = startVertex;
        indices[4] = startVertex + 2;
        indices[5] = startVertex + 3;
    };

    Float3 corners[8];
    AABB aabb = AABB::CenterExtents(FLOAT3_ZERO, extents);
    AABB::GetCorners(aabb, corners);

    uint32 numVertexBuffers = GeometryUtils::CreateVertexBuffers(layout, outBox, alloc);

    uint64 vertexBufferOffsets[GEOMETRY_MAX_VERTEX_BUFFERS_PER_SHADER] = {0, 0, 0, 0};
    uint32 posStride, uvStride, normalStride;
    uint8* posPtr = layout.GetVertexAttributePointer(vertexBufferOffsets, outBox.cpuBuffers, "POSITION", 0, posStride);
    uint8* uvPtr = layout.GetVertexAttributePointer(vertexBufferOffsets, outBox.cpuBuffers, "TEXCOORD", 0, uvStride);
    uint8* normalPtr = layout.GetVertexAttributePointer(vertexBufferOffsets, outBox.cpuBuffers, "NORMAL", 0, normalStride);
    ASSERT(posPtr);
    ASSERT(uvPtr);
    ASSERT(normalPtr);
    uint32* indices = (uint32*)outBox.cpuBuffers.indexBuffer;
    uint32 numVertices = 0;
    uint32 numIndices = 0;

    // +X
    MakeFace(posPtr, normalPtr, uvPtr, &indices[numIndices], numVertices, posStride, normalStride, uvStride,
             corners[1], corners[5], corners[7], corners[3], 
             Float3(1, 0, 0));
    numVertices += 4;
    numIndices += 6;

    // -X
    MakeFace(posPtr, normalPtr, uvPtr, &indices[numIndices], numVertices, posStride, normalStride, uvStride,
             corners[4], corners[0], corners[2], corners[6],
             Float3(-1, 0, 0));
    numVertices += 4;
    numIndices += 6;

    // +Y 
    MakeFace(posPtr, normalPtr, uvPtr, &indices[numIndices], numVertices, posStride, normalStride, uvStride,
             corners[5], corners[4], corners[6], corners[7],
             Float3(0, 1, 0));
    numVertices += 4;
    numIndices += 6;

    // -Y
    MakeFace(posPtr, normalPtr, uvPtr, &indices[numIndices], numVertices, posStride, normalStride, uvStride,
             corners[0], corners[1], corners[3], corners[2],
             Float3(0, -1, 0));
    numVertices += 4;
    numIndices += 6;

    // +Z
    MakeFace(posPtr, normalPtr, uvPtr, &indices[numIndices], numVertices, posStride, normalStride, uvStride,
             corners[2], corners[3], corners[7], corners[6],
             Float3(0, 0, 1));
    numVertices += 4;
    numIndices += 6;

    // -Z
    MakeFace(posPtr, normalPtr, uvPtr, &indices[numIndices], numVertices, posStride, normalStride, uvStride,
             corners[0], corners[4], corners[5], corners[1],
             Float3(0, 0, -1));
    numVertices += 4;
    numIndices += 6;

    ASSERT(numVertices == 24);
    ASSERT(numIndices == 36);

    outBox.alloc = alloc;
    outBox.numVertices = numVertices;
    outBox.numIndices = numIndices; 
    outBox.numVertexBuffers = numVertexBuffers;
}

void Geometry::CreatePlane(Float2 extents, const GeometryVertexLayout& layout, GeometryData& outPlane, MemAllocator* alloc)
{
    ASSERT(alloc);
    memset(&outPlane, 0x0, sizeof(outPlane));

    uint32 numVertexBuffers = GeometryUtils::CreateVertexBuffers(layout, outPlane, alloc);
    uint64 vertexBufferOffsets[GEOMETRY_MAX_VERTEX_BUFFERS_PER_SHADER] = {0, 0, 0, 0};
    uint32 positionStride, uvStride, normalsStride;
    uint8* positions = layout.GetVertexAttributePointer(vertexBufferOffsets, outPlane.cpuBuffers, "POSITION", 0, positionStride);
    uint8* uvs = layout.GetVertexAttributePointer(vertexBufferOffsets, outPlane.cpuBuffers, "TEXCOORD", 0, uvStride);
    uint8* normals = layout.GetVertexAttributePointer(vertexBufferOffsets, outPlane.cpuBuffers, "NORMAL", 0, normalsStride);
    ASSERT(positions);
    ASSERT(uvs);
    ASSERT(normals);
    uint32* indices = (uint32*)outPlane.cpuBuffers.indexBuffer;

    GeometryUtils::PtrToElement<Float3>(positions, positionStride, 0) = Float3(-extents.x, -extents.y, 0);
    GeometryUtils::PtrToElement<Float3>(positions, positionStride, 1) = Float3(extents.x, -extents.y, 0);
    GeometryUtils::PtrToElement<Float3>(positions, positionStride, 2) = Float3(extents.x, extents.y, 0);
    GeometryUtils::PtrToElement<Float3>(positions, positionStride, 3) = Float3(-extents.x, extents.y, 0);

    GeometryUtils::PtrToElement<Float2>(uvs, uvStride, 0) = Float2(0, 0);
    GeometryUtils::PtrToElement<Float2>(uvs, uvStride, 1) = Float2(1, 0);
    GeometryUtils::PtrToElement<Float2>(uvs, uvStride, 2) = Float2(1, 1);
    GeometryUtils::PtrToElement<Float2>(uvs, uvStride, 3) = Float2(0, 1);

    GeometryUtils::PtrToElement<Float3>(normals, normalsStride, 0) = FLOAT3_UNITZ;
    GeometryUtils::PtrToElement<Float3>(normals, normalsStride, 1) = FLOAT3_UNITZ;
    GeometryUtils::PtrToElement<Float3>(normals, normalsStride, 2) = FLOAT3_UNITZ;
    GeometryUtils::PtrToElement<Float3>(normals, normalsStride, 3) = FLOAT3_UNITZ;

    // 2 triangles (CCW)
    indices[0] = 0;
    indices[1] = 1;
    indices[2] = 2;

    indices[3] = 0;
    indices[4] = 2;
    indices[5] = 3;

    outPlane.alloc = alloc;
    outPlane.numVertices = 4;
    outPlane.numIndices = 6;
    outPlane.numVertexBuffers = numVertexBuffers;
}

void Geometry::Destroy(GeometryData& geo)
{
    if (!geo.alloc)
        return;

    for (uint32 i = 0; i < geo.numVertexBuffers; i++) {
        Mem::Free(geo.cpuBuffers.vertexBuffers[i], geo.alloc);
        GfxBackend::DestroyBuffer(geo.vertexBuffers[i]);
    }

    Mem::Free(geo.cpuBuffers.indexBuffer, geo.alloc);
    GfxBackend::DestroyBuffer(geo.indexBuffer);

    memset(&geo, 0x0, sizeof(geo));
}

void Geometry::UpdateGpuBuffers(GeometryData& geo, GfxCommandBuffer cmd)
{
    ASSERT(geo.numVertexBuffers);
    for (uint32 i = 0; i < geo.numVertexBuffers; i++) {
        GfxHelperBufferUpdateScope bufferUpdater(cmd, geo.vertexBuffers[i], uint32(geo.cpuBuffers.vertexBufferSizes[i]), GfxShaderStage::Vertex);
        memcpy(bufferUpdater.mData, geo.cpuBuffers.vertexBuffers[i], geo.cpuBuffers.vertexBufferSizes[i]);
    }

    {
        GfxHelperBufferUpdateScope bufferUpdater(cmd, geo.indexBuffer, uint32(geo.cpuBuffers.indexBufferSize), GfxShaderStage::Vertex);
        memcpy(bufferUpdater.mData, geo.cpuBuffers.indexBuffer, geo.cpuBuffers.indexBufferSize);
    }

    geo.firstUpdate = true;
}
