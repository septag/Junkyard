// (C) Sebastian Aaltonen 2023
// MIT License (see file: LICENSE)

#pragma once

//#define USE_16_BIT_OFFSETS

namespace OffsetAllocator
{
    typedef unsigned char uint8;
    typedef unsigned short uint16;
    typedef unsigned int uint32;

    // 16 bit offsets mode will halve the metadata storage cost
    // But it only supports up to 65536 maximum allocation count
#ifdef USE_16_BIT_NODE_INDICES
    typedef uint16 NodeIndex;
#else
    typedef uint32 NodeIndex;
#endif

    inline constexpr uint32 NUM_TOP_BINS = 32;
    inline constexpr uint32 BINS_PER_LEAF = 8;
    inline constexpr uint32 TOP_BINS_INDEX_SHIFT = 3;
    inline constexpr uint32 LEAF_BINS_INDEX_MASK = 0x7;
    inline constexpr uint32 NUM_LEAF_BINS = NUM_TOP_BINS * BINS_PER_LEAF;

    struct HeapAllocationOverride
    {
        using AllocateCallback = void*(*)(size_t size, void* userData);
        using FreeCallback = void(*)(void* ptr, void* userData);

        AllocateCallback allocateFn;
        FreeCallback freeFn;
    };

    struct Allocation
    {
        static constexpr uint32 NO_SPACE = 0xffffffff;
        
        uint32 offset = NO_SPACE;
        NodeIndex metadata = NO_SPACE; // internal: node index
    };

    struct StorageReport
    {
        uint32 totalFreeSpace;
        uint32 largestFreeRegion;
    };

    struct StorageReportFull
    {
        struct Region
        {
            uint32 size;
            uint32 count;
        };
        
        Region freeRegions[NUM_LEAF_BINS];
    };

    class Allocator
    {
    public:
        Allocator(uint32 size, uint32 maxAllocs = 128 * 1024, 
                  HeapAllocationOverride* heapOverride = nullptr, void* heapOverrideUserData = nullptr);
        Allocator(Allocator &&other);
        ~Allocator();
        void reset();
        
        Allocation allocate(uint32 size);
        void free(Allocation allocation);

        uint32 allocationSize(Allocation allocation) const;
        StorageReport storageReport() const;
        StorageReportFull storageReportFull() const;
        
    private:
        uint32 insertNodeIntoBin(uint32 size, uint32 dataOffset);
        void removeNodeFromBin(uint32 nodeIndex);

        struct Node
        {
            static constexpr NodeIndex UNUSED = 0xffffffff;
            
            uint32 dataOffset;
            uint32 dataSize;
            NodeIndex binListPrev;
            NodeIndex binListNext;
            NodeIndex neighborPrev;
            NodeIndex neighborNext;
            bool used; // TODO: Merge as bit flag
        };

        HeapAllocationOverride* m_heapOverride;
        void* m_heapOverrideUserData;

        uint32 m_size;
        uint32 m_maxAllocs;
        uint32 m_freeStorage;

        uint32 m_usedBinsTop;
        uint8 m_usedBins[NUM_TOP_BINS];
        NodeIndex m_binIndices[NUM_LEAF_BINS];
                
        Node* m_nodes;
        NodeIndex* m_freeNodes;
        uint32 m_freeOffset;
    };
}
