#pragma once
//
// Contains memory containers and useful buffer manipulation classes
// All objects can be initialized either with allocator or with user provided buffer/size pair, in the case of latter, use GetMemoryRequirement to get needed memory size before creating
//
// BuffersAllocPOD: POD struct continous allocator. If you have POD structs that contain some buffers and arrays
//              	You can create all of the buffers in one malloc call with the help of this allocator
//					Relative pointers are also supported for member variables, as shown in the example below.
//					Example:
//						struct SomeObject
//						{
//						    uint32 a;
//						    uint32 b;
//						};
//						
//						struct SomeStruct
//						{
//						    int count;
//						    RelativePtr<SomeObject> objects;
//						};
//						
//						BuffersAllocPOD<SomeStruct> alloc;
//						SomeStruct* s = alloc.AddMemberField<SomeObject>(offsetof(SomeStruct, objects), 100, true).Calloc();
//						s->count = 100;
//						
//						for (int i = 0; i < s->count; i++) {
//						    s->objects[i].a = (uint32)i;
//						    s->objects[i].b = (uint32)(i*2);
//						}
//
//  RingBuffer: Regular ring-buffer
//      Writing to ring-buffer: use ExpectWrite method to determine how much memory is available in the ring-buffer before writing
//      Example:
//          if (buffer.ExpectWrite() >= sizeof(float))
//              buffer.Write<float>(value);
//      Reading: There are two methods for Reading. `Read` progreses the ring-buffer offset. ReadInPlace does not progress the offset
//
//  Blob: Readable and Writable blob of memory
//        Blob can contain static buffer (by explicitly prodiving a buffer pointer and size), or Dynamic growable buffer (by providing allocator)
//        In the case of dynamic growable memory, you should provide Growing policy with `SetGrowPolicy` method.
//        
//  PoolBuffer: Fast pool memory allocator with Fixed sized elements
//              Pools can grow by adding pages. For that, you need to provide allocator to the pool-buffer instead of pre-allocated pointer/size pair.
//              
#include "Memory.h"

//------------------------------------------------------------------------
template <typename _T, uint32 _MaxFields = 8>
struct BuffersAllocPOD
{
    BuffersAllocPOD() : BuffersAllocPOD(CONFIG_MACHINE_ALIGNMENT) {}
    explicit BuffersAllocPOD(uint32 align);

    template <typename _FieldType> BuffersAllocPOD& AddMemberField(uint32 offsetInStruct, size_t arrayCount, 
                                                                bool relativePtr = false,
                                                                uint32 align = CONFIG_MACHINE_ALIGNMENT);
    template <typename _PodAllocType> BuffersAllocPOD& AddMemberChildPODField(const _PodAllocType& podAlloc, 
                                                                            uint32 offsetInStruct, size_t arrayCount, 
                                                                            bool relativePtr = false,
                                                                            uint32 align = CONFIG_MACHINE_ALIGNMENT);
    template <typename _FieldType> BuffersAllocPOD& AddExternalPointerField(_FieldType** pPtr, size_t arrayCount, 
                                                                            uint32 align = CONFIG_MACHINE_ALIGNMENT);

    _T* Calloc(Allocator* alloc = memDefaultAlloc());
    _T* Calloc(void* buff, size_t size);

    size_t GetMemoryRequirement() const;
    size_t GetSize() const;

private:
    struct Field
    {
        void** pPtr;
        size_t offset;
        uint32 offsetInStruct;
        bool   relativePtr;
    };

    Field  _fields[_MaxFields];
    size_t _size;
    uint32 _numFields;
    uint32 _structAlign;
};

//------------------------------------------------------------------------
struct RingBuffer
{
    RingBuffer() : RingBuffer(memDefaultAlloc()) {}
    explicit RingBuffer(Allocator* alloc) : _alloc(alloc) {}
    explicit RingBuffer(void* buffer, size_t size);
    
