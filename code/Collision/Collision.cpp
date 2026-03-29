#include "Collision.h"

#include "../Core/Arrays.h"
#include "../Core/MathAll.h"
#include "../Core/Hash.h"
#include "../Core/BlitSort.h"
#include "../Engine.h"
#include "../Common/Application.h"

#if CONFIG_DEBUG_COLLISIONS
#include "../ImGui/ImGuiMain.h"
#endif

#define CUTE_C2_IMPLEMENTATION
PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4456)    
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4505)
#include "../External/cute_headers/cute_c2.h"
PRAGMA_DIAGNOSTIC_POP()

static inline constexpr size_t COLLISION_ISLAND_POOL_SIZE = SIZE_MB;

DEFINE_HANDLE(CollisionShapeHandle);

struct CollisionSpatialGridCell
{
    Int2 posGS;         // Grid-space
    Float2 centerWS;    // World-space
    Array<CollisionShapeHandle> shapes;
};

struct CollisionIslandData
{
    HandlePool<CollisionShapeHandle, CollisionShapeData> shapes;
    HashTable<CollisionShapeHandle> idToShapeMap;
    Array<CollisionShapeHandle> updatedShapes;
    CollisionSpatialGridCell* cells;

    RectFloat mapRect;
    float cellSize;
    uint32 numCellsX;
    uint32 numCellsY;
    uint32 numCells;

    inline uint32 GetCellIndex(int x, int y) { return uint32(y*numCellsX + x); }
};

struct CollisionContext
{
    MemProxyAllocator alloc;
    MemProxyAllocator islandAlloc;

    MemTlsfAllocator islandAllocBase;

    HandlePool<CollisionIslandHandle, CollisionIslandData*> islands;
};

static CollisionContext gCollision;

namespace Collision
{
    INLINE Int2 HashPoint(const CollisionIslandData* island, Float2 pt)
    {
        float cellSize = island->cellSize;
        float hx = pt.x/cellSize + (float)island->numCellsX*0.5f;
        float hy = pt.y/cellSize + (float)island->numCellsY*0.5f;

        return Int2(Clamp<int>(int(hx), 0, int(island->numCellsX - 1)), 
                    Clamp<int>(int(hy), 0, int(island->numCellsY - 1)));
    }

    INLINE void CalculatePolyFromBox(const CollisionShapeBox& box, c2Poly* outPoly, c2x* outTransform)
    {
        const float aa = 0.70710678f;

        outPoly->count = 4;
        outPoly->verts[0] = { box.extents.x, box.extents.y };
        outPoly->norms[0] = { aa, aa };
        outPoly->verts[1] = { -box.extents.x, box.extents.y };
        outPoly->norms[1] = { -aa, aa };
        outPoly->verts[2] = { -box.extents.x, -box.extents.y };
        outPoly->norms[2] = { -aa, -aa };
        outPoly->verts[3] = { box.extents.x, -box.extents.y };
        outPoly->norms[3] = { aa, -aa };

        // From Quat::ToEuler
        Quat q = box.transform.rotation;
        outTransform->r.c = 1 - 2 * (q.x * q.x + q.y * q.y);
        outTransform->r.s = 2 * (q.w * q.x + q.y * q.z);
        outTransform->p.x = box.transform.position.x;
        outTransform->p.y = box.transform.position.y;
    }

    INLINE bool CheckWithExistingPairs(const CollisionPair* pairs, uint32 numPairs, const CollisionPair testPair)
    {
        for (uint32 i = 0; i < numPairs; i++) {
            if ((pairs[i].entity1 == testPair.entity1 && pairs[i].entity2 == testPair.entity2) ||
                (pairs[i].entity2 == testPair.entity1 && pairs[i].entity1 == testPair.entity2))
            {
                return true;
            }
        }

        return false;
    }

#if CONFIG_DEBUG_COLLISIONS
    static void BoxToVertices(const CollisionShapeBox& box, Float3 outVertices[4])
    {
        outVertices[0] = Quat::TransformFloat3(Float3(box.extents.x, box.extents.y, 0), box.transform.rotation) + box.transform.position;
        outVertices[1] = Quat::TransformFloat3(Float3(-box.extents.x, box.extents.y, 0), box.transform.rotation) + box.transform.position;
        outVertices[2] = Quat::TransformFloat3(Float3(-box.extents.x, -box.extents.y, 0), box.transform.rotation) + box.transform.position;
        outVertices[3] = Quat::TransformFloat3(Float3(box.extents.x, -box.extents.y, 0), box.transform.rotation) + box.transform.position;
    }
#endif  // CONFIG_DEBUG_COLLISIONS

} // Collision 

