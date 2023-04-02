#pragma once

#include "Base.h"
#include "Memory.h"

enum class FileIOFlags : uint32
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
ENABLE_BITMASK(FileIOFlags);

enum class FileIOSeekMode
{
    Start = 0,
    Current,
    End 
};

struct alignas(16) FileData 
{
    uint8 data[64];
};

API bool fileOpen(FileData* file, const char* filepath, FileIOFlags flags);
API void fileClose(FileData* file);
API bool fileIsOpen(FileData* file);

// These functions return SIZE_MAX on error
API size_t fileRead(FileData* file, void* dst, size_t size);
API size_t fileWrite(FileData* file, const void* src, size_t size);
API size_t fileSeek(FileData* file, size_t offset, FileIOSeekMode mode);
API size_t fileGetSize(const FileData* file);
API uint64 fileGetLastModified(const FileData* file);

struct File
{
    inline File();
    inline explicit File(const char* filepath, FileIOFlags flags);

    inline bool Open(const char* filepath, FileIOFlags flags);
    inline void Close();

    inline size_t Read(void* dst, size_t size);
    inline size_t Write(const void* src, size_t size);
    inline size_t Seek(size_t offset, FileIOSeekMode mode = FileIOSeekMode::Start);

    template <typename _T> uint32 Read(_T* dst, uint32 count);
    template <typename _T> uint32 Write(_T* dst, uint32 count);

    inline size_t GetSize() const;
    inline uint64 GetLastModified() const;
    inline bool IsOpen();

private:
    FileData _data;
};

//------------------------------------------------------------------------
inline File::File()
{
    memset(&_data, 0x0, 8);
}

inline File::File(const char* filepath, FileIOFlags flags)
{
    fileOpen(&_data, filepath, flags);
}

inline bool File::Open(const char* filepath, FileIOFlags flags)
{
    return fileOpen(&_data, filepath, flags);
}

inline void File::Close()
{
    return fileClose(&_data);
}

inline size_t File::Read(void* dst, size_t size)
{
    return fileRead(&_data, dst, size);
}

inline size_t File::Write(const void* src, size_t size)
{
    return fileWrite(&_data, src, size);
}

inline size_t File::Seek(size_t offset, FileIOSeekMode mode)
{
    return fileSeek(&_data, offset, mode);
}

inline size_t File::GetSize() const
{
    return fileGetSize(&_data);
}

inline uint64 File::GetLastModified() const
{
    return fileGetLastModified(&_data);
}

inline bool File::IsOpen()
{
    return fileIsOpen(&_data);
}

template <typename _T> inline uint32 File::Read(_T* dst, uint32 count)
{
    return static_cast<uint32>(fileRead(&_data, dst, sizeof(_T)*count)/sizeof(_T));
}

template <typename _T> inline uint32 File::Write(_T* dst, uint32 count)
{
    return static_cast<uint32>(fileWrite(&_data, dst, sizeof(_T)*count)/sizeof(_T));
}