    void SetAllocator(Allocator* alloc);
    void Reserve(size_t capacity);
    void Reserve(void* buffer, size_t size);
    void Free();
    static size_t GetMemoryRequirement(size_t capacity);
    
    size_t ExpectWrite() const;

    void Write(const void* src, size_t size);
    size_t Read(void* dst, size_t size);
    size_t ReadInPlace(void* dst, size_t size, size_t* pOffset = nullptr);

    template <typename _T> void Write(const _T& src);
    template <typename _T> size_t Read(_T* dst);

    size_t Capacity() const;

private:
    Allocator*  _alloc = nullptr;
    uint8*      _buffer = nullptr;
    size_t      _capacity = 0;
    size_t      _size = 0;
    size_t      _start = 0;
    size_t      _end = 0;
};

//------------------------------------------------------------------------
struct Blob
{
    enum class GrowPolicy : uint32
    {
        None = 0,
        Linear,
        Multiply
    };

    inline Blob() : Blob(memDefaultAlloc()) {}
    inline explicit Blob(Allocator* alloc) : _alloc(alloc) {}
    inline explicit Blob(void* buffer, size_t size);
    inline Blob& operator=(const Blob&) = default;
    inline Blob(const Blob&) = default;

    inline void Attach(void* data, size_t size, Allocator* alloc);
    inline void Detach(void** outData, size_t* outSize);

    inline void SetAllocator(Allocator* alloc);
    inline void SetGrowPolicy(GrowPolicy policy, uint32 amount = 0);
    inline void SetAlignment(uint8 align);
    inline void SetSize(size_t size);
    inline void Reserve(size_t capacity);
    inline void Reserve(void* buffer, size_t size);
    inline void Free();
    inline void ResetOffset();
    inline void SetOffset(size_t offset);
    inline void CopyTo(Blob* otherBlob) const;

    inline size_t Write(const void* src, size_t size);
    inline size_t Read(void* dst, size_t size) const;
    template <typename _T> size_t Write(const _T& src);
    template <typename _T> size_t Read(_T* dst) const;
    inline size_t WriteStringBinary(const char* str, uint32 len);
    inline size_t ReadStringBinary(char* outStr, uint32 outStrSize) const;

    inline size_t Size() const;
    inline size_t Capacity() const;
    inline size_t ReadOffset() const;
    inline const void* Data() const;
    inline bool IsValid() const;

private:
    Allocator* _alloc = nullptr;
    void*      _buffer = nullptr;
    size_t     _size = 0;
    size_t     _offset = 0;
    size_t     _capacity = 0;
    uint32     _align = CONFIG_MACHINE_ALIGNMENT;
    GrowPolicy _growPolicy = GrowPolicy::None;
    uint32     _growAmount = 4096u;
};

//------------------------------------------------------------------------
template <typename _T, uint32 _Align = CONFIG_MACHINE_ALIGNMENT>
struct PoolBuffer
{
    PoolBuffer() : PoolBuffer(memDefaultAlloc()) {}
    explicit PoolBuffer(Allocator* alloc) : _alloc(alloc) {}
    explicit PoolBuffer(void* buffer, size_t size);
    
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
        Iterator(Page* page, uint32 index, uint32 pageSize) : _page(page), _index(index), _pageSize(pageSize) {}
        _T& operator*() { return _page->data[_index]; }
        void operator++() 
        { 
            ASSERT(_page); 
            if (_index < _pageSize) 
                _index++; 
            else { 
                _page = _page->next; 
                _index = 0; 
            } 
        }
        bool operator!=(Iterator it) { return _page != it._page || _index != it._index; }

        Page* _page;
        uint32 _index;
        uint32 _pageSize;
    };

    Iterator begin()    { return Iterator(_pages, 0, _pageSize); }
    Iterator end()      
    { 
        Page* page = _pages;
        while (page && page->index == 0 && page->next)
            page = page->next;

        return Iterator(page, 0, _pageSize); 
    }

    Iterator begin() const    { return Iterator(_pages, 0, _pageSize); }
    Iterator end() const     
    { 
        Page* page = _pages;
        while (page && page->index == 0 && page->next)
            page = page->next;

        return Iterator(page, 0, _pageSize); 
    }

