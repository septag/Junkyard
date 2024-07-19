#pragma once

#include "../Core/Blobs.h"

struct Path;

enum class VfsFlags : uint32
{
    None = 0x0,
    AbsolutePath = 0x1,
    TextFile = 0x2,
    Append = 0x4,
    CreateDirs = 0x8
};
ENABLE_BITMASK(VfsFlags);

enum class VfsMountType 
{
    None,
    Local,
    Remote,
    PackageBundle
};

// Note: these callbacks are called from a VirtualFS worker thread
//       So, care must be taken when implementing these callbacks. Make sure global data access is thread-safe
using VfsReadAsyncCallback = void(*)(const char* path, const Blob& blob, void* user);
using VfsWriteAsyncCallback = void(*)(const char* path, size_t bytesWritten, const Blob& originalBlob, void* user);
using VfsFileChangeCallback = void(*)(const char* path);

namespace Vfs
{
    API bool MountLocal(const char* rootDir, const char* alias, bool watch);
    API bool MountRemote(const char* alias, bool watch);
    API bool MountPackageBundle(const char* alias);

    // If file fails to load, Blob.IsValid() == false
    // Note: This function works without initializing the virtual file-system
    API Blob ReadFile(const char* path, VfsFlags flags, MemAllocator* alloc = Mem::GetDefaultAlloc(), Path* outResolvedPath = nullptr);

    // If file fails to write, it will return 0, otherwise, it will return the number of bytes written
    API size_t WriteFile(const char* path, const Blob& blob, VfsFlags flags);

    API void ReadFileAsync(const char* path, VfsFlags flags, VfsReadAsyncCallback readResultFn, void* user, MemAllocator* alloc = Mem::GetDefaultAlloc());
    API void WriteFileAsync(const char* path, const Blob& blob, VfsFlags flags, VfsWriteAsyncCallback writeResultFn, void* user);

    API VfsMountType GetMountType(const char* path);
    API uint64 GetLastModified(const char* path);
    API Path ResolveFilepath(const char* path);
    API bool StripMountPath(char* outPath, uint32 outPathSize, const char* path);

    API void RegisterFileChangeCallback(VfsFileChangeCallback callback);

    API bool Initialize();
    API void Release();
}


