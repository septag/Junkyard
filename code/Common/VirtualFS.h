#pragma once

#include "../Core/Base.h"
#include "../Core/Buffers.h"

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
//       So, care must be taken when implementing these callbacks so they would be thread-safe
using VfsReadAsyncCallback = void(*)(const char* path, const Blob& blob, void* user);
using VfsWriteAsyncCallback = void(*)(const char* path, size_t bytesWritten, const Blob& originalBlob, void* user);
using VfsFileChangeCallback = void(*)(const char* path);

API bool vfsMountLocal(const char* rootDir, const char* alias, bool watch);
API bool vfsMountRemote(const char* alias, bool watch);
API bool vfsMountPackageBundle(const char* alias);

// If file fails to load, Blob.IsValid() == false
// Note: This function works without initializing the virtual file-system
API Blob vfsReadFile(const char* path, VfsFlags flags, Allocator* alloc = memDefaultAlloc());

// If file fails to write, it will return 0, otherwise, it will return the number of bytes written
API size_t vfsWriteFile(const char* path, const Blob& blob, VfsFlags flags);

API void vfsReadFileAsync(const char* path, VfsFlags flags, VfsReadAsyncCallback readResultFn, void* user, Allocator* alloc = memDefaultAlloc());
API void vfsWriteFileAsync(const char* path, const Blob& blob, VfsFlags flags, VfsWriteAsyncCallback writeResultFn, void* user);

API VfsMountType vfsGetMountType(const char* path);
API uint64 vfsGetLastModified(const char* path);
API bool vfsStripMountPath(char* outPath, uint32 outPathSize, const char* path);

API void vfsRegisterFileChangeCallback(VfsFileChangeCallback callback);

namespace _private 
{
    bool vfsInitialize();
    void vfsRelease();
}