void CollisionIsland::AddBoxes(uint32 numBoxes, const CollisionAddBoxDesc* boxes)
{
    CollisionIslandData* data = gCollision.islands.Data(mHandle);

    for (uint32 i = 0; i < numBoxes; i++) {
        const CollisionAddBoxDesc& boxDesc = boxes[i];
        CollisionShapeData shape {
            .type = CollisionShapeType::Box,
            .maskPair = {
                .id = boxDesc.id,
                .mask = boxDesc.mask
            },
            .box = boxDesc.shape,
        };

        ASSERT(Float3::Dot(boxes[i].shape.extents, Float3(1, 1, 1)) > 0);

        // AABB
        Float3 position = boxDesc.shape.transform.position;
        Mat3 rotMat = Mat3::FromQuat(boxDesc.shape.transform.rotation);
        Float3 aabbExtents = Mat3::MulFloat3(Mat3::Abs(rotMat), boxDesc.shape.extents);
        shape.aabb = AABB(position - aabbExtents, position + aabbExtents);

        // Transformed shapes
        Mat4 transformedMat = Mat4::Mul(Mat4::FromQuat(boxDesc.transform.rotation), Mat4::Translate(boxDesc.transform.position));
        shape.transformedAABB = AABB::Transform(shape.aabb, transformedMat);
        
        Float3 transformedBoxPos = Quat::TransformFloat3(boxDesc.shape.transform.position, boxDesc.transform.rotation) + 
            boxDesc.transform.position;
        Quat transformedBoxQuat = Quat::Mul(boxDesc.transform.rotation, boxDesc.shape.transform.rotation);
        shape.transformedBox = {
            .transform = {
                .position = transformedBoxPos,
                .rotation = transformedBoxQuat
            },
            .extents = boxDesc.shape.extents
        };

        CollisionShapeHandle handle = data->shapes.Add(shape);

        // Push AABB to spatial grid
        Int2 minHashed = Collision::HashPoint(data, Float2(shape.transformedAABB.xmin, shape.transformedAABB.ymin));
        Int2 maxHashed = Collision::HashPoint(data, Float2(shape.transformedAABB.xmax, shape.transformedAABB.ymax));
        for (int y = minHashed.y; y <= maxHashed.y; y++) {
            for (int x = minHashed.x; x <= maxHashed.x; x++) {
                data->cells[data->GetCellIndex(x, y)].shapes.Push(handle);
            }            
        }

        // Save external Id -> handle mapping
        static_assert(sizeof(boxDesc.id) == sizeof(uint32), "Id size should be 4");
        ASSERT_MSG(data->idToShapeMap.Find(boxDesc.id) == -1, "Specified shape with Id=%u already added", boxDesc.id);
        data->idToShapeMap.Add(boxDesc.id, handle);

        data->updatedShapes.Push(handle);
    }
}

void CollisionIsland::AddBox(const CollisionAddBoxDesc& box)
{
    AddBoxes(1, &box);
}

void CollisionIsland::AddStaticPolys(uint32 numPolys, const CollisionAddPolyDesc* polys)
{
    CollisionIslandData* data = gCollision.islands.Data(mHandle);

    for (uint32 i = 0; i < numPolys; i++) {
        const CollisionAddPolyDesc& polyDesc = polys[i];
        CollisionShapeData shape {
            .type = CollisionShapeType::StaticPoly,
            .maskPair = {
                .id = polyDesc.id,
                .mask = polyDesc.mask
            },
            .polygon = polyDesc.shape,
        };

        // AABB
        AABB aabb = AABB_EMPTY;
        for (uint32 k = 0; k < polyDesc.shape.numVertices; k++) {
            AABB::AddPoint(aabb, Float3(polyDesc.shape.vertices[k].x, polyDesc.shape.vertices[k].y, 0));
        }

        CollisionShapeHandle handle = data->shapes.Add(shape);

        // Push AABB to spatial grid
        Int2 minHashed = Collision::HashPoint(data, Float2(shape.transformedAABB.xmin, shape.transformedAABB.ymin));
        Int2 maxHashed = Collision::HashPoint(data, Float2(shape.transformedAABB.xmax, shape.transformedAABB.ymax));
        for (int y = minHashed.y; y <= maxHashed.y; y++) {
            for (int x = minHashed.x; x <= maxHashed.x; x++) {
                data->cells[data->GetCellIndex(x, y)].shapes.Push(handle);
            }            
        }

        // Save external Id -> handle mapping
        static_assert(sizeof(polyDesc.id) == sizeof(uint32), "Id size should be 4");
        ASSERT_MSG(data->idToShapeMap.Find(polyDesc.id) == -1, "Specified shape with Id=%u already added", polyDesc.id);
        data->idToShapeMap.Add(polyDesc.id, handle);
    }
}

