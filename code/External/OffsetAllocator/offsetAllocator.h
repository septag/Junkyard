// (C) Sebastian Aaltonen 2023
// MIT License (see file: LICENSE)

#pragma once

#ifndef OFFSET_ALLOCATOR_API_DECL
    #define OFFSET_ALLOCATOR_API_DECL
#endif

#ifndef OFFSET_ALLOCATOR_API_IMPL
    #define OFFSET_ALLOCATOR_API_IMPL OFFSET_ALLOCATOR_API_DECL
#endif

#include <stdint.h>
#include <stdbool.h>

// 16 bit offsets mode will halve the metadata storage cost
// But it only supports up to 65536 maximum allocation count
#ifdef OFFSET_ALLOCATOR_USE_16_BIT_INDICES
    typedef uint16_t OffsetAllocatorNodeIndex;
#else
    typedef uint32_t OffsetAllocatorNodeIndex;
#endif

#define OFFSET_ALLOCATOR_NUM_TOP_BINS 32
#define OFFSET_ALLOCATOR_BINS_PER_LEAF 8
#define OFFSET_ALLOCATOR_NUM_LEAF_BINS (OFFSET_ALLOCATOR_NUM_TOP_BINS * OFFSET_ALLOCATOR_BINS_PER_LEAF)
#define OFFSET_ALLOCATOR_NO_SPACE 0xffffffff

typedef struct OffsetAllocatorAllocation
{
    uint32_t offset;
    OffsetAllocatorNodeIndex metadata; // internal: node index
} OffsetAllocatorAllocation;

typedef struct OffsetAllocatorStorageReport
{
    uint32_t totalFreeSpace;
    uint32_t largestFreeRegion;
} OffsetAllocatorStorageReport;

typedef struct OffsetAllocatorRegion
{
    uint32_t size;
    uint32_t count;
} OffsetAllocatorRegion;

typedef struct OffsetAllocatorStorageReportFull
{
    OffsetAllocatorRegion freeRegions[OFFSET_ALLOCATOR_NUM_LEAF_BINS];
} OffsetAllocatorStorageReportFull;

typedef struct OffsetAllocator OffsetAllocator;

#ifdef __cplusplus
extern "C" {
#endif

OFFSET_ALLOCATOR_API_DECL size_t OffsetAllocator_GetRequiredBytes(uint32_t maxAllocs);
OFFSET_ALLOCATOR_API_DECL OffsetAllocator* OffsetAllocator_Create(uint32_t maxSize, uint32_t maxAllocs, void* buffer, size_t bufferSize);
OFFSET_ALLOCATOR_API_DECL void OffsetAllocator_Destroy(OffsetAllocator* allocator);
OFFSET_ALLOCATOR_API_DECL void OffsetAllocator_Reset(OffsetAllocator* allocator);

OFFSET_ALLOCATOR_API_DECL bool OffsetAllocator_Allocate(OffsetAllocator* allocator, uint32_t size, OffsetAllocatorAllocation* allocation);
OFFSET_ALLOCATOR_API_DECL void OffsetAllocator_Free(OffsetAllocator* allocator, OffsetAllocatorAllocation* allocation);

OFFSET_ALLOCATOR_API_DECL uint32_t OffsetAllocator_GetAllocationSize(OffsetAllocator* allocator, const OffsetAllocatorAllocation* allocation);
OFFSET_ALLOCATOR_API_DECL void OffsetAllocator_GetStorageReport(OffsetAllocator* allocator, OffsetAllocatorStorageReport* report);
OFFSET_ALLOCATOR_API_DECL void OffsetAllocator_GetStorageReportFull(OffsetAllocator* allocator, OffsetAllocatorStorageReportFull* report);

#ifdef __cplusplus
}
#endif

#ifdef OFFSET_ALLOCATOR_IMPLEMENT

// (C) Sebastian Aaltonen 2023
// MIT License (see file: LICENSE)

#if defined(DEBUG) || defined(_DEBUG)
    #ifndef OFFSET_ALLOCATOR_ASSERT
        #include <assert.h>
        #define OFFSET_ALLOCATOR_ASSERT(x) assert(x)
    #endif
#else
    #ifndef OFFSET_ALLOCATOR_ASSERT
        #define OFFSET_ALLOCATOR_ASSERT(x)
    #endif
#endif