private:
    Allocator*  _alloc = nullptr;
    uint32      _pageSize = 32;      // maximum number of items that a page can hold
    Page*       _pages = nullptr;
};

//------------------------------------------------------------------------
// BuffersAllocPOD
template <typename _T, uint32 _MaxFields>
inline BuffersAllocPOD<_T, _MaxFields>::BuffersAllocPOD(uint32 align)
{
    align = Max(CONFIG_MACHINE_ALIGNMENT, align);
    _size = AlignValue<size_t>(sizeof(_T), align);
    _structAlign = align;

    _fields[0].pPtr = nullptr;
    _fields[0].offset = 0;
    _fields[0].offsetInStruct = UINT32_MAX;
    _numFields = 1;
}

template <typename _T, uint32 _MaxFields>
template <typename _FieldType> inline BuffersAllocPOD<_T, _MaxFields>& 
    BuffersAllocPOD<_T, _MaxFields>::AddMemberField(uint32 offsetInStruct, size_t arrayCount, bool relativePtr, uint32 align)
{
    uint32 index = _numFields;
    ASSERT_MSG(index < _MaxFields, "Cannot add more fields, increase the _MaxFields");
    
    align = Max(CONFIG_MACHINE_ALIGNMENT, align);
    size_t size = sizeof(_FieldType) * arrayCount;
    size = AlignValue<size_t>(size, align);

    size_t offset = _size;
    if (offset % align != 0) {
        offset = AlignValue<size_t>(offset, align);
    }

    Field& buff = _fields[index];
    buff.pPtr = nullptr;
    buff.offset = offset;
    buff.offsetInStruct = offsetInStruct;
    buff.relativePtr = relativePtr;

    _size += size;
    ++_numFields;

    return *this;
}

template <typename _T, uint32 _MaxFields>
template <typename _PodAllocType> inline BuffersAllocPOD<_T, _MaxFields>& 
BuffersAllocPOD<_T, _MaxFields>::AddMemberChildPODField(const _PodAllocType& podAlloc, uint32 offsetInStruct, 
                                                        size_t arrayCount, bool relativePtr, uint32 align)
{
    uint32 index = _numFields;
    ASSERT_MSG(index < _MaxFields, "Cannot add more fields, increase the _MaxFields");
    
    align = Max(CONFIG_MACHINE_ALIGNMENT, align);
    size_t size = podAlloc.GetMemoryRequirement() * arrayCount;
    size = AlignValue<size_t>(size, align);

    size_t offset = _size;
    if (offset % align != 0) {
        offset = AlignValue<size_t>(offset, align);
    }

    Field& buff = _fields[index];
    buff.pPtr = nullptr;
    buff.offset = offset;
    buff.offsetInStruct = offsetInStruct;
    buff.relativePtr = relativePtr;

    _size += size;
    ++_numFields;

    return *this;
}


template <typename _T, uint32 _MaxFields>
template <typename _FieldType> inline BuffersAllocPOD<_T, _MaxFields>& 
    BuffersAllocPOD<_T, _MaxFields>::AddExternalPointerField(_FieldType** pPtr, size_t arrayCount, uint32 align)
{
    ASSERT(pPtr);

    uint32 index = _numFields;
    ASSERT_MSG(index < _MaxFields, "Cannot add more fields, increase the _MaxFields");
    
    align = Max(CONFIG_MACHINE_ALIGNMENT, align);
    size_t size = sizeof(_FieldType) * arrayCount;
    size = AlignValue<size_t>(size, align);
    
    size_t offset = _size;
    if (offset % align != 0) {
        offset = AlignValue<size_t>(offset, align);
    }
    
    Field& buff = _fields[index];
    buff.pPtr = (void**)pPtr;
    buff.offset = offset;
    buff.offsetInStruct = UINT32_MAX;
    buff.relativePtr = false;
    
    _size += size;
    ++_numFields;

    return *this;
}