void CollisionIsland::AddStaticPoly(const CollisionAddPolyDesc& poly)
{
    AddStaticPolys(1, &poly);
}

void CollisionIsland::Remove(uint32 numEntities, const CollisionEntityId* ids)
{
    CollisionIslandData* data = gCollision.islands.Data(mHandle);
    for (uint32 i = 0; i < numEntities; i++) {
        CollisionEntityId id = ids[i];

        uint32 index = data->idToShapeMap.Find(id);
        if (index == -1) {
            ASSERT_MSG(0, "Entity with Id=%u does not exist or already removed", id);
            continue;
        }

        CollisionShapeHandle handle = data->idToShapeMap.Get(index);
        CollisionShapeData& shape = data->shapes.Data(handle);

        // Remove from spatial grid
        Int2 minHashed = Collision::HashPoint(data, Float2(shape.aabb.xmin, shape.aabb.ymin));
        Int2 maxHashed = Collision::HashPoint(data, Float2(shape.aabb.xmax, shape.aabb.ymax));

        for (int y = minHashed.y; y <= maxHashed.y; y++) {
            for (int x = minHashed.x; x <= maxHashed.x; x++) {
                CollisionSpatialGridCell& cell = data->cells[data->GetCellIndex(x, y)];
                for (uint32 k = 0; k < cell.shapes.Count(); k++) {
                    if (cell.shapes[k] == handle) {
                        cell.shapes.Pop(k);
                        break;
                    }
                }
            }
        }

        //
        data->idToShapeMap.Remove(index);
        data->shapes.Remove(handle);
    }
}

void CollisionIsland::Remove(CollisionEntityId id)
{
    Remove(1, &id);
}

void CollisionIsland::RemoveAll()
{
    CollisionIslandData* data = gCollision.islands.Data(mHandle);

    for (uint32 i = 0; i < data->numCells; i++) {
        CollisionSpatialGridCell& cell = data->cells[i];
        cell.shapes.Clear();
    }

    data->idToShapeMap.Clear();
    data->shapes.Clear();
}

void CollisionIsland::UpdateTransforms(uint32 numEntities, const CollisionEntityId* ids, const CollisionTransform* transforms)
{
    CollisionIslandData* data = gCollision.islands.Data(mHandle);

    for (uint32 i = 0; i < numEntities; i++) {
        CollisionShapeHandle handle = data->idToShapeMap.FindAndFetch(ids[i], CollisionShapeHandle());
        ASSERT_MSG(handle.IsValid(), "Entity id '%u' not found", ids[i]);

        CollisionShapeData& shape = data->shapes.Data(handle);
        
        AABB prevAABB = shape.transformedAABB;
        Int2 prevMinHashed = Collision::HashPoint(data, Float2(prevAABB.xmin, prevAABB.ymin));
        Int2 prevMaxHashed = Collision::HashPoint(data, Float2(prevAABB.xmax, prevAABB.ymax));
        RectInt prevArea = RectInt(prevMinHashed, prevMaxHashed);

        Mat4 transformMat = Mat4::Mul(Mat4::FromQuat(transforms[i].rotation), Mat4::Translate(transforms[i].position));
        AABB aabb = AABB::Transform(shape.aabb, transformMat);
        Int2 minHashed = Collision::HashPoint(data, Float2(aabb.xmin, aabb.ymin));
        Int2 maxHashed = Collision::HashPoint(data, Float2(aabb.xmax, aabb.ymax));
        RectInt area = RectInt(minHashed, maxHashed);

        shape.transformedAABB = aabb;
        Float3 transformedBoxPos = Quat::TransformFloat3(transforms[i].position, transforms[i].rotation) + transforms[i].position;
        Quat transformedBoxQuat = Quat::Mul(transforms[i].rotation, transforms[i].rotation);
        shape.transformedBox = {
            .transform = {
                .position = transformedBoxPos,
                .rotation = transformedBoxQuat
            },
            .extents = shape.transformedBox.extents
        };

        // Remove from old cells (if not collide with the new cells)
        for (int y = prevMinHashed.y; y <= prevMaxHashed.y; y++) {
            for (int x = prevMinHashed.x; x <= prevMaxHashed.x; x++) {
                if (!RectInt::TestPoint(area, Int2(x, y))) {
                    CollisionSpatialGridCell& cell = data->cells[data->GetCellIndex(x, y)];
                    uint32 shapeIdx = cell.shapes.Find(handle);
                    if (shapeIdx != -1) 
                        cell.shapes.Pop(shapeIdx);
                }
            }
        }

        // Add to new cells (if not collide with old cells)
        for (int y = minHashed.y; y <= maxHashed.y; y++) {
            for (int x = minHashed.x; x <= maxHashed.x; x++) {
                if (!RectInt::TestPoint(prevArea, Int2(x, y))) {
                    CollisionSpatialGridCell& cell = data->cells[data->GetCellIndex(x, y)];
                    cell.shapes.Push(handle);
                }
            }
        }

        data->updatedShapes.Push(handle);
    }
}

