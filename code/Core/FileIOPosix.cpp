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
    FileIOFlags flags;
    size_t     size;    
};

bool fileOpen(FileData* file, const char* filepath, FileIOFlags flags)
{
    ASSERT((flags & (FileIOFlags::Read|FileIOFlags::Write)) != (FileIOFlags::Read|FileIOFlags::Write));
    ASSERT((flags & (FileIOFlags::Read|FileIOFlags::Write)) != FileIOFlags::None);

    FilePosix* f = (FilePosix*)file;
    memset(f, 0x0, sizeof(FilePosix));

    int openFlags = __O_LARGEFILE;
    mode_t mode = 0;

    if ((flags & FileIOFlags::Read) == FileIOFlags::Read) {
        openFlags |= O_RDONLY;
    } else if ((flags & FileIOFlags::Write) == FileIOFlags::Write) {
        openFlags |= O_WRONLY;
        if ((flags & FileIOFlags::Append) == FileIOFlags::Append) {
            openFlags |= O_APPEND;
        } else {
            openFlags |= (O_CREAT | O_TRUNC);
            mode |= (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH); 
        }
    }

    #if (PLATFORM_LINUX || PLATFORM_ANDROID)
        if ((flags & FileIOFlags::Temp) == FileIOFlags::Temp) {
            openFlags |= __O_TMPFILE;
        }
    #endif

    int fileId = open(filepath, openFlags, mode);
    if (fileId == -1) 
        return false;

    #if PLATFORM_APPLE
        if (flags & FileIOFlags::Nocache) {
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
    f->size = static_cast<size_t>(_stat.st_size);
    return true;
}

void fileClose(FileData* file)
{
    FilePosix* f = (FilePosix*)file;
    if (f && f->id) {
        close(f->id);
        f->id = 0;
    }
}

size_t fileRead(FileData* file, void* dst, size_t size)
{
    ASSERT(file);
    FilePosix* f = (FilePosix*)file;
    ASSERT(f->id && f->id != -1);
    
    if ((f->flags & FileIOFlags::NoCache) == FileIOFlags::NoCache) {
        static size_t pagesz = 0;
        if (pagesz == 0)
            pagesz = sysGetPageSize();
        ASSERT_ALWAYS((uintptr_t)dst % pagesz == 0, "buffers must be aligned with NoCache flag");
    }
    ssize_t r = read(f->id, dst, size);
    return r != -1 ? r : SIZE_MAX;
}

size_t fileWrite(FileData* file, const void* src, size_t size)
{
    ASSERT(file);
    FilePosix* f = (FilePosix*)file;

    ASSERT(f->id && f->id != -1);
    int64_t bytesWritten = write(f->id, src, size);
    if (bytesWritten > -1) {
        f->size += bytesWritten; 
        return bytesWritten;
    }
    else {
        return SIZE_MAX;
    }    
}

size_t fileSeek(FileData* file, size_t offset, FileIOSeekMode mode)
{
    ASSERT(file);

    FilePosix* f = (FilePosix*)file;
    ASSERT(f->id && f->id != -1);

    int _whence = 0;
    switch (mode) {
        case FileIOSeekMode::Current:    _whence = SEEK_CUR; break;
        case FileIOSeekMode::Start:      _whence = SEEK_SET; break;
        case FileIOSeekMode::End:        _whence = SEEK_END; break;
    }

    return static_cast<size_t>(lseek(f->id, static_cast<off_t>(offset), _whence));
}

size_t fileGetSize(FileData* file)
{
    ASSERT(file);

    FilePosix* f = (FilePosix*)file;
    return f->size;
}

bool fileIsOpen(FileData* file)
{
    FilePosix* f = (FilePosix*)file;
    return f && f->id != 0;
}

#endif // PLATFORM_POSIX