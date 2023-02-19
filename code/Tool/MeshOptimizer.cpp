#include "MeshOptimizer.h"

#if CONFIG_TOOLMODE

#include "../External/meshoptimizer/include/meshoptimizer.h"

#include "../Core/TracyHelper.h"

#include "../Graphics/Model.h"

static thread_local Allocator* gMeshOptAlloc = nullptr;

void _private::meshoptInitialize()
{
    meshopt_setAllocator(
        [](size_t size)->void* { ASSERT(gMeshOptAlloc); return memAlloc(size, gMeshOptAlloc); },
        [](void* ptr) { ASSERT(gMeshOptAlloc); memFree(ptr, gMeshOptAlloc); }
    );
}

void meshoptOptimizeModel(Model* model, const ModelLoadParams& modelParams)
{
    PROFILE_ZONE(true);

    MemTempAllocator tmpAlloc;
    gMeshOptAlloc = &tmpAlloc;

    for (uint32 i = 0; i < model->numMeshes; i++) {
        ModelMesh* mesh = &model->meshes[i];
        uint32* indices = mesh->cpuBuffers.indexBuffer.Get();

        meshopt_Stream* streams = tmpAlloc.MallocTyped<meshopt_Stream>(mesh->numVertexBuffers);
        for (uint32 k = 0; k < mesh->numVertexBuffers; k++) {
            streams[k] = meshopt_Stream {
                .data = mesh->cpuBuffers.vertexBuffers[k].Get(),
                .size = modelParams.layout.vertexBufferStrides[k],
                .stride = modelParams.layout.vertexBufferStrides[k]
            };
        }

        // Reindex/Remove duplicates
        uint32* remap = tmpAlloc.MallocTyped<uint32>(mesh->numIndices);
        size_t numVertices = meshopt_generateVertexRemapMulti(remap, indices, mesh->numIndices, 
                                                              mesh->numVertices, streams, mesh->numVertexBuffers);
        [[maybe_unused]] uint32 numDuplicates = mesh->numVertices - (uint32)numVertices;
        mesh->numVertices = uint32(numVertices);

        for (uint32 k = 0; k < mesh->numVertexBuffers; k++) {
            void* remappedVertices = tmpAlloc.Malloc(numVertices * modelParams.layout.vertexBufferStrides[k]);
            meshopt_remapVertexBuffer(remappedVertices, mesh->cpuBuffers.vertexBuffers[k].Get(), mesh->numVertices, 
                                      modelParams.layout.vertexBufferStrides[k], remap);
            memcpy(mesh->cpuBuffers.vertexBuffers[k].Get(), remappedVertices, numVertices * modelParams.layout.vertexBufferStrides[k]);
        }
        meshopt_remapIndexBuffer(indices, indices, mesh->numIndices, remap);

        // Vertex cache
        meshopt_optimizeVertexCache(indices, indices, mesh->numIndices, mesh->numVertices);
        
        // Overdraw
        uint32 positionStride;
        uint8* positions = modelGetVertexAttributePointer(mesh, modelParams.layout,  "POSITION", 0, &positionStride);
        ASSERT(positions);
        meshopt_optimizeOverdraw(indices, indices, mesh->numIndices, (const float*)positions, mesh->numVertices, positionStride, 1.05f);

        #if 0
        meshopt_OverdrawStatistics overdrawStats =
            meshopt_analyzeOverdraw(indices, mesh->numIndices, (const float*)positions, mesh->numVertices, positionStride);
        #endif

        // Fetch
        for (uint32 k = 0; k < mesh->numVertexBuffers; k++) {
            numVertices = meshopt_optimizeVertexFetchRemap(remap, indices, mesh->numIndices, mesh->numVertices);
            ASSERT(numVertices == mesh->numVertices);       // This will raise problems, should I do something about this ?
            void* remappedVertices = tmpAlloc.Malloc(numVertices * modelParams.layout.vertexBufferStrides[k]);
            meshopt_remapVertexBuffer(remappedVertices, mesh->cpuBuffers.vertexBuffers[k].Get(), mesh->numVertices, 
                                      modelParams.layout.vertexBufferStrides[k], remap);
            memcpy(mesh->cpuBuffers.vertexBuffers[k].Get(), remappedVertices, numVertices * modelParams.layout.vertexBufferStrides[k]);
        }  
        meshopt_remapIndexBuffer(indices, indices, mesh->numIndices, remap);
    }

    gMeshOptAlloc = nullptr;
}

#endif // CONFIG_TOOLMODE
