#include "FileIO.h"

#if PLATFORM_WINDOWS
#include "System.h"
#include "IncludeWin.h"

struct FileWin
{
    HANDLE      handle;
    FileOpenFlags flags;
    uint64      size;
    uint64      lastModifiedTime;
};
static_assert(sizeof(FileWin) <= sizeof(File));

//------------------------------------------------------------------------
File::File()
{
    FileWin* f = (FileWin*)this->_data;

    f->handle = INVALID_HANDLE_VALUE;
    f->flags = FileOpenFlags::None;
    f->size = 0;
    f->lastModifiedTime = 0;
}

bool File::Open(const char* filepath, FileOpenFlags flags)
{
    ASSERT((flags & (FileOpenFlags::Read|FileOpenFlags::Write)) != (FileOpenFlags::Read|FileOpenFlags::Write));
    ASSERT((flags & (FileOpenFlags::Read|FileOpenFlags::Write)) != FileOpenFlags::None);

    FileWin* f = (FileWin*)this->_data;

    uint32 accessFlags = GENERIC_READ;
    uint32 attrs = FILE_ATTRIBUTE_NORMAL;
    uint32 createFlags = 0;
    uint32 shareFlags = 0;

    if ((flags & FileOpenFlags::Read) == FileOpenFlags::Read) {
        createFlags = OPEN_EXISTING;
        shareFlags |= FILE_SHARE_READ;
    } else if ((flags & FileOpenFlags::Write) == FileOpenFlags::Write) {
        shareFlags |= FILE_SHARE_WRITE;
        accessFlags |= GENERIC_WRITE;
        createFlags |= (flags & FileOpenFlags::Append) == FileOpenFlags::Append ? 
            OPEN_EXISTING : CREATE_ALWAYS;
    }

    if ((flags & FileOpenFlags::NoCache) == FileOpenFlags::NoCache)             attrs |= FILE_FLAG_NO_BUFFERING;
    if ((flags & FileOpenFlags::Writethrough) == FileOpenFlags::Writethrough)   attrs |= FILE_FLAG_WRITE_THROUGH;
    if ((flags & FileOpenFlags::SeqScan) == FileOpenFlags::SeqScan)             attrs |= FILE_FLAG_SEQUENTIAL_SCAN;
    if ((flags & FileOpenFlags::RandomAccess) == FileOpenFlags::RandomAccess)   attrs |= FILE_FLAG_RANDOM_ACCESS;
    if ((flags & FileOpenFlags::Temp) == FileOpenFlags::Temp)                   attrs |= FILE_ATTRIBUTE_TEMPORARY;

    HANDLE hfile = CreateFileA(filepath, accessFlags, shareFlags, NULL, createFlags, attrs, NULL);
    if (hfile == INVALID_HANDLE_VALUE)
        return false;

    f->handle = hfile;
    f->flags = flags;

    BY_HANDLE_FILE_INFORMATION fileInfo {};
    GetFileInformationByHandle(hfile, &fileInfo);
    f->size = (flags & (FileOpenFlags::Read|FileOpenFlags::Append)) != FileOpenFlags::None ? 
        (uint64(fileInfo.nFileSizeHigh)<<32 | uint64(fileInfo.nFileSizeLow)) : 0;
    f->lastModifiedTime = uint64(fileInfo.ftLastAccessTime.dwHighDateTime)<<32 | uint64(fileInfo.ftLastAccessTime.dwLowDateTime);

    return true;

}

void File::Close()
{
    FileWin* f = (FileWin*)this->_data;

    if (f->handle != INVALID_HANDLE_VALUE) {
        CloseHandle(f->handle);
        f->handle = INVALID_HANDLE_VALUE;
    }
}

size_t File::Read(void* dst, size_t size)
{
    FileWin* f = (FileWin*)this->_data;
    ASSERT(f->handle != INVALID_HANDLE_VALUE);

    if ((f->flags & FileOpenFlags::NoCache) == FileOpenFlags::NoCache) {
        static size_t pagesz = 0;
        if (pagesz == 0) {
            pagesz = sysGetPageSize();
        }
        ASSERT_ALWAYS((uintptr_t)dst % pagesz == 0, "buffers must be aligned with NoCache flag");
    }

    DWORD bytesRead;
    if (!ReadFile(f->handle, dst, (DWORD)size, &bytesRead, NULL))
        return SIZE_MAX;

    return size_t(bytesRead);
}

size_t File::Write(const void* src, size_t size)
{
    FileWin* f = (FileWin*)this->_data;
    ASSERT(f->handle != INVALID_HANDLE_VALUE);

    DWORD bytesWritten;
    if (!WriteFile(f->handle, src, (DWORD)size, &bytesWritten, NULL))
        return SIZE_MAX;
    f->size += bytesWritten;

    return bytesWritten;
}

size_t File::Seek(size_t offset, FileSeekMode mode)
{
    FileWin* f = (FileWin*)this->_data;
    ASSERT(f->handle != INVALID_HANDLE_VALUE);

    DWORD moveMethod = 0;
    switch (mode) {
    case FileSeekMode::Start:
        moveMethod = FILE_BEGIN;
        break;
    case FileSeekMode::Current:
        moveMethod = FILE_CURRENT;
        break;
    case FileSeekMode::End:
        ASSERT(offset <= f->size);
        moveMethod = FILE_END;
        break;
    }

    LARGE_INTEGER largeOff;
    LARGE_INTEGER largeRet;
    largeOff.QuadPart = (LONGLONG)offset;

    if (SetFilePointerEx(f->handle, largeOff, &largeRet, moveMethod))
        return (int64_t)largeRet.QuadPart;

    return SIZE_MAX;
}

size_t File::GetSize() const
{
    FileWin* f = (FileWin*)this->_data;
    return size_t(f->size);    
}

uint64 File::GetLastModified() const
{
    FileWin* f = (FileWin*)this->_data;
    return f->lastModifiedTime;
}

bool File::IsOpen() const
{
    FileWin* f = (FileWin*)this->_data;
    return f->handle != INVALID_HANDLE_VALUE;
}

#endif // PLATFORM_WINDOWS