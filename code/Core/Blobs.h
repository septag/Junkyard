#pragma once

//  Blob: Readable and Writable blob of memory
//        - A blob can contain static buffer (by explicitly prodiving a buffer pointer and size), or Dynamic growable buffer (by providing allocator)
//        - In the case of dynamic growable memory, you should provide Growing policy with `SetGrowPolicy` method before writing to the blob
//        - Blobs don't allocate anything in ctor or deallocate anything in dtor. 
//        - So you should either pre-allocate with `Reserve` method, or set allocator and grow policy and call `Write` method to implictly allocate internal memory.
//        -  deallocate with `Free` or implicitly with `Detach`ing the pointer from the blob
//
//  RingBlob: Regular, naively implemented ring-buffer
//      Writing to ring-buffer: use `ExpectWrite` method to determine how much memory is available in the ring-buffer before writing.
//      Example:
//          if (buffer.ExpectWrite() >= sizeof(float))
//              buffer.Write<float>(value);
//      Reading: There are two methods for Reading. `Read` progreses the ring-buffer offset. `Peek` just reads a buffer and doesn't move the offset
//      RingBlob is not thread-safe, wrap all calls in locking primitives if you want to use it with multiple threads
//
//        
#include "Base.h"
#include "StringUtil.h"     // strLen

//----------------------------------------------------------------------------------------------------------------------
struct Blob
{
    enum class GrowPolicy : uint32
    {
        None = 0,
        Linear,
        Multiply
    };

    inline Blob() : Blob(memDefaultAlloc()) {}
    inline explicit Blob(Allocator* alloc) : mAlloc(alloc) {}
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
    inline void ResetRead();
    inline void ResetWrite();
    inline void Reset();
    inline void SetOffset(size_t offset);
    inline void CopyTo(Blob* otherBlob) const;

    inline size_t Write(const void* src, size_t size);
    inline size_t Read(void* dst, size_t size) const;
    template <typename _T> size_t Write(const _T& src);
    template <typename _T> size_t Read(_T* dst) const;
    size_t WriteStringBinary(const char* str, uint32 len = 0);
    size_t ReadStringBinary(char* outStr, uint32 outStrSize) const;
    size_t WriteStringBinary16(const char* str, uint32 len = 0);
    size_t ReadStringBinary16(char* outStr, uint32 outStrSize) const;
    
    inline size_t Size() const;
    inline size_t Capacity() const;
    inline size_t ReadOffset() const;
    inline const void* Data() const;
    inline bool IsValid() const;

private:
    Allocator* mAlloc = nullptr;
    void*      mBuffer = nullptr;
    size_t     mSize = 0;
    size_t     mOffset = 0;
    size_t     mCapacity = 0;
    uint32     mAlign = CONFIG_MACHINE_ALIGNMENT;
    GrowPolicy mGrowPolicy = GrowPolicy::None;
    uint32     mGrowCount = 4096u;
};

//----------------------------------------------------------------------------------------------------------------------
// RingBlob
struct RingBlob
{
    RingBlob() : RingBlob(memDefaultAlloc()) {}
    explicit RingBlob(Allocator* alloc) : mAlloc(alloc) {}
    explicit RingBlob(void* buffer, size_t size);
    
    void SetAllocator(Allocator* alloc);
    void Reserve(size_t capacity);
    void Reserve(void* buffer, size_t size);
    void Free();
    static size_t GetMemoryRequirement(size_t capacity);
    
    size_t ExpectWrite() const;

    void Write(const void* src, size_t size);
    size_t Read(void* dst, size_t size);
    size_t Peek(void* dst, size_t size, size_t* pOffset = nullptr);

    template <typename _T> void Write(const _T& src);
    template <typename _T> size_t Read(_T* dst);

    size_t Capacity() const;

private:
    Allocator* mAlloc = nullptr;
    uint8* mBuffer = nullptr;
    size_t mCapacity = 0;
    size_t mSize = 0;
    size_t mStart = 0;
    size_t mEnd = 0;
};

//----------------------------------------------------------------------------------------------------------------------
// @impl RingBlob
inline RingBlob::RingBlob(void* buffer, size_t size)
{
    ASSERT(buffer);
    ASSERT(size);

    mCapacity = size;
    mBuffer = reinterpret_cast<uint8*>(buffer);
}

inline void RingBlob::SetAllocator(Allocator* alloc)
{
    ASSERT_MSG(mBuffer == nullptr, "buffer should be freed/uninitialized before setting allocator");
    mAlloc = alloc;
}

inline void RingBlob::Reserve(size_t capacity)
{
    ASSERT(mAlloc);
    mCapacity = Max(capacity, mCapacity);
    mBuffer = reinterpret_cast<uint8*>(memRealloc(mBuffer, mCapacity, mAlloc));
    ASSERT(mBuffer);
}

