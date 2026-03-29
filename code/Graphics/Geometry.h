#pragma once

#include "../Core/Base.h"
#include "GfxBackend.h"

inline constexpr uint32 GEOMETRY_MAX_VERTEX_ATTRIBUTES = 8;
inline constexpr uint32 GEOMETRY_MAX_VERTEX_BUFFERS_PER_SHADER = 4;

struct GeometryCpuBuffers
{
    uint8* vertexBuffers[GEOMETRY_MAX_VERTEX_BUFFERS_PER_SHADER];
    uint8* indexBuffer;
    uint64 vertexBufferSizes[GEOMETRY_MAX_VERTEX_BUFFERS_PER_SHADER];
    uint64 indexBufferSize;
};

struct GeometryData
{
    MemAllocator* alloc;
    GeometryCpuBuffers cpuBuffers;
    uint32 numVertices;
    uint32 numIndices;
    uint32 numVertexBuffers;
    GfxBufferHandle vertexBuffers[GEOMETRY_MAX_VERTEX_BUFFERS_PER_SHADER];
    GfxBufferHandle indexBuffer;
    bool firstUpdate;
};

// Must be 0 initialized
struct GeometryVertexLayout 
{
    GfxVertexInputAttributeDesc vertexAttributes[GEOMETRY_MAX_VERTEX_ATTRIBUTES];
    uint32 vertexBufferStrides[GEOMETRY_MAX_VERTEX_BUFFERS_PER_SHADER]; // if =0 , then we can stop counting the number of buffers

    bool HasTangents() const;
    const GfxVertexInputAttributeDesc* FindAttribute(const char* semantic, uint32 semanticIdx) const;
    bool HasAttribute(const char* semantic, uint32 semanticIdx) const;

    // Returns the pointer to the vertex-buffer based on the semantic. Also returns the selected vertex stride
    uint8* GetVertexAttributePointer(uint64 vertexBufferOffsets[GEOMETRY_MAX_VERTEX_BUFFERS_PER_SHADER], 
                                     GeometryCpuBuffers& cpuBuffers, 
                                     const char* semantic, uint32 semanticIdx, 
                                     uint32& outVertexStride) const;
};

namespace Geometry
{
    uint32 GetVertexStride(GfxFormat fmt);

    void CreateAxisAlignedBox(Float3 extents, const GeometryVertexLayout& layout, GeometryData& outBox, MemAllocator* alloc = Mem::GetDefaultAlloc());
    void CreatePlane(Float2 extents, const GeometryVertexLayout& layout, GeometryData& outPlane, MemAllocator* alloc = Mem::GetDefaultAlloc());

    void Destroy(GeometryData& geo);
    void UpdateGpuBuffers(GeometryData& geo, GfxCommandBuffer cmd);
}

