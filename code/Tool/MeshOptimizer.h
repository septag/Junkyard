#pragma once

#include "../Core/Base.h"

#if CONFIG_TOOLMODE
struct MeshOptMesh
{
    void** vertexBuffers;
    uint32* indexBuffer;
    uint32* vertexStrides;
    
    uint32 posStride;
    uint32 posBufferIndex;
    uint32 posOffset;

    uint32 numVertexBuffers;
    uint32 numVertices;
    uint32 numIndices;
};

struct MeshOptModel
{
    MeshOptMesh** meshes;
    uint32 numMeshes;
};

void meshoptOptimizeModel(MeshOptModel* model);

namespace _private
{
    void meshoptInitialize();
}

#endif // CONFIG_TOOLMODE