void CollisionIsland::UpdateTransform(CollisionEntityId id, const CollisionTransform& transform)
{
    UpdateTransforms(1, &id, &transform);
}

Span<CollisionPair> CollisionIsland::DetectCollisions(MemAllocator* alloc)
{
    CollisionIslandData* data = gCollision.islands.Data(mHandle);

    DEFINE_SAFE_TEMP_ALLOCATOR(tempAlloc, alloc);
    Array<CollisionShapeHandle> candidates(&tempAlloc);
    Array<CollisionPair> pairs(&tempAlloc);

    for (CollisionShapeHandle handle : data->updatedShapes) {
        CollisionShapeData& shape = data->shapes.Data(handle);

        ASSERT_MSG(shape.type != CollisionShapeType::StaticPoly, "Static shapes transforms should not be updated");

        // Broad-phase
        Int2 minHashed = Collision::HashPoint(data, Float2(shape.transformedAABB.xmin, shape.transformedAABB.ymin));
        Int2 maxHashed = Collision::HashPoint(data, Float2(shape.transformedAABB.xmax, shape.transformedAABB.ymax));

        for (int y = minHashed.y; y <= maxHashed.y; y++) {
            for (int x = minHashed.x; x <= maxHashed.x; x++) {
                CollisionSpatialGridCell& cell = data->cells[data->GetCellIndex(x, y)];
                candidates.PushBatch(cell.shapes.Ptr(), cell.shapes.Count());
            }
        }

        if (candidates.IsEmpty())
            continue;

        // Remove duplicates
        if (candidates.Count() > 1) {
            BlitSort<CollisionShapeHandle>(candidates.Ptr(), candidates.Count(), 
                [](const CollisionShapeHandle& h1, const CollisionShapeHandle& h2)->int 
                {
                    return int(h1.mId) - int(h2.mId);
                });

            int j = 0;  // last unique index
            for (uint32 i = 1; i < candidates.Count(); i++) {
                if (candidates[j] != candidates[i]) {
                    j++;
                    candidates[j] = candidates[i];
                }
            }

            candidates.ForceSetCount(j + 1);
        }

        // Narrow-phase
        c2Poly poly;
        c2x polyTransform;
        Collision::CalculatePolyFromBox(shape.box, &poly, &polyTransform);

        for (CollisionShapeHandle testHandle : candidates) {
            CollisionShapeData& testShape = data->shapes.Data(testHandle);

            if ((shape.maskPair.mask & testShape.maskPair.mask) == 0 || 
                !AABB::Test(shape.transformedAABB, testShape.transformedAABB))
            {
                continue;
            }

            if (testShape.type == CollisionShapeType::Box) {
                c2Poly testPoly;
                c2x testPolyTransform;
                Collision::CalculatePolyFromBox(shape.box, &testPoly, &testPolyTransform);
                if (!c2PolytoPoly(&poly, &polyTransform, &testPoly, &testPolyTransform))
                    continue;
            }
            else if (testShape.type == CollisionShapeType::StaticPoly) {
                if (!c2PolytoPoly(&poly, &polyTransform, (const c2Poly*)&testShape.polygon, nullptr))
                    continue;
            }
            else {
                ASSERT_MSG(0, "Not Implemented");
            }

            if (shape.maskPair.id != testShape.maskPair.id) {
                CollisionPair pair = {
                    .entity1 = shape.maskPair.id,
                    .entity2 = testShape.maskPair.id,
                    .mask1 = shape.maskPair.id,
                    .mask2 = testShape.maskPair.id
                };

                if (!Collision::CheckWithExistingPairs(pairs.Ptr(), pairs.Count(), pair))
                    pairs.Push(pair);
            }
        }

        candidates.Clear();
    }

    if (tempAlloc.OwnsId()) {
        return Span<CollisionPair>(Mem::AllocCopy<CollisionPair>(pairs.Ptr(), pairs.Count(), alloc), pairs.Count());
    }
    else {
        return pairs.Detach();
    }
}