inline void RingBlob::Reserve(void* buffer, size_t size)
{
    ASSERT_MSG(mBuffer == nullptr, "RingBlob must not get used before setting buffer pointer");
    ASSERT(buffer);
    
    mCapacity = size;
    mBuffer = reinterpret_cast<uint8*>(buffer);
    mAlloc = nullptr;
}

inline void RingBlob::Free()
{
    if (mAlloc) {
        memFree(mBuffer, mAlloc);
        mCapacity = mSize = mStart = mEnd = 0;
        mBuffer = nullptr;
    }
}

inline size_t RingBlob::GetMemoryRequirement(size_t capacity)
{
    return capacity;
}

inline size_t RingBlob::ExpectWrite() const
{
    return mCapacity - mSize;
}

inline void RingBlob::Write(const void* src, size_t size)
{
    ASSERT(size <= ExpectWrite());
    
    uint8* buff = mBuffer;
    const uint8* udata = reinterpret_cast<const uint8*>(src);
    size_t remain = mCapacity - mEnd;
    if (remain >= size) {
        memcpy(&buff[mEnd], udata, size);
    } else {
        memcpy(&buff[mEnd], udata, remain);
        memcpy(buff, &udata[remain], size - remain);
    }
    
    mEnd = (mEnd + size) % mCapacity;
    mSize += size;
}

inline size_t RingBlob::Read(void* dst, size_t size)
{
    ASSERT(size > 0);
    
    size = Min(size, mSize);
    if (size == 0)
    return 0;
    
    if (dst) {
        uint8* buff = mBuffer;
        uint8* udata = reinterpret_cast<uint8*>(dst);
        size_t remain = mCapacity - mStart;
        if (remain >= size) {
            memcpy(udata, &buff[mStart], size);
        } else {
            memcpy(udata, &buff[mStart], remain);
            memcpy(&udata[remain], buff, size - remain);
        }
    }
    
    mStart = (mStart + size) % mCapacity;
    mSize -= size;
    return size;
}

inline size_t RingBlob::Peek(void* dst, size_t size, size_t* pOffset)
{
    ASSERT(size > 0);
    
    size = Min(size, mSize);
    if (size == 0)
    return 0;
    
    ASSERT(dst);
    uint8* buff = mBuffer;
    uint8* udata = reinterpret_cast<uint8*>(dst);
    size_t _offset = pOffset ? *pOffset : mStart;
    size_t remain = mCapacity - _offset;
    if (remain >= size) {
        memcpy(udata, &buff[_offset], size);
    } else {
        memcpy(udata, &buff[_offset], remain);
        memcpy(&udata[remain], buff, (size_t)size - (size_t)remain);
    }
    
    if (pOffset)
    *pOffset = (*pOffset + size) % mCapacity;

    return size;
}

template <typename _T> inline void RingBlob::Write(const _T& src)
{
    Write(&src, sizeof(_T));
}

template <typename _T> inline size_t RingBlob::Read(_T* dst)
{
    return Read(dst, sizeof(_T));
}

inline size_t RingBlob::Capacity() const
{
    return mCapacity;
}

//----------------------------------------------------------------------------------------------------------------------
// @impl Blob
inline Blob::Blob(void* buffer, size_t size)
{
    ASSERT(buffer && size);
    mBuffer = buffer;
    mCapacity = size;
}

inline void Blob::Attach(void* data, size_t size, Allocator* alloc)
{
    ASSERT(data);
    ASSERT_MSG(!mBuffer, "buffer should be freed before attach");
    mAlloc = alloc;
    mGrowPolicy = GrowPolicy::None;
    mBuffer = data;
    mOffset = 0;
    mSize = size;
    mCapacity = size;
}

inline void Blob::Detach(void** outData, size_t* outSize)
{
    ASSERT(outData);
    ASSERT(outSize);

    *outData = mBuffer;
    *outSize = mSize;

    mBuffer = nullptr;
    mSize = 0;
    mOffset = 0;
    mCapacity = 0;
}

inline void Blob::SetAllocator(Allocator* alloc)
{
    ASSERT_MSG(!mBuffer, "SetAllocator must be called before using/initializing the Blob");
    mAlloc = alloc;
}

inline void Blob::SetSize(size_t size)
{
    ASSERT_MSG(size <= mCapacity, "Size cannot be larger than capacity");
    mSize = size;
}

inline void Blob::Reserve(size_t capacity)
{
    ASSERT_MSG(mAlloc, "Allocator must be set for dynamic Reserve");
    ASSERT(capacity > mSize);

    mBuffer = memReallocAligned(mBuffer, capacity, mAlign, mAlloc);
    mCapacity = capacity;
}

