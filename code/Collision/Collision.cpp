#include "Collision.h"

#include "../Core/Arrays.h"
#include "../Core/MathAll.h"
#include "../Core/Hash.h"
#include "../Core/BlitSort.h"
#include "../Engine.h"
#include "../Common/Application.h"

#include "../DebugTools/DebugDraw.h"

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

#if CONFIG_DEBUG_COLLISIONS
    uint32 numCollisions;
    uint32 numRayhits;
    uint32 numRayMarches;
#endif
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

    MemTlsfAllocator islandAllocBase;

    HandlePool<CollisionIslandHandle, CollisionIslandData*> islands;
};

static CollisionContext gCollision;

namespace Collision
{
    INLINE Int2 _HashPoint(const CollisionIslandData* island, Float2 pt)
    {
        float cellSize = island->cellSize;
        float hx = pt.x/cellSize + (float)island->numCellsX*0.5f;
        float hy = pt.y/cellSize + (float)island->numCellsY*0.5f;

        return Int2(Clamp<int>(int(hx), 0, int(island->numCellsX - 1)), 
                    Clamp<int>(int(hy), 0, int(island->numCellsY - 1)));
    }

    INLINE void _CalculatePolyFromBox(const CollisionShapeBox& box, c2Poly* outPoly, c2x* outTransform)
    {
        outPoly->count = 4;
        outPoly->verts[0] = { box.extents.x, box.extents.y };
        outPoly->norms[0] = { 0, 1 };   // top edge
        outPoly->verts[1] = { -box.extents.x, box.extents.y };
        outPoly->norms[1] = { -1, 0 };  // left edge
        outPoly->verts[2] = { -box.extents.x, -box.extents.y };
        outPoly->norms[2] = { 0, -1 };  // bottom edge
        outPoly->verts[3] = { box.extents.x, -box.extents.y };
        outPoly->norms[3] = { 1, 0 };   // right edge

        // From Quat::ToEuler (z-axis calculation)
        Quat q = box.transform.rotation;
        outTransform->r.c = 1 - 2 * (q.y * q.y + q.z * q.z);
        outTransform->r.s = 2 * (q.w * q.z + q.x * q.y);
        outTransform->p.x = box.transform.position.x;
        outTransform->p.y = box.transform.position.y;
    }

    INLINE bool _CheckWithExistingPairs(const CollisionPair* pairs, uint32 numPairs, const CollisionPair testPair)
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

    static bool _RayHitBox(CollisionRay ray, const CollisionShapeBox& box, CollisionRayHit& outHit)
    {
        float const epsilon = 1.0e-8f;

        Quat invRotation = Quat::Conjugate(box.transform.rotation);
        Float3 d = Quat::TransformFloat3(ray.direction, invRotation);
        Float3 p = Quat::TransformFloat3(ray.origin - box.transform.position, invRotation);
        float tmin = 0;
        float tmax = ray.length;
    
        float t0;
        float t1;
        Float3 n0 = FLOAT3_ZERO;
        Float3 e = box.extents;

        for (int i = 0; i < 3; i++) {
            if (M::Abs(d.f[i]) < epsilon) {
                if (p.f[i] < -e.f[i] || p.f[i] > e.f[i]) {
                    return false;
                }
            } else {
                float d0 = 1.0f / d.f[i];
                float s = M::Sign(d.f[i]);
                float ei = e.f[i] * s;

                Float3 n = FLOAT3_ZERO;
                n.f[i] = -s;

                t0 = -(ei + p.f[i]) * d0;
                t1 =  (ei - p.f[i]) * d0;

                if (t0 > tmin) {
                    n0 = n;
                    tmin = t0;
                }

                tmax = Min<float>(tmax, t1);
                if (tmin > tmax) {
                    return false;
                }
            }
        }

        if (tmin <= epsilon) 
            return false;

        outHit.t = tmin;
        outHit.normal = Quat::TransformFloat3(n0, box.transform.rotation);

        return true;
    }

    static void _RemoveDuplicates(Array<CollisionShapeHandle>& shapeArray)
    {
        if (shapeArray.Count() <= 1)
            return;

        BlitSort<CollisionShapeHandle>(shapeArray.Ptr(), shapeArray.Count(), 
            [](const CollisionShapeHandle& h1, const CollisionShapeHandle& h2)->int 
            {
                return int(h1.mId) - int(h2.mId);
            });

        int j = 0;  // last unique index
        for (uint32 i = 1; i < shapeArray.Count(); i++) {
            if (shapeArray[j] != shapeArray[i]) {
                j++;
                shapeArray[j] = shapeArray[i];
            }
        }

        shapeArray.ForceSetCount(j + 1);
    }

#if CONFIG_DEBUG_COLLISIONS
    static void _BoxToVertices(const CollisionShapeBox& box, Float3 outVertices[4])
    {
        outVertices[0] = Quat::TransformFloat3(Float3(box.extents.x, box.extents.y, 0), box.transform.rotation) + box.transform.position;
        outVertices[1] = Quat::TransformFloat3(Float3(-box.extents.x, box.extents.y, 0), box.transform.rotation) + box.transform.position;
        outVertices[2] = Quat::TransformFloat3(Float3(-box.extents.x, -box.extents.y, 0), box.transform.rotation) + box.transform.position;
        outVertices[3] = Quat::TransformFloat3(Float3(box.extents.x, -box.extents.y, 0), box.transform.rotation) + box.transform.position;
    }

