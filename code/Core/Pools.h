#pragma once

#include "Arrays.h"
#include "Allocators.h"

//----------------------------------------------------------------------------------------------------------------------
// Handle Pool
namespace _private 
{
    // change number of kHandleGenBits to have more generation range
    // Whatever the GenBits is, max gen would be 2^GenBits-1 and max index would be 2^(32-GenBits)-1
    // Handle = [<--- high-bits: Generation --->][<--- low-bits: Index -->]
    static inline constexpr uint32 kHandleGenBits = 14;
    static inline constexpr uint32 kHandleIndexMask = (1 << (32 - kHandleGenBits)) - 1;
    static inline constexpr uint32 kHandleGenMask = (1 << kHandleGenBits) - 1;
    static inline constexpr uint32 kHandleGenShift  = 32 - kHandleGenBits;

    struct alignas(16) HandlePoolTable
    {
        uint32  count;
        uint32  capacity;
        uint32* dense;          // actual handles are stored in 'dense' array [0..arrayCount]
        uint32* sparse;         // indices to dense for removal lookup [0..arrayCapacity]
        uint8   padding[sizeof(void*)];
    };

    API HandlePoolTable* handleCreatePoolTable(uint32 capacity, Allocator* alloc);
    API void handleDestroyPoolTable(HandlePoolTable* tbl, Allocator* alloc);
    API bool handleGrowPoolTable(HandlePoolTable** pTbl, Allocator* alloc);
    API HandlePoolTable* handleClone(HandlePoolTable* tbl, Allocator* alloc);

    API uint32 handleNew(HandlePoolTable* tbl);
    API void   handleDel(HandlePoolTable* tbl, uint32 handle);
    API void   handleResetPoolTable(HandlePoolTable* tbl);
    API bool   handleIsValid(const HandlePoolTable* tbl, uint32 handle);
    API uint32 handleAt(const HandlePoolTable* tbl, uint32 index);
    API bool   handleFull(const HandlePoolTable* tbl);

    API size_t handleGetMemoryRequirement(uint32 capacity);
    API HandlePoolTable* handleCreatePoolTableWithBuffer(uint32 capacity, void* buff, size_t size);
    API bool handleGrowPoolTableWithBuffer(HandlePoolTable** pTbl, void* buff, size_t size);
} // _private

// TODO: Apple declares "Handle" type in MacTypes.h, so we cannot use C++ handle-pool types in ObjectiveC code
//       However, it's still possible to use the C _private API for ObjC
#ifndef __OBJC__
template <typename _T>
struct Handle
{
    Handle() = default;
    Handle(const Handle<_T>&) = default;
    explicit Handle(uint32 _id) : mId(_id) {}
    Handle<_T>& operator=(const Handle<_T>&) = default;

    void Set(uint32 gen, uint32 index) { mId = ((gen & _private::kHandleGenMask)<<_private::kHandleGenShift) | (index&_private::kHandleIndexMask); }
    explicit operator uint32() const { return mId; }
    uint32 GetSparseIndex() { return mId & _private::kHandleIndexMask; }
    uint32 GetGen() { return (mId >> _private::kHandleGenShift) & _private::kHandleGenMask; }
    bool IsValid() const { return mId != 0; }
    bool operator==(const Handle<_T>& v) const { return mId == v.mId; }
    bool operator!=(const Handle<_T>& v) const { return mId != v.mId; }

    uint32 mId = 0;
};

#define DEFINE_HANDLE(_Name) struct _Name##T; using _Name = Handle<_Name##T>

template <typename _HandleType, typename _DataType, uint32 _Reserve = 32>
struct HandlePool
{
    HandlePool() : HandlePool(memDefaultAlloc()) {}
    explicit HandlePool(Allocator* alloc) : mAlloc(alloc), mItems(alloc) {}
    explicit HandlePool(void* data, size_t size); 

    void CopyTo(HandlePool<_HandleType, _DataType, _Reserve>* otherPool) const;

