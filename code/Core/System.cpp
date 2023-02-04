#include "Base.h"
#include "String.h"

#include "System.h"

#ifdef BUILD_UNITY
    #if PLATFORM_WINDOWS
        #include "SystemWin.cpp"
    #elif PLATFORM_ANDROID
        #include "SystemPosix.cpp"
        #include "SystemAndroid.cpp"
    #elif PLATFORM_APPLE
        #include "SystemMac.cpp"
    #else
        #error "Not implemented"
    #endif
#endif

char* pathToUnix(const char *path, char *dst, size_t dstSize)
{
    size_t len = strLen(path);
    len = Min<size_t>(len, dstSize - 1);

    for (int i = 0; i < len; i++) {
        if (path[i] != '\\')
            dst[i] = path[i];
        else
            dst[i] = '/';
    }
    dst[len] = '\0';
    return dst;
}

char* pathToWin(const char *path, char *dst, size_t dstSize)
{
    size_t len = strLen(path);
    len = Min<size_t>(len, dstSize - 1);

    for (int i = 0; i < len; i++) {
        if (path[i] != '/')
            dst[i] = path[i];
        else
            dst[i] = '\\';
    }
    dst[len] = '\0';
    return dst;
}

char* pathFileExtension(const char *path, char *dst, size_t dstSize)
{
    ASSERT(dstSize > 0);

    int len = strLen(path);
    if (len > 0) {
        const char *start = strrchr(path, '/');
        if (!start)
            start = strrchr(path, '\\');
        if (!start)
            start = path;
        const char *end = &path[len - 1];
        for (const char *e = start; e < end; ++e) {
            if (*e != '.')
                continue;
            strCopy(dst, (uint32)dstSize, e);
            return dst;
        }
    }

    dst[0] = '\0'; // no extension
    return dst;
}

char* pathFileNameAndExt(const char *path, char *dst, size_t dstSize)
{
    const char *r = strrchr(path, '/');
    if (!r)
        r = strrchr(path, '\\');
    if (r) {
        strCopy(dst, (uint32)dstSize, r + 1);
    }
    else if (dst != path) {
        strCopy(dst, (uint32)dstSize, path);
    }
    return dst;
}

char* pathFileName(const char* path, char* dst, size_t dstSize)
{
    const char *r = strrchr(path, '/');
    if (!r)
        r = strrchr(path, '\\');
    if (r) {
        strCopy(dst, (uint32)dstSize, r + 1);
    }
    else if (dst != path) {
        strCopy(dst, (uint32)dstSize, path);
    }

    char* dot = strrchr(dst, '.');
    if (dot) 
        *dot = '\0';

    return dst;
}


char* pathDirectory(const char *path, char *dst, size_t dstSize)
{
    const char *r = strrchr(path, '/');
    if (!r)
        r = strrchr(path, '\\');
    if (r) {
        int o = (int)(intptr_t)(r - path);
        if (dst == path) {
            dst[o] = '\0';
        }
        else {
            strCopyCount(dst, (uint32)dstSize, path, o);
        }
    }
    else if (dst != path) {
        *dst = '\0';
    }
    return dst;
}

static char* pathJoin(char *dst, size_t dstSize, const char *sep, const char *pathA, const char *pathB)
{
    ASSERT(dst != pathB);
    int len = strLen(pathA);
    if (dst != pathA) {
        if (len > 0 && pathA[len - 1] == sep[0]) {
            strCopy(dst, (uint32)dstSize, pathA);
        }
        else if (len > 0) {
            strCopy(dst, (uint32)dstSize, pathA);
            strConcat(dst, (uint32)dstSize, sep);
        }
        else {
            dst[0] = '\0';
        }
    }
    else if (len > 0 && pathA[len - 1] != sep[0]) {
        strConcat(dst, (uint32)dstSize, sep);
    }

    if (pathB[0] == sep[0])
        ++pathB;
    strConcat(dst, (uint32)dstSize, pathB);
    return dst;
}

char* pathJoin(char *dst, size_t dstSize, const char *pathA, const char *pathB)
{
    #if PLATFORM_WINDOWS || PLATFORM_SCARLETT
        const char *kSep = "\\";
    #else
        const char *kSep = "/";
    #endif

    return pathJoin(dst, dstSize, kSep, pathA, pathB);
}

char* pathJoinUnixStyle(char *dst, size_t dstSize, const char *pathA, const char *pathB)
{
    return pathJoin(dst, dstSize, "/", pathA, pathB);
}

uint64 timerLapTime(uint64* lastTime)
{
    ASSERT(lastTime);
    uint64 dt = 0;
    uint64 now = timerGetTicks();
    if (*lastTime != 0) 
        dt = timerDiff(now, *lastTime);
    *lastTime = now;
    return dt;
}