template <typename _T, uint32 _MaxFields>
inline _T* BuffersAllocPOD<_T, _MaxFields>::Calloc(Allocator* alloc)
{
    void* mem = memAllocAligned(_size, _structAlign, alloc);
    return Calloc(mem, _size);
}

template <typename _T, uint32 _MaxFields>
inline size_t  BuffersAllocPOD<_T, _MaxFields>::GetMemoryRequirement() const
{
    return AlignValue<size_t>(_size, _structAlign);
}

template <typename _T, uint32 _MaxFields>
inline size_t BuffersAllocPOD<_T, _MaxFields>::GetSize() const
{
    return _size;
}

template <typename _T, uint32 _MaxFields>
inline _T*  BuffersAllocPOD<_T, _MaxFields>::Calloc(void* buff, [[maybe_unused]] size_t size)
{
    ASSERT(buff);
    ASSERT(size == 0 || size >= GetMemoryRequirement());

    memset(buff, 0x0, _size);
    
    uint8* tmp = (uint8*)buff;
    
    // Assign buffer pointers
    for (int i = 1, c = _numFields; i < c; i++) {
        if (_fields[i].offsetInStruct != UINT32_MAX) {
            ASSERT(_fields[i].pPtr == NULL);
            if (!_fields[i].relativePtr) 
                *((void**)(tmp + _fields[i].offsetInStruct)) = tmp + _fields[i].offset;
            else
                *((uint32*)(tmp + _fields[i].offsetInStruct)) = (uint32)_fields[i].offset - _fields[i].offsetInStruct;
        } else {
            ASSERT(_fields[i].offsetInStruct == -1);
            *_fields[i].pPtr = tmp + _fields[i].offset;
        }
    }

    return (_T*)buff;
}

//------------------------------------------------------------------------
// RingBuffer
inline RingBuffer::RingBuffer(void* buffer, size_t size)
{
    ASSERT(buffer);
    ASSERT(size);

    _capacity = size;
    _buffer = reinterpret_cast<uint8*>(buffer);
}

inline void RingBuffer::SetAllocator(Allocator* alloc)
{
    ASSERT_MSG(_buffer == nullptr, "buffer should be freed/uninitialized before setting allocator");
    _alloc = alloc;
}

inline void RingBuffer::Reserve(size_t capacity)
{
    ASSERT(_alloc);
    _capacity = Max(capacity, _capacity);
    _buffer = reinterpret_cast<uint8*>(memRealloc(_buffer, _capacity, _alloc));
    ASSERT(_buffer);
}

inline void RingBuffer::Reserve(void* buffer, size_t size)
{
    ASSERT_MSG(_buffer == nullptr, "RingBuffer must not get used before setting buffer pointer");
    ASSERT(buffer);
    
    _capacity = size;
    _buffer = reinterpret_cast<uint8*>(buffer);
    _alloc = nullptr;
}

inline void RingBuffer::Free()
{
    if (_alloc) {
        memFree(_buffer, _alloc);
        _capacity = _size = _start = _end = 0;
        _buffer = nullptr;
    }
}

inline size_t RingBuffer::GetMemoryRequirement(size_t capacity)
{
    return capacity;
}

inline size_t RingBuffer::ExpectWrite() const
{
    return _capacity - _size;
}

inline void RingBuffer::Write(const void* src, size_t size)
{
    ASSERT(size <= ExpectWrite());
    
    uint8* buff = _buffer;
    const uint8* udata = reinterpret_cast<const uint8*>(src);
    size_t remain = _capacity - _end;
    if (remain >= size) {
        memcpy(&buff[_end], udata, size);
    } else {
        memcpy(&buff[_end], udata, remain);
        memcpy(buff, &udata[remain], size - remain);
    }
    
    _end = (_end + size) % _capacity;
    _size += size;
}

