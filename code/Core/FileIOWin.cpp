#include "FileIO.h"

#if PLATFORM_WINDOWS
#include "System.h"
#include "IncludeWin.h"

struct FileWin
{
    HANDLE      handle;
    FileIOFlags flags;
    uint64      size;
    uint64      lastModifiedTime;
};

bool fileOpen(FileData* file, const char* filepath, FileIOFlags flags)
{
    ASSERT((flags & (FileIOFlags::Read|FileIOFlags::Write)) != (FileIOFlags::Read|FileIOFlags::Write));
    ASSERT((flags & (FileIOFlags::Read|FileIOFlags::Write)) != FileIOFlags::None);

    FileWin* f = (FileWin*)file;
    memset(f, 0x0, sizeof(FileWin));
    
    uint32 accessFlags = GENERIC_READ;
    uint32 attrs = FILE_ATTRIBUTE_NORMAL;
    uint32 createFlags = 0;
    uint32 shareFlags = 0;

    if ((flags & FileIOFlags::Read) == FileIOFlags::Read) {
        createFlags = OPEN_EXISTING;
        shareFlags |= FILE_SHARE_READ;
    } else if ((flags & FileIOFlags::Write) == FileIOFlags::Write) {
        shareFlags |= FILE_SHARE_WRITE;
        accessFlags |= GENERIC_WRITE;
        createFlags |= (flags & FileIOFlags::Append) == FileIOFlags::Append ? 
            OPEN_EXISTING : CREATE_ALWAYS;
    }

    if ((flags & FileIOFlags::NoCache) == FileIOFlags::NoCache)             attrs |= FILE_FLAG_NO_BUFFERING;
    if ((flags & FileIOFlags::Writethrough) == FileIOFlags::Writethrough)   attrs |= FILE_FLAG_WRITE_THROUGH;
    if ((flags & FileIOFlags::SeqScan) == FileIOFlags::SeqScan)             attrs |= FILE_FLAG_SEQUENTIAL_SCAN;
    if ((flags & FileIOFlags::RandomAccess) == FileIOFlags::RandomAccess)   attrs |= FILE_FLAG_RANDOM_ACCESS;
    if ((flags & FileIOFlags::Temp) == FileIOFlags::Temp)                   attrs |= FILE_ATTRIBUTE_TEMPORARY;

    HANDLE hfile = CreateFileA(filepath, accessFlags, shareFlags, NULL, createFlags, attrs, NULL);
    if (hfile == INVALID_HANDLE_VALUE)
        return false;

    f->handle = hfile;
    f->flags = flags;

    BY_HANDLE_FILE_INFORMATION fileInfo {};
    GetFileInformationByHandle(hfile, &fileInfo);
    f->size = (flags & (FileIOFlags::Read|FileIOFlags::Append)) != FileIOFlags::None ? 
              (uint64(fileInfo.nFileSizeHigh)<<32 | uint64(fileInfo.nFileSizeLow)) : 0;
    f->lastModifiedTime = uint64(fileInfo.ftLastAccessTime.dwHighDateTime)<<32 | uint64(fileInfo.ftLastAccessTime.dwLowDateTime);

    return true;
}

void fileClose(FileData* file)
{
    FileWin* f = (FileWin*)file;
    if (f && f->handle && f->handle != INVALID_HANDLE_VALUE) {
        CloseHandle(f->handle);
        f->handle = nullptr;
    }
}

size_t fileRead(FileData* file, void* dst, size_t size)
{
    ASSERT(file);
    FileWin* f = (FileWin*)file;

    ASSERT(f->handle && f->handle != INVALID_HANDLE_VALUE);

    if ((f->flags & FileIOFlags::NoCache) == FileIOFlags::NoCache) {
        static size_t pagesz = 0;
        if (pagesz == 0) {
            pagesz = sysGetPageSize();
        }
        ASSERT_ALWAYS((uintptr_t)dst % pagesz == 0, "buffers must be aligned with NoCache flag");
    }

    DWORD bytesRead;
    if (!ReadFile(f->handle, dst, (DWORD)size, &bytesRead, NULL)) {
        return SIZE_MAX;
    }

    return static_cast<size_t>(bytesRead);
}

size_t fileWrite(FileData* file, const void* src, size_t size)
{
    ASSERT(file);
    FileWin* f = (FileWin*)file;

    ASSERT(f->handle && f->handle != INVALID_HANDLE_VALUE);

    DWORD bytesWritten;
    if (!WriteFile(f->handle, src, (DWORD)size, &bytesWritten, NULL))
        return SIZE_MAX;
    f->size += bytesWritten;

    return bytesWritten;
}

size_t fileSeek(FileData* file, size_t offset, FileIOSeekMode mode)
{
    ASSERT(file);

    FileWin* f = (FileWin*)file;

    DWORD move_method = 0;
    switch (mode) {
    case FileIOSeekMode::Start:
        move_method = FILE_BEGIN;
        break;
    case FileIOSeekMode::Current:
        move_method = FILE_CURRENT;
        break;
    case FileIOSeekMode::End:
        ASSERT(offset <= f->size);
        move_method = FILE_END;
        break;
    }

    LARGE_INTEGER large_off;
    LARGE_INTEGER large_ret;
    large_off.QuadPart = (LONGLONG)offset;

    if (SetFilePointerEx(f->handle, large_off, &large_ret, move_method))
        return (int64_t)large_ret.QuadPart;

    return SIZE_MAX;
}

size_t fileGetSize(const FileData* file)
{
    ASSERT(file);

    FileWin* f = (FileWin*)file;
    return static_cast<size_t>(f->size);    
}

uint64 fileGetLastModified(const FileData* file)
{
    ASSERT(file);

    FileWin* f = (FileWin*)file;
    return f->lastModifiedTime;
}

bool fileIsOpen(FileData* file)
{
    FileWin* f = (FileWin*)file;
    return f && f->handle != nullptr;
}

#endif // PLATFORM_WINDOWS