uint32 CollisionIsland::GetCellCount(uint32* outNumCellsX, uint32* outNumCellsY)
{
    CollisionIslandData* data = gCollision.islands.Data(mHandle);
    if (outNumCellsX)
        *outNumCellsX = data->numCellsX;
    if (outNumCellsY)
        *outNumCellsY = data->numCellsY;
    return data->numCells;
}

RectFloat CollisionIsland::GetCellRect(uint32 cellIdx)
{
    CollisionIslandData* data = gCollision.islands.Data(mHandle);
    ASSERT(cellIdx < data->numCells);
    return RectFloat::CenterExtents(data->cells[cellIdx].centerWS, Float2(data->cellSize*0.5f));
}

bool CollisionIsland::GetEntityData(CollisionEntityId id, CollisionShapeData* outData)
{
    ASSERT(outData);
    ASSERT(id);

    CollisionIslandData* data = gCollision.islands.Data(mHandle);
    CollisionShapeHandle handle = data->idToShapeMap.FindAndFetch(id, CollisionShapeHandle());
    if (handle.IsValid()) {
        *outData = data->shapes.Data(handle);
        return true;
    }
    else {
        return false;
    }
}

CollisionIsland Collision::CreateIsland(RectFloat mapRect, float cellSize)
{
    MemAllocator* alloc = &gCollision.islandAlloc;
    MemSingleShotMalloc<CollisionIslandData> dataMallocator;

    uint32 numCellsX = (uint32)M::Ceil(mapRect.Width() / cellSize);
    uint32 numCellsY = (uint32)M::Ceil(mapRect.Height() / cellSize);
    uint32 numCells = numCellsX * numCellsY;

    dataMallocator.AddMemberArray<CollisionSpatialGridCell>(offsetof(CollisionIslandData, cells), numCells);
    CollisionIslandData* islandData = dataMallocator.Calloc(alloc);

    islandData->shapes.SetAllocator(alloc);
    islandData->updatedShapes.SetAllocator(alloc);
    islandData->cellSize = cellSize;
    islandData->mapRect = mapRect;

    float y = mapRect.ymin;
    for (uint32 cellY = 0; cellY < islandData->numCellsY; cellY++) {
        float x = mapRect.xmin;
        for (uint32 cellX = 0; cellX < islandData->numCellsX; cellX++) {
            uint32 index = cellY*numCellsX + cellX;
            CollisionSpatialGridCell& cell = islandData->cells[index];

            cell.posGS = Int2((int)cellX, (int)cellY);
            cell.centerWS = Float2(x + cellSize*0.5f, y + cellSize*0.5f);
            cell.shapes.SetAllocator(alloc);
            x += cellSize;
        }
    }

    islandData->numCellsX = numCellsX;
    islandData->numCellsY = numCellsY;
    islandData->numCells = numCells;

    CollisionIsland collisionIsland { 
        .mHandle = gCollision.islands.Add(islandData)
    };

    return collisionIsland;
}