inline size_t RingBuffer::Read(void* dst, size_t size)
{
    ASSERT(size > 0);
    
    size = Min(size, _size);
    if (size == 0)
        return 0;
    
    if (dst) {
        uint8* buff = _buffer;
        uint8* udata = reinterpret_cast<uint8*>(dst);
        size_t remain = _capacity - _start;
        if (remain >= size) {
            memcpy(udata, &buff[_start], size);
        } else {
            memcpy(udata, &buff[_start], remain);
            memcpy(&udata[remain], buff, size - remain);
        }
    }
    
    _start = (_start + size) % _capacity;
    _size -= size;
    return size;
}

inline size_t RingBuffer::ReadInPlace(void* dst, size_t size, size_t* pOffset)
{
    ASSERT(size > 0);
    
    size = Min(size, _size);
    if (size == 0)
        return 0;
    
    ASSERT(dst);
    uint8* buff = _buffer;
    uint8* udata = reinterpret_cast<uint8*>(dst);
    size_t _offset = pOffset ? *pOffset : _start;
    size_t remain = _capacity - _offset;
    if (remain >= size) {
        memcpy(udata, &buff[_offset], size);
    } else {
        memcpy(udata, &buff[_offset], remain);
        memcpy(&udata[remain], buff, (size_t)size - (size_t)remain);
    }
    
    if (pOffset)
        *pOffset = (*pOffset + size) % _capacity;

    return size;
}

template <typename _T> inline void RingBuffer::Write(const _T& src)
{
    Write(&src, sizeof(_T));
}

template <typename _T> inline size_t RingBuffer::Read(_T* dst)
{
    return Read(dst, sizeof(_T));
}

inline size_t RingBuffer::Capacity() const
{
    return _capacity;
}

//------------------------------------------------------------------------
// Blob
inline Blob::Blob(void* buffer, size_t size)
{
    ASSERT(buffer && size);
    _buffer = buffer;
    _capacity = size;
}

inline void Blob::Attach(void* data, size_t size, Allocator* alloc)
{
    ASSERT(data);
    ASSERT_MSG(!_buffer, "buffer should be freed before attach");
    _alloc = alloc;
    _growPolicy = GrowPolicy::None;
    _buffer = data;
    _offset = 0;
    _size = size;
    _capacity = size;
}

inline void Blob::Detach(void** outData, size_t* outSize)
{
    ASSERT(outData);
    ASSERT(outSize);

    *outData = _buffer;
    *outSize = _size;

    _buffer = nullptr;
    _size = 0;
    _offset = 0;
    _capacity = 0;
}

inline void Blob::SetAllocator(Allocator* alloc)
{
    ASSERT_MSG(!_buffer, "SetAllocator must be called before using/initializing the Blob");
    _alloc = alloc;
}

inline void Blob::SetSize(size_t size)
{
    ASSERT_MSG(size <= _capacity, "Size cannot be larger than capacity");
    _size = size;
}

inline void Blob::Reserve(size_t capacity)
{
    ASSERT_MSG(_alloc, "Allocator must be set for dynamic Reserve");
    ASSERT(capacity > _size);

    _buffer = memReallocAligned(_buffer, capacity, _align, _alloc);
    _capacity = capacity;
}

inline void Blob::Reserve(void* buffer, size_t size)
{
    ASSERT(size > _size);
    ASSERT(PtrToInt<uint64>(buffer) % _align == 0);
    ASSERT(_buffer == nullptr);

    _buffer = buffer;
    _capacity = size;
    _alloc = nullptr;
}

inline void Blob::Free()
{
    if (_alloc)
        memFreeAligned(_buffer, _align, _alloc);
    _buffer = nullptr;
    _size = 0;
    _capacity = 0;
    _alloc = nullptr;
}

inline void Blob::ResetOffset() 
{
    _offset = 0;
}

inline void Blob::SetOffset(size_t offset) 
{
    ASSERT(_offset < _size);
    _offset = offset;
}