#ifdef OFFSET_ALLOCATOR_DEBUG_VERBOSE
    #include <stdio.h>
#endif

#ifdef _MSC_VER
    #include <intrin.h>
#endif

#include <string.h>

#define OFFSET_ALLOCATOR_MANTISSA_BITS 3
#define OFFSET_ALLOCATOR_MANTISSA_VALUE (1 << OFFSET_ALLOCATOR_MANTISSA_BITS)
#define OFFSET_ALLOCATOR_MANTISSA_MASK (OFFSET_ALLOCATOR_MANTISSA_VALUE - 1)
#define OFFSET_ALLOCATOR_TOP_BINS_INDEX_SHIFT 3
#define OFFSET_ALLOCATOR_LEAF_BINS_INDEX_MASK 0x7

#define OFFSET_ALLOCATOR_UNUSED 0xffffffff

typedef struct OffsetAllocatorNode
{
    uint32_t dataOffset;
    uint32_t dataSize;
    OffsetAllocatorNodeIndex binListPrev;
    OffsetAllocatorNodeIndex binListNext;
    OffsetAllocatorNodeIndex neighborPrev;
    OffsetAllocatorNodeIndex neighborNext;
    bool used; // TODO: Merge as bit flag
} OffsetAllocatorNode;

typedef struct OffsetAllocator
{
    uint32_t maxSize;
    uint32_t maxAllocs;
    uint32_t freeStorage;

    uint32_t usedBinsTop;
    uint8_t usedBins[OFFSET_ALLOCATOR_NUM_TOP_BINS];
    OffsetAllocatorNodeIndex binIndices[OFFSET_ALLOCATOR_NUM_LEAF_BINS];
                
    OffsetAllocatorNode* nodes;
    OffsetAllocatorNodeIndex* freeNodes;
    uint32_t freeOffset;
} OffsetAllocator;

static inline uint32_t OffsetAllocator_lzcnt_nonzero(uint32_t v)
{
#ifdef _MSC_VER
    unsigned long retVal;
    _BitScanReverse(&retVal, v);
    return 31 - retVal;
#else
    return __builtin_clz(v);
#endif
}

static inline uint32_t OffsetAllocator_tzcnt_nonzero(uint32_t v)
{
#ifdef _MSC_VER
    unsigned long retVal;
    _BitScanForward(&retVal, v);
    return retVal;
#else
    return __builtin_ctz(v);
#endif
}

// Bin sizes follow floating point (exponent + mantissa) distribution (piecewise linear log approx)
// This ensures that for each size class, the average overhead percentage stays the same
static uint32_t OffsetAllocator_UintToFloatRoundUp(uint32_t size)
{
    uint32_t exp = 0;
    uint32_t mantissa = 0;
            
    if (size < OFFSET_ALLOCATOR_MANTISSA_VALUE)
    {
        // Denorm: 0..(MANTISSA_VALUE-1)
        mantissa = size;
    }
    else
    {
        // Normalized: Hidden high bit always 1. Not stored. Just like float.
        uint32_t leadingZeros = OffsetAllocator_lzcnt_nonzero(size);
        uint32_t highestSetBit = 31 - leadingZeros;
                
        uint32_t mantissaStartBit = highestSetBit - OFFSET_ALLOCATOR_MANTISSA_BITS;
        exp = mantissaStartBit + 1;
        mantissa = (size >> mantissaStartBit) & OFFSET_ALLOCATOR_MANTISSA_MASK;
                
        uint32_t lowBitsMask = (1 << mantissaStartBit) - 1;
                
        // Round up!
        if ((size & lowBitsMask) != 0)
            mantissa++;
    }
            
    return (exp << OFFSET_ALLOCATOR_MANTISSA_BITS) + mantissa; // + allows mantissa->exp overflow for round up
}

static uint32_t OffsetAllocator_UintToFloatRoundDown(uint32_t size)
{
    uint32_t exp = 0;
    uint32_t mantissa = 0;
            
    if (size < OFFSET_ALLOCATOR_MANTISSA_VALUE)
    {
        // Denorm: 0..(MANTISSA_VALUE-1)
        mantissa = size;
    }
    else
    {
        // Normalized: Hidden high bit always 1. Not stored. Just like float.
        uint32_t leadingZeros = OffsetAllocator_lzcnt_nonzero(size);
        uint32_t highestSetBit = 31 - leadingZeros;
                
        uint32_t mantissaStartBit = highestSetBit - OFFSET_ALLOCATOR_MANTISSA_BITS;
        exp = mantissaStartBit + 1;
        mantissa = (size >> mantissaStartBit) & OFFSET_ALLOCATOR_MANTISSA_MASK;
    }
            
    return (exp << OFFSET_ALLOCATOR_MANTISSA_BITS) | mantissa;
}
    