void Collision::DestroyIsland(CollisionIsland& island)
{
    CollisionIslandData* islandData = gCollision.islands.Data(island.mHandle);

    islandData->shapes.Free();
    islandData->updatedShapes.Free();

    for (uint32 i = 0; i < islandData->numCells; i++) 
        islandData->cells[i].shapes.Free();

    MemSingleShotMalloc<CollisionIslandData>::Free(islandData, &gCollision.islandAlloc);
    island.mHandle = CollisionIslandHandle();
}

bool Collision::Initialize()
{
    Engine::HelperInitializeProxyAllocator(&gCollision.alloc, "Collision");

    Engine::RegisterProxyAllocator(&gCollision.alloc);

    gCollision.islands.SetAllocator(&gCollision.alloc);
    gCollision.islandAllocBase.Initialize(&gCollision.alloc, COLLISION_ISLAND_POOL_SIZE, false);

    return true;
}

void Collision::Release()
{
    gCollision.islandAllocBase.Release();
    gCollision.islands.Free();
}

Span<CollisionEntityId> CollisionIsland::IntersectSphere(Float3 center, float radius, uint32 mask, MemAllocator* alloc)
{
    return Span<CollisionEntityId>();
}

Span<CollisionEntityId> CollisionIsland::IntersectPolygon(const CollisionShapePolygon2D& poly, uint32 mask, MemAllocator* alloc)
{
    return Span<CollisionEntityId>();
}

Span<CollisionRayHit> CollisionIsland::IntersectRay(const CollisionRay& ray, uint32 mask, MemAllocator* alloc)
{
    return Span<CollisionRayHit>();
}

void CollisionIsland::DebugCollisionsGUI(float opacity, CollisionDebugMode mode, float heatmapLimit)
{
#if CONFIG_DEBUG_COLLISIONS
    CollisionIslandData* data = gCollision.islands.Data(mHandle);
    ImDrawList* drawList = ImGui::BeginFullscreenView("DebugCollisions");

    ImVec2 windowSize = ImVec2(float(App::GetWindowWidth()), float(App::GetWindowHeight()));
    drawList->AddRectFilled(ImVec2(0, 0), windowSize, Color4u(0, 0, 0, uint8(opacity*255)).n);
    RectFloat mapRect = RectFloat(-data->mapRect.Width()*0.5f, -data->mapRect.Height()*0.5f, data->mapRect.Width(), data->mapRect.Height());
    RectFloat viewport = RectFloat::Expand(mapRect, Float2(data->mapRect.Width(), data->mapRect.Height())*0.05f);
    Mat4 viewToClipMat = Mat4::OrthoOffCenter(viewport.xmin, viewport.ymin, viewport.xmax, viewport.ymax, -10.0f, 10.0f);
    Mat4 worldToViewMat = Mat4::ViewLookAt(Float3(0, 0, 5), FLOAT3_ZERO, FLOAT3_UNITY);
    Mat4 worldToClipMat = viewToClipMat * worldToViewMat;
    RectFloat screenViewport = RectFloat(0, 0, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);

    switch (mode) {
        case CollisionDebugMode::Collisions:
        {
            Float3 vertices[4];
            for (CollisionShapeData& shape : data->shapes) {
                Collision::BoxToVertices(shape.transformedBox, vertices);
                drawList->AddQuad(ImGui::ProjectToScreen(vertices[0], worldToClipMat, screenViewport),
                                  ImGui::ProjectToScreen(vertices[1], worldToClipMat, screenViewport),
                                  ImGui::ProjectToScreen(vertices[2], worldToClipMat, screenViewport),
                                  ImGui::ProjectToScreen(vertices[3], worldToClipMat, screenViewport),
                                  shape.collisionFrameIdx != Engine::GetFrameIndex() ? COLOR4U_RED.n : COLOR4U_WHITE.n);
            }
        }
        break;
    }

    // World bounds
    drawList->AddRect(ImGui::ProjectToScreen(Float3(mapRect.xmin, mapRect.ymin, 0), worldToClipMat, screenViewport),
                      ImGui::ProjectToScreen(Float3(mapRect.xmax, mapRect.ymax, 0), worldToClipMat, screenViewport),
                      COLOR4U_YELLOW.n, 0, 0, 2);
#else
    UNUSED(opacity);
    UNUSED(mode);
    UNUSED(heatmapLimit);
#endif  // CONFIG_DEBUG_COLLISIONS
}

void CollisionIsland::DebugRaycastGUI(float opacity, CollisionDebugRaycastMode mode, float heatmapLimit)
{
}