    [[nodiscard]] _HandleType Add(const _DataType& item, _DataType* prevItem = nullptr);
    void Remove(_HandleType handle);
    uint32 Count() const;
    void Clear();
    bool IsValid(_HandleType handle);
    _HandleType HandleAt(uint32 index);
    _DataType& Data(uint32 index);
    _DataType& Data(_HandleType handle);
    bool IsFull() const;
    uint32 Capacity() const;

    void Reserve(uint32 capacity, void* buffer, size_t size);
    void SetAllocator(Allocator* alloc);
    void Free();

    static size_t GetMemoryRequirement(uint32 capacity = _Reserve);
    bool Grow();
    bool Grow(void* data, size_t size);

    // _Func = [](const _DataType&)->bool
    template <typename _Func> _HandleType FindIf(_Func findFunc);

    // C++ stl crap compatibility. we just want to use for(auto t : array) syntax sugar
    struct Iterator 
    {
        using HandlePool_t = HandlePool<_HandleType, _DataType, _Reserve>;

        Iterator(HandlePool_t* pool, uint32 index) : _pool(pool), mIndex(index) {}
        _DataType& operator*() { return _pool->Data(mIndex); }
        void operator++() { ++mIndex; }
        bool operator!=(Iterator it) { return mIndex != it.mIndex; }
        HandlePool_t* _pool;
        uint32 mIndex;
    };
    
    Iterator begin()    { return Iterator(this, 0); }
    Iterator end()      { return Iterator(this, mHandles ? mHandles->count : 0); }

private:
    Allocator*                  mAlloc = nullptr;
    _private::HandlePoolTable*  mHandles = nullptr;
    Array<_DataType, _Reserve>  mItems;
};

#endif  // !__OBJC__

//----------------------------------------------------------------------------------------------------------------------
// FixedSizePool

template <typename _T, uint32 _Align = CONFIG_MACHINE_ALIGNMENT>
struct FixedSizePool
{
    FixedSizePool() : FixedSizePool(memDefaultAlloc()) {}
    explicit FixedSizePool(Allocator* alloc) : mAlloc(alloc) {}
    explicit FixedSizePool(void* buffer, size_t size);
    
    void SetAllocator(Allocator* alloc);
    void Reserve(uint32 pageSize);
    void Reserve(void* buffer, size_t size, uint32 pageSize);
    void Free();
    static size_t GetMemoryRequirement(uint32 pageSize);

    _T* New();
    void Delete(_T* item);
    bool IsFull() const;

private:
    struct Page
    {
        _T**    ptrs;
        _T*     data;
        Page*   next;
        uint32  index;
    };

    Page* CreatePage(void* buffer, size_t size);

public:
    // To Iterate over all items in the pool
    struct Iterator 
    {
        Iterator(Page* page, uint32 index, uint32 pageSize) : mPage(page), mIndex(index), mPageSize(pageSize) {}
        _T& operator*() { return mPage->data[mIndex]; }
        void operator++() 
        { 
            ASSERT(mPage); 
            if (mIndex < mPageSize) 
            mIndex++; 
            else { 
                mPage = mPage->next; 
                mIndex = 0; 
            } 
        }
        bool operator!=(Iterator it) { return mPage != it.mPage || mIndex != it.mIndex; }

        Page* mPage;
        uint32 mIndex;
        uint32 mPageSize;
    };

    Iterator begin()    { return Iterator(mPages, 0, mPageSize); }
    Iterator end()      
    { 
        Page* page = mPages;
        while (page && page->index == 0 && page->next)
        page = page->next;

        return Iterator(page, 0, mPageSize); 
    }

    Iterator begin() const    { return Iterator(mPages, 0, mPageSize); }
    Iterator end() const     
    { 
        Page* page = mPages;
        while (page && page->index == 0 && page->next)
        page = page->next;

        return Iterator(page, 0, mPageSize); 
    }

private:
    Allocator*  mAlloc = nullptr;
    uint32      mPageSize = 32;      // maximum number of items that a page can hold
    Page*       mPages = nullptr;
};