static uint32_t OffsetAllocator_FloatToUint(uint32_t floatValue)
{
    uint32_t exponent = floatValue >> OFFSET_ALLOCATOR_MANTISSA_BITS;
    uint32_t mantissa = floatValue & OFFSET_ALLOCATOR_MANTISSA_MASK;
    if (exponent == 0)
    {
        // Denorms
        return mantissa;
    }
    else
    {
        return (mantissa | OFFSET_ALLOCATOR_MANTISSA_VALUE) << (exponent - 1);
    }
}

// Utility functions
static uint32_t OffsetAllocator_FindLowestSetBitAfter(uint32_t bitMask, uint32_t startBitIndex)
{
    uint32_t maskBeforeStartIndex = (1 << startBitIndex) - 1;
    uint32_t maskAfterStartIndex = ~maskBeforeStartIndex;
    uint32_t bitsAfter = bitMask & maskAfterStartIndex;
    if (bitsAfter == 0) return OFFSET_ALLOCATOR_NO_SPACE;
    return OffsetAllocator_tzcnt_nonzero(bitsAfter);
}

//----------------------------------------------------------------------------------------------------------------------
// Allocator...

static uint32_t OffsetAllocator_InsertNodeIntoBin(OffsetAllocator* allocator, uint32_t size, uint32_t dataOffset)
{
    // Round down to bin index to ensure that bin >= alloc
    uint32_t binIndex = OffsetAllocator_UintToFloatRoundDown(size);
        
    uint32_t topBinIndex = binIndex >> OFFSET_ALLOCATOR_TOP_BINS_INDEX_SHIFT;
    uint32_t leafBinIndex = binIndex & OFFSET_ALLOCATOR_LEAF_BINS_INDEX_MASK;
        
    // Bin was empty before?
    if (allocator->binIndices[binIndex] == OFFSET_ALLOCATOR_UNUSED)
    {
        // Set bin mask bits
        allocator->usedBins[topBinIndex] |= 1 << leafBinIndex;
        allocator->usedBinsTop |= 1 << topBinIndex;
    }
        
    // Take a freelist node and insert on top of the bin linked list (next = old top)
    uint32_t topNodeIndex = allocator->binIndices[binIndex];
    uint32_t nodeIndex = allocator->freeNodes[allocator->freeOffset--];
#ifdef OFFSET_ALLOCATOR_DEBUG_VERBOSE
    printf("Getting node %u from freelist[%u]\n", nodeIndex, allocator->freeOffset + 1);
#endif
    OffsetAllocatorNode node = {
        .dataOffset = dataOffset, 
        .dataSize = size, 
        .binListNext = topNodeIndex
    };
    allocator->nodes[nodeIndex] = node;
    if (topNodeIndex != OFFSET_ALLOCATOR_UNUSED) allocator->nodes[topNodeIndex].binListPrev = nodeIndex;
    allocator->binIndices[binIndex] = nodeIndex;
        
    allocator->freeStorage += size;
#ifdef OFFSET_ALLOCATOR_DEBUG_VERBOSE
    printf("Free storage: %u (+%u) (insertNodeIntoBin)\n", allocator->freeStorage, size);
#endif

    return nodeIndex;
}
    