    static void _MarkCollision(CollisionIslandData* island, const AABB& aabb)
    {
        Int2 hmin = Collision::_HashPoint(island, Float2(aabb.vmin.f));
        Int2 hmax = Collision::_HashPoint(island, Float2(aabb.vmax.f));

        for (int y = hmin.y; y <= hmax.y; y++) {
            for (int x = hmin.x; x <= hmax.x; x++) {
                CollisionSpatialGridCell& cell = island->cells[island->GetCellIndex(x, y)];
                ++cell.numCollisions;   // TODO: switch to atomic for multi-threading
            }
        }
    }

    static void _MarkRayhit(CollisionIslandData* island, const AABB& aabb)
    {
        Int2 hmin = Collision::_HashPoint(island, Float2(aabb.vmin.f));
        Int2 hmax = Collision::_HashPoint(island, Float2(aabb.vmax.f));

        for (int y = hmin.y; y <= hmax.y; y++) {
            for (int x = hmin.x; x <= hmax.x; x++) {
                CollisionSpatialGridCell& cell = island->cells[island->GetCellIndex(x, y)];
                ++cell.numRayhits; // TODO: switch to atomic for multi-threading
            }
        }
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
        Mat4 transformedMat = Mat4::TransformMat(boxDesc.transform.position, boxDesc.transform.rotation, Float3(1, 1, 1));
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
        Int2 minHashed = Collision::_HashPoint(data, Float2(shape.transformedAABB.xmin, shape.transformedAABB.ymin));
        Int2 maxHashed = Collision::_HashPoint(data, Float2(shape.transformedAABB.xmax, shape.transformedAABB.ymax));
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
        Int2 minHashed = Collision::_HashPoint(data, Float2(shape.transformedAABB.xmin, shape.transformedAABB.ymin));
        Int2 maxHashed = Collision::_HashPoint(data, Float2(shape.transformedAABB.xmax, shape.transformedAABB.ymax));
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
        Int2 minHashed = Collision::_HashPoint(data, Float2(shape.aabb.xmin, shape.aabb.ymin));
        Int2 maxHashed = Collision::_HashPoint(data, Float2(shape.aabb.xmax, shape.aabb.ymax));

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
        Int2 prevMinHashed = Collision::_HashPoint(data, Float2(prevAABB.xmin, prevAABB.ymin));
        Int2 prevMaxHashed = Collision::_HashPoint(data, Float2(prevAABB.xmax, prevAABB.ymax));
        RectInt prevArea = RectInt(prevMinHashed, prevMaxHashed);

        Mat4 transformMat = Mat4::TransformMat(transforms[i].position, transforms[i].rotation, Float3(1, 1, 1));
        AABB aabb = AABB::Transform(shape.aabb, transformMat);
        Int2 minHashed = Collision::_HashPoint(data, Float2(aabb.xmin, aabb.ymin));
        Int2 maxHashed = Collision::_HashPoint(data, Float2(aabb.xmax, aabb.ymax));
        RectInt area = RectInt(minHashed, maxHashed);

        shape.transformedAABB = aabb;
        Float3 transformedBoxPos = Quat::TransformFloat3(shape.box.transform.position, transforms[i].rotation) + transforms[i].position;
        Quat transformedBoxQuat = Quat::Mul(transforms[i].rotation, shape.box.transform.rotation);
        shape.transformedBox = {
            .transform = {
                .position = transformedBoxPos,
                .rotation = transformedBoxQuat
            },
            .extents = shape.box.extents
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

void CollisionIsland::Invalidate(CollisionEntityId id)
{
    CollisionIslandData* data = gCollision.islands.Data(mHandle);
    CollisionShapeHandle handle = data->idToShapeMap.FindAndFetch(id, CollisionShapeHandle());
    ASSERT_MSG(handle.IsValid(), "Entity id '%u' not found", id);

    data->updatedShapes.Push(handle);
}

Span<CollisionPair> CollisionIsland::DetectCollisions(MemAllocator* alloc)
{
    PROFILE_ZONE("C_DetectCollisions");

    CollisionIslandData* data = gCollision.islands.Data(mHandle);

    DEFINE_SAFE_TEMP_ALLOCATOR(tempAlloc, alloc);
    Array<CollisionShapeHandle> candidates(&tempAlloc);
    Array<CollisionPair> pairs(&tempAlloc);

    for (CollisionShapeHandle handle : data->updatedShapes) {
        CollisionShapeData& shape = data->shapes.Data(handle);

        ASSERT_MSG(shape.type != CollisionShapeType::StaticPoly, "Static shapes transforms should not be updated");

        // Broadphase
        Int2 minHashed = Collision::_HashPoint(data, Float2(shape.transformedAABB.xmin, shape.transformedAABB.ymin));
        Int2 maxHashed = Collision::_HashPoint(data, Float2(shape.transformedAABB.xmax, shape.transformedAABB.ymax));

        for (int y = minHashed.y; y <= maxHashed.y; y++) {
            for (int x = minHashed.x; x <= maxHashed.x; x++) {
                CollisionSpatialGridCell& cell = data->cells[data->GetCellIndex(x, y)];
                if (!cell.shapes.IsEmpty())
                    candidates.PushBatch(cell.shapes.Ptr(), cell.shapes.Count());
            }
        }

        if (candidates.IsEmpty())
            continue;

        // Remove duplicates
        Collision::_RemoveDuplicates(candidates);

        // Narrow-phase
        c2Poly poly;
        c2x polyTransform;
        Collision::_CalculatePolyFromBox(shape.transformedBox, &poly, &polyTransform);

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
                Collision::_CalculatePolyFromBox(testShape.transformedBox, &testPoly, &testPolyTransform);
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

    #if CONFIG_DEBUG_COLLISIONS
                uint64 frameIdx = Engine::GetFrameIndex();
                shape.collisionFrameIdx = frameIdx;
                Collision::_MarkCollision(data, shape.transformedAABB);

                if (testShape.type == CollisionShapeType::StaticPoly) {
                    testShape.collisionFrameIdx = frameIdx;
                    Collision::_MarkCollision(data, shape.aabb);
                }
    #endif

                if (!Collision::_CheckWithExistingPairs(pairs.Ptr(), pairs.Count(), pair))
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

void CollisionIsland::ClearUpdates()
{
    CollisionIslandData* data = gCollision.islands.Data(mHandle);
    data->updatedShapes.Clear();
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
    MemAllocator* alloc = &gCollision.islandAllocBase;
    MemSingleShotMalloc<CollisionIslandData> dataMallocator;

    uint32 numCellsX = (uint32)M::Ceil(mapRect.Width() / cellSize);
    uint32 numCellsY = (uint32)M::Ceil(mapRect.Height() / cellSize);
    uint32 numCells = numCellsX * numCellsY;

    dataMallocator.AddMemberArray<CollisionSpatialGridCell>(offsetof(CollisionIslandData, cells), numCells);
    CollisionIslandData* islandData = dataMallocator.Calloc(alloc);

    islandData->shapes.SetAllocator(alloc);
    islandData->updatedShapes.SetAllocator(alloc);
    islandData->idToShapeMap.SetAllocator(alloc);
    islandData->cellSize = cellSize;
    islandData->mapRect = mapRect;

    float y = mapRect.ymin;
    for (uint32 cellY = 0; cellY < numCellsY; cellY++) {
        float x = mapRect.xmin;
        for (uint32 cellX = 0; cellX < numCellsX; cellX++) {
            uint32 index = cellY*numCellsX + cellX;
            CollisionSpatialGridCell& cell = islandData->cells[index];

            cell.posGS = Int2((int)cellX, (int)cellY);
            cell.centerWS = Float2(x + cellSize*0.5f, y + cellSize*0.5f);
            cell.shapes.SetAllocator(alloc);
            x += cellSize;
        }
        y += cellSize;
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

    MemSingleShotMalloc<CollisionIslandData>::Free(islandData, &gCollision.islandAllocBase);
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
    CollisionIslandData* data = gCollision.islands.Data(mHandle);

    AABB aabb = AABB(center - Float3(radius), center + Float3(radius));
    Int2 minHashed = Collision::_HashPoint(data, Float2(aabb.vmin.f));
    Int2 maxHashed = Collision::_HashPoint(data, Float2(aabb.vmax.f));

    DEFINE_SAFE_TEMP_ALLOCATOR(tempAlloc, alloc);
    Array<CollisionShapeHandle> candidates(&tempAlloc);
    Array<CollisionEntityId> intersections(&tempAlloc);

    for (int y = minHashed.y; y <= maxHashed.y; y++) {
        for (int x = minHashed.x; x <= maxHashed.x; x++) {
            const CollisionSpatialGridCell& cell = data->cells[data->GetCellIndex(x, y)];
            if (!cell.shapes.IsEmpty())
                candidates.PushBatch(cell.shapes.Ptr(), cell.shapes.Count());
        }
    }

    Collision::_RemoveDuplicates(candidates);

    c2Circle circle {
        .p = {center.x, center.y},
        .r = radius
    };

    for (CollisionShapeHandle handle : candidates) {
        CollisionShapeData& shape = data->shapes.Data(handle);
        if ((shape.maskPair.mask & mask) == 0)
            continue;

        if (shape.type == CollisionShapeType::Box) {
            if (!AABB::Test(shape.transformedAABB, aabb))
                continue;

            c2Poly testPoly;
            c2x testPolyTransform;
            Collision::_CalculatePolyFromBox(shape.transformedBox, &testPoly, &testPolyTransform);
            if (!c2CircletoPoly(circle, &testPoly, &testPolyTransform))
                continue;
        }
        else if (shape.type == CollisionShapeType::StaticPoly) {
            if (!AABB::Test(shape.aabb, aabb))
                continue;

            if (!c2CircletoPoly(circle,  (const c2Poly*)&shape.polygon, nullptr))
                continue;
        }
        else {
            ASSERT_MSG(0, "Not implemented");
        }

        intersections.Push(shape.maskPair.id);
    }

    if (tempAlloc.OwnsId()) {
        return Span<CollisionEntityId>(Mem::AllocCopy<CollisionEntityId>(intersections.Ptr(), intersections.Count(), alloc), intersections.Count());
    }
    else {
        return intersections.Detach();
    }
}

Span<CollisionEntityId> CollisionIsland::IntersectPolygon(const CollisionShapePolygon2D& poly, uint32 mask, MemAllocator* alloc)
{
    CollisionIslandData* data = gCollision.islands.Data(mHandle);

    AABB aabb = AABB_EMPTY;
    for (uint32 i = 0; i < poly.numVertices; i++)
        AABB::AddPoint(aabb, Float3(poly.vertices[i]));

    Int2 minHashed = Collision::_HashPoint(data, Float2(aabb.vmin.f));
    Int2 maxHashed = Collision::_HashPoint(data, Float2(aabb.vmax.f));

    DEFINE_SAFE_TEMP_ALLOCATOR(tempAlloc, alloc);
    Array<CollisionShapeHandle> candidates(&tempAlloc);
    Array<CollisionEntityId> intersections(&tempAlloc);

    for (int y = minHashed.y; y <= maxHashed.y; y++) {
        for (int x = minHashed.x; x <= maxHashed.x; x++) {
            const CollisionSpatialGridCell& cell = data->cells[data->GetCellIndex(x, y)];
            if (!cell.shapes.IsEmpty())
                candidates.PushBatch(cell.shapes.Ptr(), cell.shapes.Count());
        }
    }

    Collision::_RemoveDuplicates(candidates);

    for (CollisionShapeHandle handle : candidates) {
        CollisionShapeData& shape = data->shapes.Data(handle);
        if ((shape.maskPair.mask & mask) == 0)
            continue;

        if (shape.type == CollisionShapeType::Box) {
            c2Poly testPoly;
            c2x testPolyTransform;
            Collision::_CalculatePolyFromBox(shape.transformedBox, &testPoly, &testPolyTransform);
            if (!c2PolytoPoly((const c2Poly*)&poly, nullptr, &testPoly, &testPolyTransform))
                continue;
        }
        else if (shape.type == CollisionShapeType::StaticPoly) {
            if (!AABB::Test(shape.aabb, aabb))
                continue;

            if (!c2PolytoPoly((const c2Poly*)&poly, nullptr, (const c2Poly*)&shape.polygon, nullptr))
                continue;
        }
        else {
            ASSERT_MSG(0, "Not implemented");
        }

        intersections.Push(shape.maskPair.id);
    }

    if (tempAlloc.OwnsId()) {
        return Span<CollisionEntityId>(Mem::AllocCopy<CollisionEntityId>(intersections.Ptr(), intersections.Count(), alloc), intersections.Count());
    }
    else {
        return intersections.Detach();
    }
}

Span<CollisionEntityId> CollisionIsland::IntersectBox(const CollisionShapeBox& box, uint32 mask, MemAllocator* alloc)
{
    CollisionIslandData* data = gCollision.islands.Data(mHandle);

    c2Poly poly;
    c2x polyTransform;
    Collision::_CalculatePolyFromBox(box, &poly, &polyTransform);
    AABB aabb = AABB::Transform(AABB::CenterExtents(FLOAT3_ZERO, box.extents), 
                                Mat4::TransformMat(box.transform.position, box.transform.rotation, Float3(1, 1, 1)));;

    Int2 minHashed = Collision::_HashPoint(data, Float2(aabb.vmin.f));
    Int2 maxHashed = Collision::_HashPoint(data, Float2(aabb.vmax.f));

    DEFINE_SAFE_TEMP_ALLOCATOR(tempAlloc, alloc);
    Array<CollisionShapeHandle> candidates(&tempAlloc);
    Array<CollisionEntityId> intersections(&tempAlloc);

    for (int y = minHashed.y; y <= maxHashed.y; y++) {
        for (int x = minHashed.x; x <= maxHashed.x; x++) {
            const CollisionSpatialGridCell& cell = data->cells[data->GetCellIndex(x, y)];
            if (!cell.shapes.IsEmpty())
                candidates.PushBatch(cell.shapes.Ptr(), cell.shapes.Count());
        }
    }

    Collision::_RemoveDuplicates(candidates);

    for (CollisionShapeHandle handle : candidates) {
        CollisionShapeData& shape = data->shapes.Data(handle);
        if ((shape.maskPair.mask & mask) == 0)
            continue;

        if (shape.type == CollisionShapeType::Box) {
            if (!AABB::Test(shape.transformedAABB, aabb))
                continue;

            c2Poly testPoly;
            c2x testPolyTransform;
            Collision::_CalculatePolyFromBox(shape.transformedBox, &testPoly, &testPolyTransform);
            if (!c2PolytoPoly((const c2Poly*)&poly, &polyTransform, &testPoly, &testPolyTransform))
                continue;
        }
        else if (shape.type == CollisionShapeType::StaticPoly) {
            if (!AABB::Test(shape.aabb, aabb))
                continue;

            if (!c2PolytoPoly((const c2Poly*)&poly, &polyTransform, (const c2Poly*)&shape.polygon, nullptr))
                continue;
        }
        else {
            ASSERT_MSG(0, "Not implemented");
        }

        intersections.Push(shape.maskPair.id);
    }

    if (tempAlloc.OwnsId()) {
        return Span<CollisionEntityId>(Mem::AllocCopy<CollisionEntityId>(intersections.Ptr(), intersections.Count(), alloc), intersections.Count());
    }
    else {
        return intersections.Detach();
    }
}

Span<CollisionRayHit> CollisionIsland::IntersectRay(CollisionRay ray, uint32 mask, MemAllocator* alloc)
{
    CollisionIslandData* data = gCollision.islands.Data(mHandle);

    if (!RectFloat::TestPoint(data->mapRect, Float2(ray.origin.f))) {
        // Check ray origin with world boundries and clip it if it's outside
        float t;
        if ((t = Plane::HitRay(Plane(0, 1, 0, -data->mapRect.ymax), ray.origin, ray.direction)) >= 0) {
            if (t >= ray.length) 
                return Span<CollisionRayHit>();
            ray.origin = ray.origin + ray.direction*t;
        }

        if ((t = Plane::HitRay(Plane(0, -1, 0, data->mapRect.ymin), ray.origin, ray.direction)) >= 0) {
            if (t >= ray.length) 
                return Span<CollisionRayHit>();
            ray.origin = ray.origin + ray.direction*t;
        }

        if ((t = Plane::HitRay(Plane(1, 0, 0, -data->mapRect.xmax), ray.origin, ray.direction)) >= 0) {
            if (t >= ray.length) 
                return Span<CollisionRayHit>();
            ray.origin = ray.origin + ray.direction*t;
        }

        if ((t = Plane::HitRay(Plane(-1, 0, 0, data->mapRect.xmin), ray.origin, ray.direction)) >= 0) {
            if (t >= ray.length) 
                return Span<CollisionRayHit>();
            ray.origin = ray.origin + ray.direction*t;
        }
    }
    else {
        // Intersect the ray with map boundries, so we won't get incorrect broadphase results
        float t;
        if ((t = Plane::HitRay(Plane(0, 1.0f, 0, -data->mapRect.ymin), ray.origin, ray.direction)) >= 0)
            ray.length = Min<float>(ray.length, t);

        if ((t = Plane::HitRay(Plane(0, -1.0f, 0, data->mapRect.ymax), ray.origin, ray.direction)) >= 0)
            ray.length = Min<float>(ray.length, t);

        if ((t = Plane::HitRay(Plane(1.0f, 0, 0, -data->mapRect.xmin), ray.origin, ray.direction)) >= 0)
            ray.length = Min<float>(ray.length, t);

        if ((t = Plane::HitRay(Plane(-1.0f, 0, 0, data->mapRect.xmax), ray.origin, ray.direction)) >= 0)
            ray.length = Min<float>(ray.length, t);
    }

    // Broadphase
    Float2 target = Float2((ray.origin + ray.direction*(ray.length - 0.00001f)).f);
    if (!RectFloat::TestPoint(data->mapRect, target))
        return Span<CollisionRayHit>();

    Int2 p0 = Collision::_HashPoint(data, Float2(ray.origin.f));
    Int2 p1 = Collision::_HashPoint(data, target);

    DEFINE_SAFE_TEMP_ALLOCATOR(tempAlloc, alloc);
    Array<CollisionRayHit> hits(&tempAlloc);
    Array<uint32> candidateCells(&tempAlloc);
    Array<CollisionShapeHandle> candidates(&tempAlloc);

    // Broadphase: Bresenham AA line drawing
    int x0 = p0.x, y0 = p0.y, x1 = p1.x, y1 = p1.y;
    int dx = M::Abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = M::Abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx - dy, e2, x2;
    int ed = dx + dy == 0 ? 1 : int(M::Sqrt(float(dx*dx + dy*dy)));
    uint32 cellIdx;

    while (1) {
        cellIdx = data->GetCellIndex(x0, y0);
        if (candidateCells.Find(cellIdx) == -1) {
#if CONFIG_DEBUG_COLLISIONS
            ++data->cells[cellIdx].numRayMarches;
#endif
            candidateCells.Push(cellIdx);
        }
        e2 = err; x2 = x0;
        if (2 * e2 >= -dx) {    // x step
            if (x0 == x1)
                break;
            if (e2 + dy < ed) {
                cellIdx = data->GetCellIndex(x0, y0 + sy);
                if (candidateCells.Find(cellIdx) == -1) {
#if CONFIG_DEBUG_COLLISIONS
                    ++data->cells[cellIdx].numRayMarches;
#endif
                    candidateCells.Push(cellIdx);
                }
            }
            err -= dy; x0 += sx;
        }
        if (2 * e2 <= dy) {     // y step
            if (y0 == y1)
                break;

            if (dx-e2 < ed) {
                cellIdx = data->GetCellIndex(x2 + sx, y0);
                if (candidateCells.Find(cellIdx) == -1) {
#if CONFIG_DEBUG_COLLISIONS
                    ++data->cells[cellIdx].numRayMarches;
#endif
                    candidateCells.Push(cellIdx);
                }
            } 
            err += dx; y0 += sy;
        }
    }

    for (uint32 i = 0; i < candidateCells.Count(); i++) {
        CollisionSpatialGridCell& cell = data->cells[candidateCells[i]];
        if (!cell.shapes.IsEmpty())
            candidates.PushBatch(cell.shapes.Ptr(), cell.shapes.Count());
    }   

    Collision::_RemoveDuplicates(candidates);

#if CONFIG_DEBUG_COLLISIONS
    for (uint32 i = 0; i < candidates.Count(); i++) {
        CollisionShapeData& shape = data->shapes.Data(candidates[i]);
        shape.raymarchFrameIdx = Engine::GetFrameIndex();
    }
#endif

    // Narrow phase
    CollisionRayHit hit;
    for (uint32 i = 0; i < candidates.Count(); i++) {
        CollisionShapeData& shape = data->shapes.Data(candidates[i]);

        if (shape.type != CollisionShapeType::StaticPoly && (mask & shape.maskPair.mask)) {
            if (Collision::_RayHitBox(ray, shape.transformedBox, hit)) {
#if CONFIG_DEBUG_COLLISIONS
                shape.rayhitFrameIdx = Engine::GetFrameIndex();
                Collision::_MarkRayhit(data, shape.transformedAABB);
#endif
                hit.entity = shape.maskPair.id;
                hits.Push(hit);
            } 
        } 
    }

    // Sort all results by closest to the ray origin
    BlitSort<CollisionRayHit>(hits.Ptr(), hits.Count(), 
        [](const CollisionRayHit& h1, const CollisionRayHit& h2)->int 
        {
            return h1.t > h2.t;
        });

    if (tempAlloc.OwnsId()) {
        return Span<CollisionRayHit>(Mem::AllocCopy<CollisionRayHit>(hits.Ptr(), hits.Count(), alloc), hits.Count());
    }
    else {
        return hits.Detach();
    }
}

void CollisionIsland::DebugCollisionsGUI(float opacity, CollisionDebugMode mode, float heatmapLimit)
{
#if CONFIG_DEBUG_COLLISIONS
    CollisionIslandData* data = gCollision.islands.Data(mHandle);
    ImDrawList* drawList = ImGui::BeginFullscreenView("DebugCollisions");

    ImVec2 windowSize = ImGui::GetIO().DisplaySize;
    drawList->AddRectFilled(ImVec2(0, 0), windowSize, Color4u(0, 0, 0, uint8(opacity*255)).n);
    RectFloat mapRect = data->mapRect;
    RectFloat viewport = RectFloat::Expand(mapRect, Float2(mapRect.Width(), mapRect.Height())*0.05f);
    Mat4 viewToClipMat = Mat4::OrthoOffCenter(viewport.xmin, viewport.ymin, viewport.xmax, viewport.ymax, -10.0f, 10.0f);
    Mat4 worldToViewMat = Mat4::ViewLookAt(Float3(0, 0, 5), FLOAT3_ZERO, FLOAT3_UNITY);
    Mat4 worldToClipMat = viewToClipMat * worldToViewMat;
    RectFloat screenViewport = RectFloat(0, 0, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);

    // TODO: This Letterbox code is a useful one, put it in a utility function
    {
        float fbWidth = windowSize.x;
        float fbHeight = windowSize.y;
        float imageWidth = mapRect.Width();
        float imageHeight = mapRect.Height();
        float scaleX = fbWidth /  imageWidth;
        float scaleY = fbHeight / imageHeight;
        float scale = (scaleX < scaleY) ? scaleX : scaleY;

        float vpwidth = imageWidth * scale;
        float vpheight = imageHeight * scale;
        float x = (fbWidth - vpwidth) * 0.5f;
        float y = (fbHeight - vpheight) * 0.5f;

        screenViewport = RectFloat::CenterExtents(Float2(x + vpwidth*0.5f, y + vpheight*0.5f), Float2(vpwidth*0.5f, vpheight*0.5f));
    }
    switch (mode) {
        case CollisionDebugMode::Collisions:
        {
            Float3 vertices[4];
            for (CollisionShapeData& shape : data->shapes) {
                Collision::_BoxToVertices(shape.transformedBox, vertices);
                drawList->AddQuad(ImGui::ProjectToScreen(vertices[0], worldToClipMat, screenViewport),
                                  ImGui::ProjectToScreen(vertices[1], worldToClipMat, screenViewport),
                                  ImGui::ProjectToScreen(vertices[2], worldToClipMat, screenViewport),
                                  ImGui::ProjectToScreen(vertices[3], worldToClipMat, screenViewport),
                                  shape.collisionFrameIdx == Engine::GetFrameIndex() ? COLOR4U_RED.n : COLOR4U_WHITE.n);
            }
            break;
        }

        case CollisionDebugMode::Heatmap:
        case CollisionDebugMode::EntityHeatmap:
        {
            Float3 hsvBase = Color4u::RGBtoHSV(Float3(0, 1, 0));

            for (uint32 i = 0; i < data->numCells; i++) {
                CollisionSpatialGridCell& cell = data->cells[i];

                RectFloat cellRect = RectFloat::CenterExtents(cell.centerWS, Float2(data->cellSize*0.5f));
                ImVec2 v1 = ImGui::ProjectToScreen(Float3(cellRect.xmin, cellRect.ymin, 0), worldToClipMat, screenViewport);
                ImVec2 v2 = ImGui::ProjectToScreen(Float3(cellRect.xmax, cellRect.ymax, 0), worldToClipMat, screenViewport);
                Swap<float>(v1.y, v2.y);

                float heatValue = (mode ==  CollisionDebugMode::Heatmap) ? 
                    heatValue = float(cell.numCollisions) / heatmapLimit :
                    heatValue = float(cell.shapes.Count()) / heatmapLimit;
                heatValue = Min<float>(1, heatValue);
                Float3 color = Color4u::HSVtoRGB(Float3(M::Lerp(hsvBase.x, 0, heatValue), hsvBase.y, hsvBase.z));
                drawList->AddRectFilled(v1, v2, Color4u::FromFloat4(color.x, color.y, color.z, 0.3f).n, 0, 0);
            }

            break;
        }
    }

    // World bounds
    drawList->AddRect(ImGui::ProjectToScreen(Float3(mapRect.xmin, mapRect.ymin, 0), worldToClipMat, screenViewport),
                      ImGui::ProjectToScreen(Float3(mapRect.xmax, mapRect.ymax, 0), worldToClipMat, screenViewport),
                      COLOR4U_YELLOW.n, 0, 0, 2);

    for (uint32 i = 0; i < data->numCells; i++)
        data->cells[i].numCollisions = 0;
#else
    UNUSED(opacity);
    UNUSED(mode);
    UNUSED(heatmapLimit);
#endif  // CONFIG_DEBUG_COLLISIONS
}

void CollisionIsland::DebugRaycastGUI(float opacity, CollisionDebugRaycastMode mode, float heatmapLimit, const CollisionRay* rays, uint32 numRays)
{
#if CONFIG_DEBUG_COLLISIONS
    CollisionIslandData* data = gCollision.islands.Data(mHandle);
    ImDrawList* drawList = ImGui::BeginFullscreenView("CollisionRaycast");
    ImVec2 windowSize = ImGui::GetIO().DisplaySize;
    
    drawList->AddRectFilled(ImVec2(0, 0), windowSize, Color4u(0, 0, 0, uint8(opacity*255)).n);

    RectFloat mapRect = data->mapRect;
    RectFloat viewport = RectFloat::Expand(mapRect, Float2(mapRect.Width(), mapRect.Height())*0.05f);
    Mat4 viewToClipMat = Mat4::OrthoOffCenter(viewport.xmin, viewport.ymin, viewport.xmax, viewport.ymax, -10.0f, 10.0f);
    Mat4 worldToViewMat = Mat4::ViewLookAt(Float3(0, 0, 5), FLOAT3_ZERO, FLOAT3_UNITY);
    Mat4 worldToClipMat = viewToClipMat * worldToViewMat;
    RectFloat screenViewport = RectFloat(0, 0, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);

    // TODO: This Letterbox code is a useful one, put it in a utility function
    {
        float fbWidth = windowSize.x;
        float fbHeight = windowSize.y;
        float imageWidth = mapRect.Width();
        float imageHeight = mapRect.Height();
        float scaleX = fbWidth /  imageWidth;
        float scaleY = fbHeight / imageHeight;
        float scale = (scaleX < scaleY) ? scaleX : scaleY;

        float vpwidth = imageWidth * scale;
        float vpheight = imageHeight * scale;
        float x = (fbWidth - vpwidth) * 0.5f;
        float y = (fbHeight - vpheight) * 0.5f;

        screenViewport = RectFloat::CenterExtents(Float2(x + vpwidth*0.5f, y + vpheight*0.5f), Float2(vpwidth*0.5f, vpheight*0.5f));
    }

    switch (mode) {
    case CollisionDebugRaycastMode::Rayhits:
    {
        uint64 frame = Engine::GetFrameIndex();
        Float3 vertices[4];

        for (CollisionShapeData& shape : data->shapes) {
            Collision::_BoxToVertices(shape.transformedBox, vertices);

            Color4u color = COLOR4U_WHITE;
            if (shape.rayhitFrameIdx == frame) 
                color = COLOR4U_RED;
            else if (shape.raymarchFrameIdx == frame) 
                color = COLOR4U_GREEN;

            drawList->AddQuad(ImGui::ProjectToScreen(vertices[0], worldToClipMat, screenViewport),
                              ImGui::ProjectToScreen(vertices[1], worldToClipMat, screenViewport),
                              ImGui::ProjectToScreen(vertices[2], worldToClipMat, screenViewport),
                              ImGui::ProjectToScreen(vertices[3], worldToClipMat, screenViewport),
                color.n, 1.0f);
        }
        break;
    } 
    case CollisionDebugRaycastMode::RayhitHeatmap:
    case CollisionDebugRaycastMode::RaymarchHeatmap:
    {
        Float3 hsvBase = Color4u::RGBtoHSV(Float3(0, 1, 0));

        for (uint32 i = 0; i < data->numCells; i++) {
            CollisionSpatialGridCell& cell = data->cells[i];

            RectFloat cellRect = RectFloat::CenterExtents(cell.centerWS, Float2(data->cellSize*0.5f));
            ImVec2 v1 = ImGui::ProjectToScreen(Float3(cellRect.xmin, cellRect.ymin, 0), worldToClipMat, screenViewport);
            ImVec2 v2 = ImGui::ProjectToScreen(Float3(cellRect.xmax, cellRect.ymax, 0), worldToClipMat, screenViewport);
            Swap<float>(v1.y, v2.y);

            float heatValue = (mode ==  CollisionDebugRaycastMode::RayhitHeatmap) ? 
                heatValue = float(cell.numRayhits) / heatmapLimit :
                heatValue = float(cell.numRayMarches) / heatmapLimit;
            heatValue = Min<float>(1, heatValue);
            Float3 color = Color4u::HSVtoRGB(Float3(M::Lerp(hsvBase.x, 0, heatValue), hsvBase.y, hsvBase.z));

            drawList->AddRect(v1, v2, COLOR4U_GREEN.n, 0, ImDrawFlags_None, 1.0f);
            drawList->AddRectFilled(v1, v2, Color4u::FromFloat4(color.x, color.y, color.z, 0.3f).n, 0, 0);
        }

        break;
    } 
    }

    // world bounds
    drawList->AddRect(ImGui::ProjectToScreen(Float3(mapRect.xmin, mapRect.ymin, 0), worldToClipMat, screenViewport),
                      ImGui::ProjectToScreen(Float3(mapRect.xmax, mapRect.ymax, 0), worldToClipMat, screenViewport),
                      COLOR4U_YELLOW.n, 0, 0, 2);

    // Rays
    for (uint32 i = 0; i < numRays; i++) {
        CollisionRay ray = rays[i];
        ImVec2 rayOrigin = ImGui::ProjectToScreen(ray.origin, worldToClipMat, screenViewport);
        {
            float t;
            if ((t = Plane::HitRay(Plane(0, 1, 0, -mapRect.ymin), ray.origin, ray.direction)) >= 0)
                ray.length = Min<float>(ray.length, t);
            if ((t = Plane::HitRay(Plane(0, -1, 0, mapRect.ymax), ray.origin, ray.direction)) >= 0)
                ray.length = Min<float>(ray.length, t);
            if ((t = Plane::HitRay(Plane(1, 0, 0, -mapRect.xmin), ray.origin, ray.direction)) >= 0) 
                ray.length = Min<float>(ray.length, t);
            if ((t = Plane::HitRay(Plane(-1, 0, 0, mapRect.xmax), ray.origin, ray.direction)) >= 0) 
                ray.length = Min<float>(ray.length, t);
        }    
        
        ImVec2 rayEnd = ImGui::ProjectToScreen(ray.origin + ray.direction*ray.length, worldToClipMat, screenViewport);
        drawList->AddCircle(rayOrigin, 5.0f, COLOR4U_PURPLE.n, 12, 2.0f);
        drawList->AddLine(rayOrigin, rayEnd, COLOR4U_PURPLE.n, 2.0f);    
    }

    for (uint32 i = 0; i < data->numCells; i++) {
        data->cells[i].numRayMarches = 0;
        data->cells[i].numRayhits = 0;
    }
#else
    UNUSED(opacity);
    UNUSED(mode);
    UNUSED(heatmapLimit);
    UNUSED(rays);
    UNUSED(numRays);
#endif
}

void CollisionIsland::DebugShapeBounds()
{
#if CONFIG_DEBUG_COLLISIONS
    CollisionIslandData* data = gCollision.islands.Data(mHandle);

    for (CollisionShapeData& shape : data->shapes) {
        if (shape.type == CollisionShapeType::Box) 
            DebugDraw::DrawAxisAlignedBoundingBox(shape.transformedAABB, COLOR4U_WHITE);
        else if (shape.type == CollisionShapeType::Box)
            DebugDraw::DrawAxisAlignedBoundingBox(shape.aabb, COLOR4U_PURPLE);
    }
#endif
}