#include "MeshOptimizer.h"

#if CONFIG_TOOLMODE

#include "../External/meshoptimizer/include/meshoptimizer.h"

#include "../Core/Allocators.h"
#include "../Core/TracyHelper.h"

#include "../Core/Log.h"

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
    MemTempAllocator tmpAlloc;
    gMeshOptAlloc = &tmpAlloc;

    for (uint32 i = 0; i < model->numMeshes; i++) {
        MeshOptMesh* mesh = model->meshes[i];
        uint32* meshIndices = mesh->indexBuffer;

        meshopt_Stream* streams = tmpAlloc.MallocTyped<meshopt_Stream>(mesh->numVertexBuffers);
        void** vertices = tmpAlloc.MallocTyped<void*>(mesh->numVertexBuffers);
        for (uint32 k = 0; k < mesh->numVertexBuffers; k++) {
            streams[k] = {
                .data = mesh->vertexBuffers[k],
                .size = mesh->vertexStrides[k],
                .stride = mesh->vertexStrides[k]
            };

            vertices[k] = tmpAlloc.Malloc(mesh->numVertices * mesh->vertexStrides[k]);
        }

        uint32* remap = tmpAlloc.MallocTyped<uint32>(mesh->numVertices);
        uint32* indices = tmpAlloc.MallocTyped<uint32>(mesh->numIndices);

        for (uint32 k = 0; k < mesh->numVertexBuffers; k++) 
            memcpy(vertices[k], mesh->vertexBuffers[k], mesh->vertexStrides[k]*mesh->numVertices);

        // TODO: This part is buggy and very unpredictable if enabled. investigate why
        #if 0
        if (mesh->numSubmeshes == 1) {
            size_t numVertices = meshopt_generateVertexRemapMulti(remap, meshIndices, mesh->numIndices, mesh->numVertices, streams, mesh->numVertexBuffers);
            for (uint32 k = 0; k < mesh->numVertexBuffers; k++) 
                meshopt_remapVertexBuffer(vertices[k], mesh->vertexBuffers[k], numVertices, mesh->vertexStrides[k], remap);
            meshopt_remapIndexBuffer(indices, meshIndices, mesh->numIndices, remap);
            mesh->numVertices = uint32(numVertices);
        }
        else {
            for (uint32 k = 0; k < mesh->numVertexBuffers; k++) 
                memcpy(vertices[k], mesh->vertexBuffers[k], mesh->vertexStrides[k]*mesh->numVertices);
            memcpy(indices, mesh->indexBuffer, sizeof(uint32)*mesh->numIndices);
        }
        #endif

        for (uint32 submeshIdx = 0; submeshIdx < mesh->numSubmeshes; submeshIdx++) {
            MeshOptSubmesh submesh = mesh->submeshes[submeshIdx];

            // Vertex cache
            meshopt_optimizeVertexCache(indices + submesh.startIndex, mesh->indexBuffer + submesh.startIndex, submesh.numIndices, mesh->numVertices);

            // Overdraw
            uint32 positionStride = mesh->posStride;
            uint8* positions = (uint8*)vertices[mesh->posBufferIndex] + mesh->posOffset;
            meshopt_optimizeOverdraw(indices + submesh.startIndex, indices + submesh.startIndex, 
                                     submesh.numIndices, (const float*)positions, mesh->numVertices, positionStride, 1.05f);

            if (model->showOverdrawAnalysis) {
                [[maybe_unused]] meshopt_OverdrawStatistics overdrawStats =
                    meshopt_analyzeOverdraw(indices + submesh.startIndex, submesh.numIndices, (const float*)positions, mesh->numVertices, positionStride);
                LOG_INFO("PixelsCovered: %u, Overdraw: %.1f", overdrawStats.pixels_covered, overdrawStats.overdraw);
            }
        }

        // Fetch
        meshopt_optimizeVertexFetchRemap(remap, indices, mesh->numIndices, mesh->numVertices);
        for (uint32 k = 0; k < mesh->numVertexBuffers; k++)
            meshopt_remapVertexBuffer(mesh->vertexBuffers[k], vertices[k], mesh->numVertices, mesh->vertexStrides[k], remap);
        meshopt_remapIndexBuffer(meshIndices, indices, mesh->numIndices, remap);
    }

    gMeshOptAlloc = nullptr;
}

#endif // CONFIG_TOOLMODE