static void OffsetAllocator_RemoveNodeFromBin(OffsetAllocator* allocator, uint32_t nodeIndex)
{
    OffsetAllocatorNode* node = &allocator->nodes[nodeIndex];
        
    if (node->binListPrev != OFFSET_ALLOCATOR_UNUSED)
    {
        // Easy case: We have previous node-> Just remove this node from the middle of the list.
        allocator->nodes[node->binListPrev].binListNext = node->binListNext;
        if (node->binListNext != OFFSET_ALLOCATOR_UNUSED) allocator->nodes[node->binListNext].binListPrev = node->binListPrev;
    }
    else
    {
        // Hard case: We are the first node in a bin. Find the bin.
            
        // Round down to bin index to ensure that bin >= alloc
        uint32_t binIndex = OffsetAllocator_UintToFloatRoundDown(node->dataSize);
            
        uint32_t topBinIndex = binIndex >> OFFSET_ALLOCATOR_TOP_BINS_INDEX_SHIFT;
        uint32_t leafBinIndex = binIndex & OFFSET_ALLOCATOR_LEAF_BINS_INDEX_MASK;
            
        allocator->binIndices[binIndex] = node->binListNext;
        if (node->binListNext != OFFSET_ALLOCATOR_UNUSED) allocator->nodes[node->binListNext].binListPrev = OFFSET_ALLOCATOR_UNUSED;

        // Bin empty?
        if (allocator->binIndices[binIndex] == OFFSET_ALLOCATOR_UNUSED)
        {
            // Remove a leaf bin mask bit
            allocator->usedBins[topBinIndex] &= ~(1 << leafBinIndex);
                
            // All leaf bins empty?
            if (allocator->usedBins[topBinIndex] == 0)
            {
                // Remove a top bin mask bit
                allocator->usedBinsTop &= ~(1 << topBinIndex);
            }
        }
    }
        
    // Insert the node to freelist
#ifdef OFFSET_ALLOCATOR_DEBUG_VERBOSE
    printf("Putting node %u into freelist[%u] (removeNodeFromBin)\n", nodeIndex, allocator->freeOffset + 1);
#endif
    allocator->freeNodes[++allocator->freeOffset] = nodeIndex;

    allocator->freeStorage -= node->dataSize;
#ifdef OFFSET_ALLOCATOR_DEBUG_VERBOSE
    printf("Free storage: %u (-%u) (removeNodeFromBin)\n", allocator->freeStorage, node->dataSize);
#endif
}

OFFSET_ALLOCATOR_API_IMPL 
size_t OffsetAllocator_GetRequiredBytes(uint32_t maxAllocs)
{
#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable: 4127)
#endif
    if (sizeof(OffsetAllocatorNodeIndex) == (size_t)2) 
    {
        OFFSET_ALLOCATOR_ASSERT(maxAllocs <= 65536);
    }
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

    return sizeof(OffsetAllocator) + (sizeof(OffsetAllocatorNode) + sizeof(OffsetAllocatorNodeIndex))*maxAllocs;
}

OFFSET_ALLOCATOR_API_IMPL 
OffsetAllocator* OffsetAllocator_Create(uint32_t maxSize, uint32_t maxAllocs, void* buffer, size_t bufferSize)
{
    OFFSET_ALLOCATOR_ASSERT(maxSize);
    OFFSET_ALLOCATOR_ASSERT(maxAllocs);

    (void)(bufferSize);
    OFFSET_ALLOCATOR_ASSERT(OffsetAllocator_GetRequiredBytes(maxAllocs) <= bufferSize);

    uint8_t* bufferPtr = (uint8_t*)buffer;
    OffsetAllocator* allocator = (OffsetAllocator*)bufferPtr;
    bufferPtr += sizeof(OffsetAllocator);

    memset(allocator, 0x0, sizeof(OffsetAllocator));

    allocator->maxSize = maxSize;
    allocator->maxAllocs = maxAllocs;
    allocator->freeOffset = maxAllocs - 1;

    for (uint32_t i = 0 ; i < OFFSET_ALLOCATOR_NUM_TOP_BINS; i++)
        allocator->usedBins[i] = 0;
        
    for (uint32_t i = 0 ; i < OFFSET_ALLOCATOR_NUM_LEAF_BINS; i++)
        allocator->binIndices[i] = OFFSET_ALLOCATOR_UNUSED;

    allocator->nodes = (OffsetAllocatorNode*)bufferPtr;
    bufferPtr += sizeof(OffsetAllocatorNode)*maxAllocs;

    allocator->freeNodes = (OffsetAllocatorNodeIndex*)bufferPtr;

    // Freelist is a stack. Nodes in inverse order so that [0] pops first.
    for (uint32_t i = 0; i < maxAllocs; i++)
    {
        OffsetAllocatorNode* node = &allocator->nodes[i];
        node->dataOffset = 0;
        node->dataSize = 0;
        node->binListPrev = OFFSET_ALLOCATOR_UNUSED;
        node->binListNext = OFFSET_ALLOCATOR_UNUSED;
        node->neighborPrev = OFFSET_ALLOCATOR_UNUSED;
        node->neighborNext = OFFSET_ALLOCATOR_UNUSED;
        node->used = false;

        allocator->freeNodes[i] = maxAllocs - i - 1;
    }
        
    // Start state: Whole storage as one big node
    // Algorithm will split remainders and push them back as smaller nodes
    OffsetAllocator_InsertNodeIntoBin(allocator, maxSize, 0);

    return allocator;
}

