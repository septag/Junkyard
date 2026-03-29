#pragma once

#include "../Core/Base.h"
#include "../Core/MathTypes.h"
#include "../Core/Pools.h"
#include "../Config.h"

inline constexpr uint32 COLLISION_MAX_POLYGON_VERTICES = 8; // = C2_MAX_POLYGON_VERTS

DEFINE_HANDLE(CollisionIslandHandle);

using CollisionEntityId = uint32;

// Internally maps to c2Poly in cute_c2.h 
struct CollisionShapePolygon2D
{
    uint32 numVertices;
    Float2 vertices[COLLISION_MAX_POLYGON_VERTICES];
    Float2 normals[COLLISION_MAX_POLYGON_VERTICES];
};

struct CollisionTransform
{
    Float3 position;
    Quat rotation;
};

struct CollisionShapeBox
{
    CollisionTransform transform;
    Float3 extents; /// HalfWidth, HalfHeight, HalfDepth
};

struct CollisionPair
{
    CollisionEntityId entity1;
    CollisionEntityId entity2;
    uint32 mask1;
    uint32 mask2;
};

struct CollisionDetectResult
{
    CollisionPair* pairs;
    uint32 numPairs;
};

struct CollisionQueryResult
{
    CollisionEntityId* entities;
    uint32 numEntities;
};

struct CollisionRay
{
    Float3 origin;
    Float3 direction;
    float length;
};

struct CollisionRayHit
{
    CollisionEntityId entity;
    Float3 normal;
    float t;    // 0..length
};

enum class CollisionShapeType 
{
    Box,
    StaticPoly
};

struct CollisionEntityMaskPair
{
    CollisionEntityId id;
    uint32 mask;
};

PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4201)
struct CollisionShapeData
{
    CollisionShapeType type;
    CollisionEntityMaskPair maskPair;
    AABB aabb;

    union {
        CollisionShapePolygon2D polygon;

        struct {
            CollisionShapeBox box;
            CollisionShapeBox transformedBox;
            AABB transformedAABB;
        };
    };

#if CONFIG_DEBUG_COLLISIONS
    uint64 collisionFrameIdx;
#endif

};
PRAGMA_DIAGNOSTIC_POP()

enum class CollisionDebugMode
{
    Collisions = 0,
    Heatmap,
    EntityHeatmap
};

enum class CollisionDebugRaycastMode
{
    Rayhits = 0,
    RayhitHeatmap,
    RaymarchHeatmap
};

struct CollisionAddBoxDesc
{
    CollisionEntityId id;
    CollisionShapeBox shape;
    CollisionTransform transform;
    uint32 mask;
};

struct CollisionAddPolyDesc
{
    CollisionEntityId id;
    CollisionShapePolygon2D shape;
    uint32 mask;
};

struct CollisionIsland
{
    CollisionIslandHandle mHandle;

    void AddBoxes(uint32 numBoxes, const CollisionAddBoxDesc* boxes);
    void AddBox(const CollisionAddBoxDesc& box);

    void AddStaticPolys(uint32 numPolys, const CollisionAddPolyDesc* polys);
    void AddStaticPoly(const CollisionAddPolyDesc& poly);

    void Remove(uint32 numEntities, const CollisionEntityId* ids);
    void Remove(CollisionEntityId id);
    void RemoveAll();

    void UpdateTransforms(uint32 numEntities, const CollisionEntityId* ids, const CollisionTransform* transforms);
    void UpdateTransform(CollisionEntityId id, const CollisionTransform& transform);

    Span<CollisionPair> DetectCollisions(MemAllocator* alloc = Mem::GetDefaultAlloc());
    Span<CollisionEntityId> IntersectSphere(Float3 center, float radius, uint32 mask, MemAllocator* alloc = Mem::GetDefaultAlloc());
    Span<CollisionEntityId> IntersectPolygon(const CollisionShapePolygon2D& poly, uint32 mask, MemAllocator* alloc = Mem::GetDefaultAlloc());
    Span<CollisionRayHit> IntersectRay(const CollisionRay& ray, uint32 mask, MemAllocator* alloc = Mem::GetDefaultAlloc());

    void DebugCollisionsGUI(float opacity, CollisionDebugMode mode, float heatmapLimit);
    void DebugRaycastGUI(float opacity, CollisionDebugRaycastMode mode, float heatmapLimit);

    uint32 GetCellCount(uint32* outNumCellsX, uint32* outNumCellsY);
    RectFloat GetCellRect(uint32 cellIdx);
    bool GetEntityData(CollisionEntityId id, CollisionShapeData* outData);
};

namespace Collision
{
    API CollisionIsland CreateIsland(RectFloat mapRect, float cellSize);
    API void DestroyIsland(CollisionIsland& island);

    bool Initialize();
    void Release();
}