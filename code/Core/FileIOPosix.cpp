#include "FileIO.h"

#if PLATFORM_POSIX

#ifndef _LARGEFILE64_SOURCE
    #define _LARGEFILE64_SOURCE
#endif

#include <memory.h>    // memset
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#undef _LARGEFILE64_SOURCE
#ifndef __O_LARGEFILE4
    #define __O_LARGEFILE 0
#endif

#include "System.h"

struct FilePosix
{
    int         id;
    FileOpenFlags flags;
    uint64      size;  
    uint64      lastModifiedTime;
};
static_assert(sizeof(FilePosix) <= sizeof(File));

File::File()
{
    FilePosix* f = (FilePosix*)this->_data;
    f->id = -1;
    f->flags = FileOpenFlags::None;
    f->size = 0;
    f->lastModifiedTime = 0;
}

bool File::Open(const char* filepath, FileOpenFlags flags)
{
    ASSERT((flags & (FileOpenFlags::Read|FileOpenFlags::Write)) != (FileOpenFlags::Read|FileOpenFlags::Write));
    ASSERT((flags & (FileOpenFlags::Read|FileOpenFlags::Write)) != FileOpenFlags::None);

    FilePosix* f = (FilePosix*)this->_data;

    int openFlags = __O_LARGEFILE;
    mode_t mode = 0;

    if ((flags & FileOpenFlags::Read) == FileOpenFlags::Read) {
        openFlags |= O_RDONLY;
    } else if ((flags & FileOpenFlags::Write) == FileOpenFlags::Write) {
        openFlags |= O_WRONLY;
        if ((flags & FileOpenFlags::Append) == FileOpenFlags::Append) {
            openFlags |= O_APPEND;
        } else {
            openFlags |= (O_CREAT | O_TRUNC);
            mode |= (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH); 
        }
    }

    #if (PLATFORM_LINUX || PLATFORM_ANDROID)
        if ((flags & FileOpenFlags::Temp) == FileOpenFlags::Temp) {
            openFlags |= __O_TMPFILE;
        }
    #endif

    int fileId = open(filepath, openFlags, mode);
    if (fileId == -1) 
        return false;

    #if PLATFORM_APPLE
        if (flags & FileOpenFlags::Nocache) {
            if (fcntl(fileId, F_NOCACHE) != 0) {
                return false;
            }
        }
    #endif

    struct stat _stat;
    int sr = fstat(fileId, &_stat);
    if (sr != 0) {
        ASSERT_MSG(0, "stat failed!");
        return false;
    }

    f->id = fileId;
    f->flags = flags;
    f->size = static_cast<uint64>(_stat.st_size);
    f->lastModifiedTime = static_cast<uint64>(_stat.st_mtime);
    return true;
}

void File::Close()
{
    FilePosix* f = (FilePosix*)this->_data;

    if (f->id != -1) {
        close(f->id);
        f->id = -1;
    }
}

size_t File::Read(void* dst, size_t size)
{
    FilePosix* f = (FilePosix*)this->_data;
    ASSERT(f->id != -1);
    
    if ((f->flags & FileOpenFlags::NoCache) == FileOpenFlags::NoCache) {
        static size_t pagesz = 0;
        if (pagesz == 0)
            pagesz = sysGetPageSize();
        ASSERT_ALWAYS((uintptr_t)dst % pagesz == 0, "buffers must be aligned with NoCache flag");
    }
    ssize_t r = read(f->id, dst, size);
    return r != -1 ? r : SIZE_MAX;
}

size_t File::Write(const void* src, size_t size)
{
    FilePosix* f = (FilePosix*)this->_data;
    ASSERT(f->id != -1);

    int64_t bytesWritten = write(f->id, src, size);
    if (bytesWritten > -1) {
        f->size += bytesWritten; 
        return bytesWritten;
    }
    else {
        return SIZE_MAX;
    }    
}

size_t File::Seek(size_t offset, FileSeekMode mode)
{
    FilePosix* f = (FilePosix*)this->_data;
    ASSERT(f->id != -1);

    int _whence = 0;
    switch (mode) {
    case FileSeekMode::Current:    _whence = SEEK_CUR; break;
    case FileSeekMode::Start:      _whence = SEEK_SET; break;
    case FileSeekMode::End:        _whence = SEEK_END; break;
    }

    return size_t(lseek(f->id, static_cast<off_t>(offset), _whence));
}

size_t File::GetSize() const
{
    const FilePosix* f = (const FilePosix*)this->_data;
    return f->size;
}

uint64 File::GetLastModified() const
{
    const FilePosix* f = (const FilePosix*)this->_data;
    return f->lastModifiedTime;
}

bool File::IsOpen() const
{
    FilePosix* f = (FilePosix*)this->_data;
    return f->id != -1;
}

#endif // PLATFORM_POSIX