inline size_t Blob::Write(const void* src, size_t size)
{
    ASSERT(src);
    ASSERT(size);

    size_t writeBytes = Min(_capacity - _size, size);
    if (writeBytes < size && _growPolicy != GrowPolicy::None) {
        ASSERT_MSG(_alloc, "Growable blobs should have allocator");
        ASSERT(_growAmount);

        if (_growPolicy == GrowPolicy::Linear) {
            _capacity += _growAmount;
            _buffer = memReallocAligned(_buffer, _capacity, _align, _alloc);
        }
        else if (_growPolicy == GrowPolicy::Multiply) {
            if (!_capacity)
                _capacity = _growAmount;
            else
                _capacity <<= 1;
            _buffer = memReallocAligned(_buffer, _capacity, _align, _alloc);
        }

        return Write(src, size);
    }

    if (writeBytes) {
        uint8* buff = reinterpret_cast<uint8*>(_buffer);
        memcpy(buff + _size, src, writeBytes);
        _size += writeBytes;
    }

    #if CONFIG_VALIDATE_IO_READ_WRITES
       ASSERT(writeBytes == size);
    #endif
    return writeBytes;
}

inline size_t Blob::Read(void* dst, size_t size) const
{
    ASSERT(dst);
    ASSERT(size);

    size_t readBytes = Min(_size - _offset, size);
    if (readBytes) {
        uint8* buff = reinterpret_cast<uint8*>(_buffer);
        memcpy(dst, buff + _offset, readBytes);
        const_cast<Blob*>(this)->_offset += readBytes;
    }

    #if CONFIG_VALIDATE_IO_READ_WRITES
       ASSERT(size == readBytes);
    #endif

    return readBytes;
}

template <typename _T> 
inline size_t Blob::Write(const _T& src)
{
    return static_cast<uint32>(Write(&src, sizeof(_T)));
}

template <typename _T> 
inline size_t Blob::Read(_T* dst) const
{
    return static_cast<uint32>(Read(dst, sizeof(_T)));
}

inline size_t Blob::WriteStringBinary(const char* str, uint32 len)
{
    size_t writtenBytes = Write<uint32>(len);
    if (len) 
        writtenBytes += Write(str, len);
    return writtenBytes;
}

inline size_t Blob::ReadStringBinary(char* outStr, [[maybe_unused]] uint32 outStrSize) const
{
    uint32 len = 0;
    size_t readStrBytes = 0;
    size_t readBytes = Read<uint32>(&len);
    ASSERT(readBytes == sizeof(len));
    ASSERT(len < outStrSize);
    if (len) {
        readStrBytes = Read(outStr, len);
        ASSERT(readStrBytes == len);
    }
    outStr[len] = '\0';
    return readStrBytes + readBytes;
}

inline size_t Blob::Size() const
{
    return _size;
}

inline size_t Blob::ReadOffset() const
{
    return _offset;
}

inline size_t Blob::Capacity() const
{
    return _capacity;
}

inline const void* Blob::Data() const
{
    return _buffer;
}

inline bool Blob::IsValid() const
{
    return _buffer && _size;
}

inline void Blob::CopyTo(Blob* otherBlob) const
{
    ASSERT(_size);
    otherBlob->Reserve(_size);
    otherBlob->SetSize(_size);
    memcpy(otherBlob->_buffer, _buffer, _size); 
}

inline void Blob::SetAlignment(uint8 align)
{
    if (align < CONFIG_MACHINE_ALIGNMENT)
        align = CONFIG_MACHINE_ALIGNMENT;
    _align = align;
}

inline void Blob::SetGrowPolicy(GrowPolicy policy, uint32 amount)
{
    _growPolicy = policy;
    _growAmount = amount == 0 ? 4096u : AlignValue(amount, CACHE_LINE_SIZE);
}

//------------------------------------------------------------------------
// PoolBuffer
template <typename _T, uint32 _Align>
inline PoolBuffer<_T, _Align>::PoolBuffer(void* buffer, size_t size)
{
    ASSERT(buffer);
    ASSERT(size > sizeof(Page));

    _pageSize = (size - sizeof(Page))/sizeof(_T);
    ASSERT_MSG(_pageSize, "Buffer size is too small");
    _pages = CreatePage(buffer, size);
}

