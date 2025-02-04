#include "System.h"

#if PLATFORM_LINUX

#include <unistd.h>

char* OS::GetMyPath(char* dst, size_t dstSize)
{
    ssize_t len = readlink("/proc/self/exe", dst, dstSize - 1);
    ASSERT(len != -1);
    dst[len] = '\0';
    return dst;
}

char* OS::GetCurrentDir(char* dst, size_t dstSize)
{
    return getcwd(dst, dstSize);
}

void OS::SetCurrentDir(const char* path)
{
    [[maybe_unused]] int r = chdir(path);
    ASSERT(r != -1);
}

void OS::GetSysInfo(SysInfo*)
{
    // TODO: implement
    ASSERT(0);
}

#endif // PLATFORM_LINUX