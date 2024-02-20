#include "System.h"

#include "StringUtil.h"
#include "Blobs.h"

#ifdef BUILD_UNITY
    #if PLATFORM_WINDOWS
        #include "SystemWin.cpp"
    #elif PLATFORM_ANDROID
        #include "SystemPosix.cpp"
        #include "SystemAndroid.cpp"
    #elif PLATFORM_OSX
        #include "SystemMac.cpp"
    #else
        #error "Not implemented"
    #endif
#endif

// We are doing static initilization for timers
// We don't do much of that because of many pitfalls of this approach. 
// But here can be a safe exception for conveniency. Because timer initialization does not involve allocations or any sensitive init code
struct TimerInitializer
{
    TimerInitializer() { _private::timerInitialize(); }
};

static TimerInitializer gTimerInit;

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

void sysGenerateCmdLineFromArgcArgv(int argc, const char* argv[], char** outString, uint32* outStringLen, 
                                    Allocator* alloc, const char* prefixCmd)
{
    ASSERT(outString);
    ASSERT(outStringLen);

    Blob blob(alloc);
    blob.SetGrowPolicy(Blob::GrowPolicy::Linear, 256);

    // If we have a prefix command, append to the beginning
    if (prefixCmd) {
        blob.Write(prefixCmd, strLen(prefixCmd));
        blob.Write<char>(32);
    }

    // TODO: perform escaping on the strings
    for (int i = 0; i < argc; i++) {
        blob.Write(argv[i], strLen(argv[i]));
        if (i != argc - 1)
            blob.Write<char>(32);
    }
    blob.Write<char>(0);

    size_t len;
    blob.Detach((void**)outString, &len);
    *outStringLen = static_cast<uint32>(len);
}

namespace _private 
{
    bool socketParseUrl(const char* url, char* address, size_t addressSize, char* port, size_t portSize, const char** pResource)
    {
        uint32 urlLen = strLen(url);
    
        // skip the 'protocol://' part
        if (const char* addressBegin = strFindStr(url, "://"); addressBegin)
            url = addressBegin + 2;
    
        // find end of address part of url
        char const* addressEnd = strFindChar(url, ':');
        if (!addressEnd) addressEnd = strFindChar(url, '/');
        if (!addressEnd) addressEnd = url + urlLen;
        
        // extract address
        uint32 addressLen = PtrToInt<uint32>((void*)(addressEnd - url));
        if(addressLen >= addressSize) 
            return false;
        memcpy(address, url, addressLen);
        address[addressLen] = '\0';
        
        // check if there's a port defined
        char const* portEnd = addressEnd;
        if (*addressEnd == ':') {
            ++addressEnd;
            portEnd = strFindChar(addressEnd, '/');
            if (!portEnd) 
                portEnd = addressEnd + strLen(addressEnd);
            uint32 portLen = PtrToInt<uint32>((void*)(portEnd - addressEnd));
            if (portLen >= portSize) 
                return false;
            memcpy(port, addressEnd, portLen);
            port[portLen] = '\0';
        }
        else {
            return false;
        }    
    
        if (pResource)
            *pResource = portEnd;    
        return true;    
    }
}   // namespace _private