template <typename _T, uint32 _Align>
inline void PoolBuffer<_T, _Align>::SetAllocator(Allocator* alloc)
{
    ASSERT_MSG(!_pages, "SetAllocator must be called before using/initializing the Blob");
    _alloc = alloc;
}

template <typename _T, uint32 _Align>
inline void PoolBuffer<_T, _Align>::Reserve(uint32 pageSize)
{
    ASSERT(_alloc);
    ASSERT(pageSize);

    _pageSize = pageSize;
    _pages = CreatePage(nullptr, 0);
}

template <typename _T, uint32 _Align>
inline void PoolBuffer<_T, _Align>::Reserve(void* buffer, size_t size, uint32 pageSize)
{
    ASSERT(buffer);
    ASSERT(size > sizeof(Page));
    ASSERT(_pages == nullptr);
    ASSERT(pageSize);
    
    _pageSize = pageSize;
    _alloc = nullptr;
    _pages = CreatePage(buffer, size);
}

template <typename _T, uint32 _Align>
inline void PoolBuffer<_T, _Align>::Free()
{
    if (_alloc) {
        Page* page = _pages;
        while (page) {
            Page* next = page->next;
            memFree(page, _alloc);
            page = next;
        }
    }

    _pageSize = 0;
    _pages = nullptr;
}

template <typename _T, uint32 _Align>
inline size_t PoolBuffer<_T, _Align>::GetMemoryRequirement(uint32 pageSize)
{
    BuffersAllocPOD<Page> pageBuffer;
    pageBuffer.template AddMemberField<_T*>(offsetof(Page, ptrs), pageSize);
    pageBuffer.template AddMemberField<_T>(offsetof(Page, data), pageSize, false, _Align);
    return pageBuffer.GetMemoryRequirement();
}

template <typename _T, uint32 _Align>
inline _T* PoolBuffer<_T, _Align>::New()
{
    Page* page = _pages;
    while (page && page->index == 0 && page->next)
        page = page->next;
    
    // Grow if necassory 
    if (!page || page->index == 0) {
        if (!_alloc) {
            ASSERT_MSG(0, "Cannot allocate anymore new objects. Pool is full");
            return nullptr;
        }

        page = CreatePage(nullptr, 0);
        if (_pages) {
            Page* lastPage = _pages;
            while (lastPage->next)
                lastPage = lastPage->next;
            lastPage->next = page;
        }
        else {
            _pages = page;
        }
    }

    ASSERT(page->index);
    return page->ptrs[--page->index];
}

template <typename _T, uint32 _Align>
inline void PoolBuffer<_T, _Align>::Delete(_T* item)
{
    uint64 uptr = PtrToInt<uint64>(item);
    Page* page = _pages;
    uint32 pageSize = _pageSize;

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
inline bool PoolBuffer<_T, _Align>::IsFull() const
{
    Page* page = _pages;
    while (page && page->index == 0 && page->next)
        page = page->next;
    
    return !page || page->index == 0;
}

template <typename _T, uint32 _Align>
inline typename PoolBuffer<_T, _Align>::Page* PoolBuffer<_T, _Align>::CreatePage(void* buffer, size_t size)
{
    ASSERT(_pageSize);

    BuffersAllocPOD<Page> pageBuffer;
    pageBuffer.template AddMemberField<_T*>(offsetof(Page, ptrs), _pageSize);
    pageBuffer.template AddMemberField<_T>(offsetof(Page, data), _pageSize, false, _Align); // Only align data buffer

    Page* page = (buffer && size) ? pageBuffer.Calloc(buffer, size) : page = pageBuffer.Calloc(_alloc);
    page->index = _pageSize;
    for (uint32 i = 0, c = _pageSize; i < c; i++)
        page->ptrs[c - i - 1] = page->data + i;
    return page;
}
