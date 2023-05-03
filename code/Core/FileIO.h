#pragma once

#include "Base.h"
#include "Memory.h"

enum class FileOpenFlags : uint32
{
    None         = 0,
    Read         = 0x01, // Open for reading
    Write        = 0x02, // Open for writing
    Append       = 0x03, // Append to the end of the file (write-mode only)
    NoCache      = 0x08, // Disable IO cache, suitable for very large files, remember to align buffers to virtual memory pages
    Writethrough = 0x10, // Write-through writes meta information to disk immediately
    SeqScan      = 0x20, // Optimize cache for sequential read (not to be used with NOCACHE)
    RandomAccess = 0x40, // Optimize cache for random access read (not to be used with NOCACHE)
    Temp         = 0x80  // Indicate that the file is temperary
};
ENABLE_BITMASK(FileOpenFlags);

enum class FileSeekMode
{
    Start = 0,
    Current,
    End 
};

struct File
{
    File();

    bool Open(const char* filepath, FileOpenFlags flags);
    void Close();

    size_t Read(void* dst, size_t size);
    size_t Write(const void* src, size_t size);
    size_t Seek(size_t offset, FileSeekMode mode = FileSeekMode::Start);

    template <typename _T> uint32 Read(_T* dst, uint32 count);
    template <typename _T> uint32 Write(_T* dst, uint32 count);

    size_t GetSize() const;
    uint64 GetLastModified() const;
    bool IsOpen() const;

private:
    uint8 _data[64];
};

//------------------------------------------------------------------------
template <typename _T> inline uint32 File::Read(_T* dst, uint32 count)
{
    return static_cast<uint32>(Read((void*)dst, sizeof(_T)*count)/sizeof(_T));
}

template <typename _T> inline uint32 File::Write(_T* dst, uint32 count)
{
    return static_cast<uint32>(Write((const void*)dst, sizeof(_T)*count)/sizeof(_T));
}