OFFSET_ALLOCATOR_API_IMPL 
void OffsetAllocator_Destroy(OffsetAllocator* allocator)
{
    memset(allocator, 0x0, sizeof(*allocator));
}


OFFSET_ALLOCATOR_API_IMPL 
void OffsetAllocator_Reset(OffsetAllocator* allocator)
{
    allocator->freeStorage = 0;
    allocator->usedBinsTop = 0;
    allocator->freeOffset = allocator->maxAllocs - 1;

    for (uint32_t i = 0 ; i < OFFSET_ALLOCATOR_NUM_TOP_BINS; i++)
        allocator->usedBins[i] = 0;
        
    for (uint32_t i = 0 ; i < OFFSET_ALLOCATOR_NUM_LEAF_BINS; i++)
        allocator->binIndices[i] = OFFSET_ALLOCATOR_UNUSED;

    // Freelist is a stack. Nodes in inverse order so that [0] pops first.
    for (uint32_t i = 0; i < allocator->maxAllocs; i++)
    {
        OffsetAllocatorNode* node = &allocator->nodes[i];
        node->dataOffset = 0;
        node->dataSize = 0;
        node->binListPrev = OFFSET_ALLOCATOR_UNUSED;
        node->binListNext = OFFSET_ALLOCATOR_UNUSED;
        node->neighborPrev = OFFSET_ALLOCATOR_UNUSED;
        node->neighborNext = OFFSET_ALLOCATOR_UNUSED;
        node->used = false;

        allocator->freeNodes[i] = allocator->maxAllocs - i - 1;
    }

    // Start state: Whole storage as one big node
    // Algorithm will split remainders and push them back as smaller nodes
    OffsetAllocator_InsertNodeIntoBin(allocator, allocator->maxSize, 0);
}