inline void Blob::Reserve(void* buffer, size_t size)
{
    ASSERT(size > mSize);
    ASSERT(PtrToInt<uint64>(buffer) % mAlign == 0);
    ASSERT(mBuffer == nullptr);

    mBuffer = buffer;
    mCapacity = size;
    mAlloc = nullptr;
}

inline void Blob::Free()
{
    if (mAlloc)
    memFreeAligned(mBuffer, mAlign, mAlloc);
    mBuffer = nullptr;
    mSize = 0;
    mCapacity = 0;
    mAlloc = nullptr;
}

inline void Blob::ResetRead() 
{
    mOffset = 0;
}

inline void Blob::ResetWrite()
{
    mSize = 0;
}

inline void Blob::Reset()
{
    mOffset = 0;
    mSize = 0;
}

inline void Blob::SetOffset(size_t offset) 
{
    ASSERT(mOffset < mSize);
    mOffset = offset;
}

inline size_t Blob::Write(const void* src, size_t size)
{
    ASSERT(src);
    ASSERT(size);

    size_t writeBytes = Min(mCapacity - mSize, size);
    if (writeBytes < size) {
        ASSERT_MSG(mAlloc, "Growable blobs should have allocator");
        ASSERT_MSG(mGrowPolicy != GrowPolicy::None, "Growable blobs should have a grow policy");
        ASSERT(mGrowCount);

        if (mGrowPolicy == GrowPolicy::Linear) {
            mCapacity += mGrowCount;
            mBuffer = memReallocAligned(mBuffer, mCapacity, mAlign, mAlloc);
        }
        else if (mGrowPolicy == GrowPolicy::Multiply) {
            if (!mCapacity)
            mCapacity = mGrowCount;
            else
            mCapacity <<= 1;
            mBuffer = memReallocAligned(mBuffer, mCapacity, mAlign, mAlloc);
        }

        return Write(src, size);
    }

    if (writeBytes) {
        uint8* buff = reinterpret_cast<uint8*>(mBuffer);
        memcpy(buff + mSize, src, writeBytes);
        mSize += writeBytes;
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

    size_t readBytes = Min(mSize - mOffset, size);
    if (readBytes) {
        uint8* buff = reinterpret_cast<uint8*>(mBuffer);
        memcpy(dst, buff + mOffset, readBytes);
        const_cast<Blob*>(this)->mOffset += readBytes;
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

inline size_t Blob::Size() const
{
    return mSize;
}

inline size_t Blob::ReadOffset() const
{
    return mOffset;
}

inline size_t Blob::Capacity() const
{
    return mCapacity;
}

inline const void* Blob::Data() const
{
    return mBuffer;
}

inline bool Blob::IsValid() const
{
    return mBuffer && mSize;
}

inline void Blob::CopyTo(Blob* otherBlob) const
{
    ASSERT(mSize);
    otherBlob->Reserve(mSize);
    otherBlob->SetSize(mSize);
    memcpy(otherBlob->mBuffer, mBuffer, mSize); 
}

inline void Blob::SetAlignment(uint8 align)
{
    if (align < CONFIG_MACHINE_ALIGNMENT)
    align = CONFIG_MACHINE_ALIGNMENT;
    mAlign = align;
}

inline void Blob::SetGrowPolicy(GrowPolicy policy, uint32 amount)
{
    mGrowPolicy = policy;
    mGrowCount = amount == 0 ? 4096u : AlignValue(amount, CACHE_LINE_SIZE);
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

inline size_t Blob::ReadStringBinary16(char* outStr, [[maybe_unused]] uint32 outStrSize) const
{
    uint16 len = 0;
    size_t readStrBytes = 0;
    size_t readBytes = Read<uint16>(&len);
    ASSERT(readBytes == sizeof(len));
    ASSERT(len < outStrSize);
    if (len) {
        readStrBytes = Read(outStr, len);
        ASSERT(readStrBytes == len);
    }
    outStr[len] = '\0';
    return readStrBytes + readBytes;
}

inline size_t Blob::WriteStringBinary(const char* str, uint32 len)
{
    ASSERT(str);
    if (len == 0)
        len = strLen(str);
    size_t writtenBytes = Write<uint32>(len);
    if (len) 
        writtenBytes += Write(str, len);
    return writtenBytes;
}

inline size_t Blob::WriteStringBinary16(const char* str, uint32 len)
{
    ASSERT(str);
    if (len == 0)
        len = strLen(str);
    ASSERT(len < UINT16_MAX);
    size_t writtenBytes = Write<uint16>(uint16(len));
    if (len) 
        writtenBytes += Write(str, len);
    return writtenBytes;
}