//----------------------------------------------------------------------------------------------------------------------
// @impl HandlePool
#ifndef __OBJC__
template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline HandlePool<_HandleType, _DataType, _Reserve>::HandlePool(void* data, size_t size) :
mItems((uint8*)data + GetMemoryRequirement(), size - GetMemoryRequirement())
{
    mHandles = _private::handleCreatePoolTableWithBuffer(_Reserve, data, GetMemoryRequirement());
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
void HandlePool<_HandleType, _DataType, _Reserve>::Reserve(uint32 capacity, void* buffer, size_t size)
{
    capacity = Max(capacity, _Reserve);
    ASSERT_MSG(mHandles == nullptr, "pool should be freed/uninitialized before reserve by pointer");
    mAlloc = nullptr;

    size_t tableSize = _private::handleGetMemoryRequirement(capacity);
    ASSERT(tableSize <= size);
    mHandles = _private::handleCreatePoolTableWithBuffer(capacity, buffer, tableSize);

    void* arrayBuffer = reinterpret_cast<uint8*>(buffer) + tableSize;
    ASSERT(reinterpret_cast<uintptr_t>(arrayBuffer)%CONFIG_MACHINE_ALIGNMENT == 0);
    mItems.Reserve(capacity, arrayBuffer, size - tableSize);
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
void HandlePool<_HandleType, _DataType, _Reserve>::SetAllocator(Allocator* alloc)
{
    ASSERT_MSG(mHandles == nullptr, "pool should be freed/uninitialized before setting allocator");
    mAlloc = alloc;
    mItems.SetAllocator(mAlloc);
}

template <typename _HandleType, typename _DataType, uint32 _Reserve>
void HandlePool<_HandleType, _DataType, _Reserve>::CopyTo(HandlePool<_HandleType, _DataType, _Reserve>* otherPool) const
{
    ASSERT_MSG(otherPool->mHandles == nullptr, "other pool should be uninitialized before cloning");
    otherPool->mHandles = _private::handleClone(mHandles, otherPool->mAlloc);
    mItems.CopyTo(&otherPool->mItems);
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline _HandleType HandlePool<_HandleType, _DataType, _Reserve>::Add(const _DataType& item, _DataType* prevItem)
{
    if (mHandles == nullptr) {
        ASSERT(mAlloc);
        mHandles = _private::handleCreatePoolTable(_Reserve, mAlloc);
    } 
    else if (mHandles->count == mHandles->capacity) {
        if (mAlloc) {
            Grow();
        }
        else {
            ASSERT_MSG(0, "HandlePool overflow, capacity=%u", mHandles->capacity);
        }
    }

    _HandleType handle(_private::handleNew(mHandles));
    uint32 index = handle.GetSparseIndex();
    if (index >= mItems.Count()) {
        mItems.Push(item);
        if (prevItem)
        *prevItem = _DataType {};
    }
    else {
        if (prevItem) 
        *prevItem = mItems[index];
        mItems[index] = item;
    }

    return handle;
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline void HandlePool<_HandleType, _DataType, _Reserve>::Remove(_HandleType handle)
{
    ASSERT(mHandles);
    _private::handleDel(mHandles, static_cast<uint32>(handle));
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline uint32 HandlePool<_HandleType, _DataType, _Reserve>::Count() const
{
    return mHandles ? mHandles->count : 0;
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline void HandlePool<_HandleType, _DataType, _Reserve>::Clear()
{
    if (mHandles)
    _private::handleResetPoolTable(mHandles);
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline bool HandlePool<_HandleType, _DataType, _Reserve>::IsValid(_HandleType handle)
{
    ASSERT(mHandles);
    return _private::handleIsValid(mHandles, static_cast<uint32>(handle));
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline _HandleType HandlePool<_HandleType, _DataType, _Reserve>::HandleAt(uint32 index)
{
    ASSERT(mHandles);
    return _HandleType(_private::handleAt(mHandles, index));
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline _DataType& HandlePool<_HandleType, _DataType, _Reserve>::Data(uint32 index)
{
    _HandleType handle = HandleAt(index);
    return mItems[handle.GetSparseIndex()];
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline _DataType& HandlePool<_HandleType, _DataType, _Reserve>::Data(_HandleType handle)
{
    ASSERT(mHandles);
    ASSERT_MSG(IsValid(handle), "Invalid handle (%u): Generation=%u, SparseIndex=%u", 
               uint32(handle), handle.GetGen(), handle.GetSparseIndex());
    return mItems[handle.GetSparseIndex()];
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline void HandlePool<_HandleType, _DataType, _Reserve>::Free()
{
    if (mAlloc) {
        if (mHandles) 
        _private::handleDestroyPoolTable(mHandles, mAlloc);
        mItems.Free();
        mHandles = nullptr;
    }
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
template<typename _Func> inline _HandleType HandlePool<_HandleType, _DataType, _Reserve>::FindIf(_Func findFunc)
{
    if (mHandles) {
        for (uint32 i = 0, c = mHandles->count; i < c; i++) {
            _HandleType h = _HandleType(_private::handleAt(mHandles, i));
            if (findFunc(mItems[h.GetSparseIndex()]))
            return h;
        }
    }
    
    return _HandleType();
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline bool HandlePool<_HandleType, _DataType, _Reserve>::IsFull() const
{
    return !mHandles && mHandles->count == mHandles->capacity;
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline uint32 HandlePool<_HandleType, _DataType, _Reserve>::Capacity() const
{
    return mHandles ? mHandles->capacity : _Reserve;
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline size_t HandlePool<_HandleType, _DataType, _Reserve>::GetMemoryRequirement(uint32 capacity)
{
    return _private::handleGetMemoryRequirement(capacity) + Array<_DataType>::GetMemoryRequirement(capacity);
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline bool HandlePool<_HandleType, _DataType, _Reserve>::Grow()
{
    ASSERT(mAlloc);
    ASSERT(mHandles);
       
    mItems.Reserve(mHandles->capacity << 1);
    return _private::handleGrowPoolTable(&mHandles, mAlloc);
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline bool HandlePool<_HandleType, _DataType, _Reserve>::Grow(void* data, size_t size)
{
    ASSERT(!mAlloc);
    ASSERT(mHandles);

    uint32 newCapacity = mHandles->capacity << 1;
    size_t handleTableSize = GetMemoryRequirement(newCapacity);
    ASSERT(handleTableSize < size);

    mItems.Reserve(mHandles->capacity << 1, (uint8*)data + handleTableSize, size - handleTableSize);
    return _private::handleGrowPoolTableWithBuffer(&mHandles, data, handleTableSize);
}
#endif // !__OBJC__

//----------------------------------------------------------------------------------------------------------------------
// @impl FixeSizePool
template <typename _T, uint32 _Align>
inline FixedSizePool<_T, _Align>::FixedSizePool(void* buffer, size_t size)
{
    ASSERT(buffer);
    ASSERT(size > sizeof(Page));

    mPageSize = (size - sizeof(Page))/sizeof(_T);
    ASSERT_MSG(mPageSize, "Buffer size is too small");
    mPages = CreatePage(buffer, size);
}

template <typename _T, uint32 _Align>
inline void FixedSizePool<_T, _Align>::SetAllocator(Allocator* alloc)
{
    ASSERT_MSG(!mPages, "SetAllocator must be called before using/initializing the Blob");
    mAlloc = alloc;
}

template <typename _T, uint32 _Align>
inline void FixedSizePool<_T, _Align>::Reserve(uint32 pageSize)
{
    ASSERT(mAlloc);
    ASSERT(pageSize);

    mPageSize = pageSize;
    mPages = CreatePage(nullptr, 0);
}

template <typename _T, uint32 _Align>
inline void FixedSizePool<_T, _Align>::Reserve(void* buffer, size_t size, uint32 pageSize)
{
    ASSERT(buffer);
    ASSERT(size > sizeof(Page));
    ASSERT(mPages == nullptr);
    ASSERT(pageSize);
    
    mPageSize = pageSize;
    mAlloc = nullptr;
    mPages = CreatePage(buffer, size);
}

template <typename _T, uint32 _Align>
inline void FixedSizePool<_T, _Align>::Free()
{
    if (mAlloc) {
        Page* page = mPages;
        while (page) {
            Page* next = page->next; 
            if (mAlloc)
                MemSingleShotMalloc<Page>::Free(page, mAlloc);
            page = next;
        }
    }

    mPageSize = 0;
    mPages = nullptr;
}

template <typename _T, uint32 _Align>
inline size_t FixedSizePool<_T, _Align>::GetMemoryRequirement(uint32 pageSize)
{
    MemSingleShotMalloc<Page> pageBuffer;
    pageBuffer.template AddMemberField<_T*>(offsetof(Page, ptrs), pageSize);
    pageBuffer.template AddMemberField<_T>(offsetof(Page, data), pageSize, false, _Align);
    return pageBuffer.GetMemoryRequirement();
}

template <typename _T, uint32 _Align>
inline _T* FixedSizePool<_T, _Align>::New()
{
    Page* page = mPages;
    while (page && page->index == 0 && page->next)
        page = page->next;
    
    // Grow if necassory 
    if (!page || page->index == 0) {
        if (!mAlloc) {
            ASSERT_MSG(0, "Cannot allocate anymore new objects. Pool is full");
            return nullptr;
        }

        page = CreatePage(nullptr, 0);
        if (mPages) {
            Page* lastPage = mPages;
            while (lastPage->next)
                lastPage = lastPage->next;
            lastPage->next = page;
        }
        else {
            mPages = page;
        }
    }

    ASSERT(page->index);
    return page->ptrs[--page->index];
}

template <typename _T, uint32 _Align>
inline void FixedSizePool<_T, _Align>::Delete(_T* item)
{
    uint64 uptr = PtrToInt<uint64>(item);
    Page* page = mPages;
    uint32 pageSize = mPageSize;

    while (page) {
        if (uptr >= PtrToInt<uint64>(page->data) && uptr < PtrToInt<uint64>(page->data + pageSize)) {
            ASSERT_MSG(page->index != pageSize, "Cannot delete more objects from this page, possible double delete");

            page->ptrs[page->index++] = item;
            return;
        }

        page = page->next;
    }

    ASSERT_MSG(0, "Pointer doesn't belong to this pool");
}

template <typename _T, uint32 _Align>
inline bool FixedSizePool<_T, _Align>::IsFull() const
{
    Page* page = mPages;
    while (page && page->index == 0 && page->next)
        page = page->next;
    
    return !page || page->index == 0;
}

template <typename _T, uint32 _Align>
inline typename FixedSizePool<_T, _Align>::Page* FixedSizePool<_T, _Align>::CreatePage(void* buffer, size_t size)
{
    ASSERT(mPageSize);

    MemSingleShotMalloc<Page> mallocator;
    mallocator.template AddMemberField<_T*>(offsetof(Page, ptrs), mPageSize);
    mallocator.template AddMemberField<_T>(offsetof(Page, data), mPageSize, false, _Align); // Only align data buffer

    Page* page = (buffer && size) ? mallocator.Calloc(buffer, size) : page = mallocator.Calloc(mAlloc);
    page->index = mPageSize;
    for (uint32 i = 0, c = mPageSize; i < c; i++)
        page->ptrs[c - i - 1] = page->data + i;
    return page;
}