OFFSET_ALLOCATOR_API_IMPL 
bool OffsetAllocator_Allocate(OffsetAllocator* allocator, uint32_t size, OffsetAllocatorAllocation* allocation)
{
    OFFSET_ALLOCATOR_ASSERT(allocator);
    OFFSET_ALLOCATOR_ASSERT(allocation);
    OFFSET_ALLOCATOR_ASSERT(size);

    static OffsetAllocatorAllocation NoSpaceAlloc = {
        .offset = OFFSET_ALLOCATOR_NO_SPACE, 
        .metadata = OFFSET_ALLOCATOR_NO_SPACE
    };

    // Out of allocations?
    if (allocator->freeOffset == 0)
    {
        *allocation = NoSpaceAlloc;
        return false;
    }
        
    // Round up to bin index to ensure that alloc >= bin
    // Gives us min bin index that fits the size
    uint32_t minBinIndex = OffsetAllocator_UintToFloatRoundUp(size);
        
    uint32_t minTopBinIndex = minBinIndex >> OFFSET_ALLOCATOR_TOP_BINS_INDEX_SHIFT;
    uint32_t minLeafBinIndex = minBinIndex & OFFSET_ALLOCATOR_LEAF_BINS_INDEX_MASK;
        
    uint32_t topBinIndex = minTopBinIndex;
    uint32_t leafBinIndex = OFFSET_ALLOCATOR_NO_SPACE;

    // If top bin exists, scan its leaf bin. This can fail (NO_SPACE).
    if (allocator->usedBinsTop & (1 << topBinIndex))
    {
        leafBinIndex = OffsetAllocator_FindLowestSetBitAfter(allocator->usedBins[topBinIndex], minLeafBinIndex);
    }
    
    // If we didn't find space in top bin, we search top bin from +1
    if (leafBinIndex == OFFSET_ALLOCATOR_NO_SPACE)
    {
        topBinIndex = OffsetAllocator_FindLowestSetBitAfter(allocator->usedBinsTop, minTopBinIndex + 1);
            
        // Out of space?
        if (topBinIndex == OFFSET_ALLOCATOR_NO_SPACE)
        {
            *allocation = NoSpaceAlloc;
            return false;
        }

        // All leaf bins here fit the alloc, since the top bin was rounded up. Start leaf search from bit 0.
        // NOTE: This search can't fail since at least one leaf bit was set because the top bit was set.
        leafBinIndex = OffsetAllocator_tzcnt_nonzero(allocator->usedBins[topBinIndex]);
    }
                
    uint32_t binIndex = (topBinIndex << OFFSET_ALLOCATOR_TOP_BINS_INDEX_SHIFT) | leafBinIndex;
        
    // Pop the top node of the bin. Bin top = node->next.
    uint32_t nodeIndex = allocator->binIndices[binIndex];
    OffsetAllocatorNode* node = &allocator->nodes[nodeIndex];
    uint32_t nodeTotalSize = node->dataSize;
    node->dataSize = size;
    node->used = true;
    allocator->binIndices[binIndex] = node->binListNext;
    if (node->binListNext != OFFSET_ALLOCATOR_UNUSED) 
        allocator->nodes[node->binListNext].binListPrev = OFFSET_ALLOCATOR_UNUSED;
    allocator->freeStorage -= nodeTotalSize;
#ifdef OFFSET_ALLOCATOR_DEBUG_VERBOSE
    printf("Free storage: %u (-%u) (allocate)\n", allocator->freeStorage, nodeTotalSize);
#endif

    // Bin empty?
    if (allocator->binIndices[binIndex] == OFFSET_ALLOCATOR_UNUSED)
    {
        // Remove a leaf bin mask bit
        allocator->usedBins[topBinIndex] &= ~(1 << leafBinIndex);
            
        // All leaf bins empty?
        if (allocator->usedBins[topBinIndex] == 0)
        {
            // Remove a top bin mask bit
            allocator->usedBinsTop &= ~(1 << topBinIndex);
        }
    }
        
    // Push back reminder N elements to a lower bin
    uint32_t reminderSize = nodeTotalSize - size;
    if (reminderSize > 0)
    {
        uint32_t newNodeIndex = OffsetAllocator_InsertNodeIntoBin(allocator, reminderSize, node->dataOffset + size);
            
        // Link nodes next to each other so that we can merge them later if both are free
        // And update the old next neighbor to point to the new node (in middle)
        if (node->neighborNext != OFFSET_ALLOCATOR_UNUSED) allocator->nodes[node->neighborNext].neighborPrev = newNodeIndex;
        allocator->nodes[newNodeIndex].neighborPrev = nodeIndex;
        allocator->nodes[newNodeIndex].neighborNext = node->neighborNext;
        node->neighborNext = newNodeIndex;
    }
    
    allocation->offset = node->dataOffset;
    allocation->metadata = nodeIndex;
    return true;
}
    
