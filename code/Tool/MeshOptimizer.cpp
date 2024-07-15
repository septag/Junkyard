#include "MeshOptimizer.h"

#if CONFIG_TOOLMODE

#include "../External/meshoptimizer/include/meshoptimizer.h"

#include "../Core/Allocators.h"
#include "../Core/TracyHelper.h"

static thread_local MemAllocator* gMeshOptAlloc = nullptr;

void MeshOpt::Initialize()
{
    meshopt_setAllocator(
        [](size_t size)->void* { ASSERT(gMeshOptAlloc); return Mem::Alloc(size, gMeshOptAlloc); },
        [](void* ptr) { ASSERT(gMeshOptAlloc); Mem::Free(ptr, gMeshOptAlloc); }
    );
}

void MeshOpt::Optimize(MeshOptModel* model)
{
    PROFILE_ZONE(true);

    MemTempAllocator tmpAlloc;
    gMeshOptAlloc = &tmpAlloc;

    for (uint32 i = 0; i < model->numMeshes; i++) {
        MeshOptMesh* mesh = model->meshes[i];
        uint32* indices = mesh->indexBuffer;

        meshopt_Stream* streams = tmpAlloc.MallocTyped<meshopt_Stream>(mesh->numVertexBuffers);
        for (uint32 k = 0; k < mesh->numVertexBuffers; k++) {
            streams[k] = meshopt_Stream {
                .data = mesh->vertexBuffers[k],
                .size = mesh->vertexStrides[k],
                .stride = mesh->vertexStrides[k]
            };
        }

        // Reindex/Remove duplicates
        uint32* remap = tmpAlloc.MallocTyped<uint32>(mesh->numIndices);
        size_t numVertices = meshopt_generateVertexRemapMulti(remap, indices, mesh->numIndices, 
                                                              mesh->numVertices, streams, mesh->numVertexBuffers);
        [[maybe_unused]] uint32 numDuplicates = mesh->numVertices - (uint32)numVertices;

        for (uint32 k = 0; k < mesh->numVertexBuffers; k++) {
            void* remappedVertices = tmpAlloc.Malloc(numVertices * mesh->vertexStrides[k]);
            meshopt_remapVertexBuffer(remappedVertices, mesh->vertexBuffers[k], mesh->numVertices, 
                                      mesh->vertexStrides[k], remap);
            memcpy(mesh->vertexBuffers[k], remappedVertices, numVertices * mesh->vertexStrides[k]);
        }
        meshopt_remapIndexBuffer(indices, indices, mesh->numIndices, remap);
        mesh->numVertices = uint32(numVertices);

        // Vertex cache
        meshopt_optimizeVertexCache(indices, indices, mesh->numIndices, mesh->numVertices);
        
        // Overdraw
        uint32 positionStride = mesh->posStride;
        uint8* positions = (uint8*)mesh->vertexBuffers[mesh->posBufferIndex] + mesh->posOffset;
        meshopt_optimizeOverdraw(indices, indices, mesh->numIndices, (const float*)positions, mesh->numVertices, positionStride, 1.05f);

        #if 0
        meshopt_OverdrawStatistics overdrawStats =
            meshopt_analyzeOverdraw(indices, mesh->numIndices, (const float*)positions, mesh->numVertices, positionStride);
        #endif
        
        // Fetch
        for (uint32 k = 0; k < mesh->numVertexBuffers; k++) {
            numVertices = meshopt_optimizeVertexFetchRemap(remap, indices, mesh->numIndices, mesh->numVertices);
            ASSERT(numVertices == mesh->numVertices);       // This will raise problems, should I do something about this ?
            void* remappedVertices = tmpAlloc.Malloc(numVertices * mesh->vertexStrides[k]);
            meshopt_remapVertexBuffer(remappedVertices, mesh->vertexBuffers[k], mesh->numVertices, mesh->vertexStrides[k], remap);
            memcpy(mesh->vertexBuffers[k], remappedVertices, numVertices * mesh->vertexStrides[k]);
        }  
        meshopt_remapIndexBuffer(indices, indices, mesh->numIndices, remap);
    }

    gMeshOptAlloc = nullptr;
}

#endif // CONFIG_TOOLMODE
