#pragma once

#include "../Core/Base.h"

#if CONFIG_TOOLMODE
struct MeshOptSubmesh
{
    uint32 startIndex;
    uint32 numIndices;
};

struct MeshOptMesh
{
    void** vertexBuffers;
    uint32* indexBuffer;
    uint32* vertexStrides;
    MeshOptSubmesh* submeshes;
    
    uint32 posStride;
    uint32 posBufferIndex;
    uint32 posOffset;

    uint32 numVertexBuffers;
    uint32 numVertices;
    uint32 numIndices;
    uint32 numSubmeshes;
};

struct MeshOptModel
{
    MeshOptMesh** meshes;
    uint32 numMeshes;
    bool showOverdrawAnalysis;
};

namespace MeshOpt
{
    API void Optimize(MeshOptModel* model);
    API void Initialize();
}

#endif // CONFIG_TOOLMODE