OFFSET_ALLOCATOR_API_IMPL 
void OffsetAllocator_Free(OffsetAllocator* allocator, OffsetAllocatorAllocation* allocation)
{
    OFFSET_ALLOCATOR_ASSERT(allocation);
    OFFSET_ALLOCATOR_ASSERT(allocation->metadata != OFFSET_ALLOCATOR_NO_SPACE);
    if (!allocator->nodes) 
        return;
        
    uint32_t nodeIndex = allocation->metadata;
    OffsetAllocatorNode* node = &allocator->nodes[nodeIndex];
        
    // Double delete check
    OFFSET_ALLOCATOR_ASSERT(node->used == true);
        
    // Merge with neighbors...
    uint32_t offset = node->dataOffset;
    uint32_t size = node->dataSize;
        
    if ((node->neighborPrev != OFFSET_ALLOCATOR_UNUSED) && (allocator->nodes[node->neighborPrev].used == false))
    {
        // Previous (contiguous) free node: Change offset to previous node offset. Sum sizes
        OffsetAllocatorNode* prevNode = &allocator->nodes[node->neighborPrev];
        offset = prevNode->dataOffset;
        size += prevNode->dataSize;
            
        // Remove node from the bin linked list and put it in the freelist
        OffsetAllocator_RemoveNodeFromBin(allocator, node->neighborPrev);
            
        OFFSET_ALLOCATOR_ASSERT(prevNode->neighborNext == nodeIndex);
        node->neighborPrev = prevNode->neighborPrev;
    }
        
    if ((node->neighborNext != OFFSET_ALLOCATOR_UNUSED) && (allocator->nodes[node->neighborNext].used == false))
    {
        // Next (contiguous) free node: Offset remains the same. Sum sizes.
        OffsetAllocatorNode* nextNode = &allocator->nodes[node->neighborNext];
        size += nextNode->dataSize;
            
        // Remove node from the bin linked list and put it in the freelist
        OffsetAllocator_RemoveNodeFromBin(allocator, node->neighborNext);
            
        OFFSET_ALLOCATOR_ASSERT(nextNode->neighborPrev == nodeIndex);
        node->neighborNext = nextNode->neighborNext;
    }

    uint32_t neighborNext = node->neighborNext;
    uint32_t neighborPrev = node->neighborPrev;
        
    // Insert the removed node to freelist
#ifdef OFFSET_ALLOCATOR_DEBUG_VERBOSE
    printf("Putting node %u into freelist[%u] (free)\n", nodeIndex, allocator->freeOffset + 1);
#endif
    allocator->freeNodes[++allocator->freeOffset] = nodeIndex;

    // Insert the (combined) free node to bin
    uint32_t combinedNodeIndex = OffsetAllocator_InsertNodeIntoBin(allocator, size, offset);

    // Connect neighbors with the new combined node
    if (neighborNext != OFFSET_ALLOCATOR_UNUSED)
    {
        allocator->nodes[combinedNodeIndex].neighborNext = neighborNext;
        allocator->nodes[neighborNext].neighborPrev = combinedNodeIndex;
    }
    if (neighborPrev != OFFSET_ALLOCATOR_UNUSED)
    {
        allocator->nodes[combinedNodeIndex].neighborPrev = neighborPrev;
        allocator->nodes[neighborPrev].neighborNext = combinedNodeIndex;
    }
}

OFFSET_ALLOCATOR_API_IMPL 
uint32_t OffsetAllocator_GetAllocationSize(OffsetAllocator* allocator, const OffsetAllocatorAllocation* allocation)
{
    if (allocation->metadata == OFFSET_ALLOCATOR_NO_SPACE) return 0;
    if (!allocator->nodes) return 0;
        
    return allocator->nodes[allocation->metadata].dataSize;
}

OFFSET_ALLOCATOR_API_IMPL 
void OffsetAllocator_GetStorageReport(OffsetAllocator* allocator, OffsetAllocatorStorageReport* report)
{
    uint32_t largestFreeRegion = 0;
    uint32_t freeStorage = 0;
        
    // Out of allocations? -> Zero free space
    if (allocator->freeOffset > 0)
    {
        freeStorage = allocator->freeStorage;
        if (allocator->usedBinsTop)
        {
            uint32_t topBinIndex = 31 - OffsetAllocator_lzcnt_nonzero(allocator->usedBinsTop);
            uint32_t leafBinIndex = 31 - OffsetAllocator_lzcnt_nonzero(allocator->usedBins[topBinIndex]);
            largestFreeRegion = OffsetAllocator_FloatToUint((topBinIndex << OFFSET_ALLOCATOR_TOP_BINS_INDEX_SHIFT) | leafBinIndex);
            OFFSET_ALLOCATOR_ASSERT(freeStorage >= largestFreeRegion);
        }
    }

    report->totalFreeSpace = freeStorage;
    report->largestFreeRegion = largestFreeRegion;
}

OFFSET_ALLOCATOR_API_DECL 
void OffsetAllocator_GetStorageReportFull(OffsetAllocator* allocator, OffsetAllocatorStorageReportFull* report)
{
    for (uint32_t i = 0; i < OFFSET_ALLOCATOR_NUM_LEAF_BINS; i++)
    {
        uint32_t count = 0;
        uint32_t nodeIndex = allocator->binIndices[i];
        while (nodeIndex != OFFSET_ALLOCATOR_UNUSED)
        {
            nodeIndex = allocator->nodes[nodeIndex].binListNext;
            count++;
        }
        report->freeRegions[i].size = OffsetAllocator_FloatToUint(i);
        report->freeRegions[i].count = count;
    }
}

#endif // OFFSET_ALLOCATOR_IMPLEMENT