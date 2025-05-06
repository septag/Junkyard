#include "VirtualFS.h"
#include "JunkyardSettings.h"
#include "RemoteServices.h"

#include "../Core/Log.h"
#include "../Core/System.h"
#include "../Core/StringUtil.h"
#include "../Core/Settings.h"
#include "../Core/TracyHelper.h"
#include "../Core/Arrays.h"
#include "../Core/Allocators.h"
#include "../Core/Hash.h"

#include "../Engine.h"

#if PLATFORM_ANDROID
    #include "Application.h"    // appGetNativeAssetManagerHandle
    #include <android/asset_manager.h>
#endif


//     ██████╗ ██╗      ██████╗ ██████╗  █████╗ ██╗     ███████╗
//    ██╔════╝ ██║     ██╔═══██╗██╔══██╗██╔══██╗██║     ██╔════╝
//    ██║  ███╗██║     ██║   ██║██████╔╝███████║██║     ███████╗
//    ██║   ██║██║     ██║   ██║██╔══██╗██╔══██║██║     ╚════██║
//    ╚██████╔╝███████╗╚██████╔╝██████╔╝██║  ██║███████╗███████║
//     ╚═════╝ ╚══════╝ ╚═════╝ ╚═════╝ ╚═╝  ╚═╝╚══════╝╚══════╝
                                                              
static constexpr uint32 VFS_REMOTE_READ_FILE_CMD = MakeFourCC('F', 'R', 'D', '0');
static constexpr uint32 VFS_REMOTE_WRITE_FILE_CMD = MakeFourCC('F', 'W', 'T', '0');
static constexpr uint32 VFS_REMOTE_READ_FILE_INFO_CMD = MakeFourCC('F', 'I', 'N', 'F');
static constexpr uint32 VFS_REMOTE_MONITOR_CHANGES_CMD = MakeFourCC('D', 'M', 'O', 'N');
static constexpr uint32 VFS_REMOTE_MONITOR_CHANGES_INTERVAL = 1000;

struct VfsMountPoint
{
    VfsMountType type;
    uint32 watchId;
    Path path;
    Path alias;
};

enum class VfsCommand
{
    Read,
    Write,
    Info
};

struct VfsFileChangeEvent
{
    Path filepath;
};

struct VfsFileReadWriteRequest
{
    VfsMountType mountType;
    VfsCommand cmd;
    VfsFlags flags;
    Path path;
    Blob blob;
    MemAllocator* alloc;
    void* user;
    union Callbacks 
    {
        VfsReadAsyncCallback readFn;
        VfsWriteAsyncCallback writeFn;
        VfsInfoAsyncCallback infoFn;
    } callbacks;
};

struct VfsAsyncManager
{
    Semaphore semaphore;
    Thread thread;
    Array<VfsFileReadWriteRequest> requests;
    Mutex requestsMtx;
};

struct VfsRemoteManager
{
    Mutex requestsMtx;
    Array<VfsFileReadWriteRequest> requests;
};

struct VfsManager
{
    MemProxyAllocator alloc;
    Thread reqFileChangesThrd;

    Mutex fileChangesMtx;
    Array<VfsFileChangeEvent> fileChanges;
    Array<VfsFileChangeCallback> fileChangeCallbacks;

    Array<VfsMountPoint> mounts;

    VfsAsyncManager asyncMgr;
    VfsRemoteManager remoteMgr;

    bool quit;
    bool initialized;
};

static VfsManager gVfs;

#if CONFIG_TOOLMODE
    #define DMON_IMPL
    #define DMON_MALLOC(size) Mem::Alloc(size, &gVfs.alloc)
    #define DMON_FREE(ptr) Mem::Free(ptr, &gVfs.alloc)
    #define DMON_REALLOC(ptr, size) Mem::Realloc(ptr, size, &gVfs.alloc)
    #define DMON_LOG_ERROR(s) LOG_ERROR(s)
    #define DMON_LOG_DEBUG(s) LOG_DEBUG(s)
    
    #include <stdio.h>  // snprintf 
    PRAGMA_DIAGNOSTIC_PUSH()
    PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4505)
    PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wc++11-narrowing")
    #include "../External/dmon/dmon.h"
    PRAGMA_DIAGNOSTIC_POP()
#endif

// Fwd
namespace Vfs
{
    static uint32 _FindMount(const char* path);
    static uint32 _ResolveDiskPath(char* dstPath, uint32 dstPathSize, const char* path, VfsFlags flags);
    static Blob _DiskReadFile(const char* path, VfsFlags flags, MemAllocator* alloc, Path* outResolvedPath = nullptr);
    static size_t _DiskWriteFile(const char* path, VfsFlags flags, const Blob& blob);
    static void _MonitorChangesClientCallback(uint32 cmd, const Blob& incomingData, void*, bool error, const char* errorDesc);
    static bool _MonitorChangesServerCallback([[maybe_unused]] uint32 cmd, const Blob& incomingData, Blob* outgoingData, void*, char outgoingErrorDesc[REMOTE_ERROR_SIZE]);
    static int _AsyncWorkerThread(void*);
    static void _RemoteReadFileComplete(const char* path, const Blob& blob, void*);
    static void _RemoteWriteFileComplete(const char* path, size_t bytesWritten, Blob&, void*);
    static bool _ReadFileHandlerServerFn(uint32 cmd, const Blob& incomingData, Blob* outgoingData, void*, char outgoingErrorDesc[REMOTE_ERROR_SIZE]);
    static bool _ReadFileInfoHandlerServerFn(uint32 cmd, const Blob& incomingData, Blob* outgoingData, void*, char outgoingErrorDesc[REMOTE_ERROR_SIZE]);
    static bool _WriteFileHandlerServerFn(uint32 cmd, const Blob& incomingData, Blob* outgoingData, void*, char outgoingErrorDesc[REMOTE_ERROR_SIZE]);
    static void _ReadFileHandlerClientFn([[maybe_unused]] uint32 cmd, const Blob& incomingData, void* userData, bool error, const char* errorDesc);
    static void _ReadFileInfoHandlerClientFn([[maybe_unused]] uint32 cmd, const Blob& incomingData, void* userData, bool error, const char* errorDesc);
    static void _WriteFileHandlerClientFn([[maybe_unused]] uint32 cmd, const Blob& incomingData, void* userData, bool error, const char* errorDesc);

    #if CONFIG_TOOLMODE
    static void _DmonCallback(dmon_watch_id watchId, dmon_action action, const char* rootDir, const char* filepath, const char*, void*);
    #endif

    #if PLATFORM_ANDROID
    static Blob _PackageBundleReadFile(const char* path, VfsFlags flags, MemAllocator* alloc);
    #endif
} // Vfs

//    ███╗   ███╗ ██████╗ ██╗   ██╗███╗   ██╗████████╗███████╗
//    ████╗ ████║██╔═══██╗██║   ██║████╗  ██║╚══██╔══╝██╔════╝
//    ██╔████╔██║██║   ██║██║   ██║██╔██╗ ██║   ██║   ███████╗
//    ██║╚██╔╝██║██║   ██║██║   ██║██║╚██╗██║   ██║   ╚════██║
//    ██║ ╚═╝ ██║╚██████╔╝╚██████╔╝██║ ╚████║   ██║   ███████║
//    ╚═╝     ╚═╝ ╚═════╝  ╚═════╝ ╚═╝  ╚═══╝   ╚═╝   ╚══════╝
bool Vfs::MountLocal(const char* rootDir, const char* alias, [[maybe_unused]] bool watch)
{
    if (!OS::IsPathDir(rootDir)) {
        char absRootPath[CONFIG_MAX_PATH];
        OS::GetAbsolutePath(rootDir, absRootPath, sizeof(absRootPath));
        LOG_ERROR("VirtualFS: '%s' is not a valid directory (%s)", rootDir, absRootPath);
        if constexpr(!CONFIG_FINAL_BUILD) {
            LOG_ERROR("VirtualFS: Make sure cwd on the root directory of the project and assets are downloaded for this app");
        }
        return false;
    }

    VfsMountPoint mount {
        .type = VfsMountType::Local,
        .path = Path(rootDir).GetAbsolute().ConvertToUnix(),
        .alias = alias
    };
    if (mount.path.EndsWith('/'))
        mount.path[mount.path.Length()-1] = '\0';

    if (gVfs.mounts.FindIf([&mount](const VfsMountPoint& m)->bool 
        { return mount.alias == m.alias || mount.path == m.path; }) != UINT32_MAX)
    {
        LOG_ERROR("VirtualFS: Mount point with RootDir '%s' already added", mount.path.CStr());
        return false;
    }

    #if CONFIG_TOOLMODE
    if (watch)
        mount.watchId = dmon_watch(rootDir, _DmonCallback, DMON_WATCHFLAGS_RECURSIVE, nullptr).id;
    #endif

    gVfs.mounts.Push(mount);
    LOG_INFO("Mounted local path '%s' to alias '%s'", mount.path.CStr(), mount.alias.CStr());
    return true;
}

bool Vfs::MountRemote(const char* alias, bool watch)
{
    static bool requestThreadInit = false;

    ASSERT_MSG(SettingsJunkyard::Get().engine.connectToServer, "Remote services is not enabled in settings");
    const char* url = SettingsJunkyard::Get().engine.remoteServicesUrl.CStr();
    
    VfsMountPoint mount {
        .type = VfsMountType::Remote,
        .path = url,
        .alias = alias
    };

    if (gVfs.mounts.FindIf([alias](const VfsMountPoint& m)->bool 
        { return m.type == VfsMountType::Remote && m.alias.IsEqual(alias); }) != UINT32_MAX)
    {
        LOG_ERROR("VirtualFS: Remote mount point with alias '%s' already added", url);
        return false;
    }

    // Run a thread that pings server for file changes
    if (watch && !requestThreadInit) {
        requestThreadInit = true;

        auto ReqFileChangesThreadFn = [](void*)
        {
            while (!gVfs.quit) {
                Remote::ExecuteCommand(VFS_REMOTE_MONITOR_CHANGES_CMD, Blob());
                Thread::Sleep(VFS_REMOTE_MONITOR_CHANGES_INTERVAL);
            }
            return 0;
        };

        // TODO: maybe create a coroutine instead of a thread here ?
        if (!gVfs.reqFileChangesThrd.IsRunning()) {
            gVfs.reqFileChangesThrd.Start(ThreadDesc {
                .entryFn = ReqFileChangesThreadFn, 
                .name = "VfsRequestRemoteFileChanges",
                .stackSize = 64*SIZE_KB
            });
            gVfs.reqFileChangesThrd.SetPriority(ThreadPriority::Idle);
        }
    }

    mount.watchId = watch ? 1 : 0;

    gVfs.mounts.Push(mount);
    LOG_INFO("Mounted '%s' on remote service '%s'", alias, url);
    return true;
}

static uint32 Vfs::_FindMount(const char* path)
{
    if (path[0] == '/') 
        path = path + 1;
    return gVfs.mounts.FindIf([path](const VfsMountPoint& mount)->bool 
    {
        return Str::IsEqualCount(path, mount.alias.CStr(), mount.alias.Length()) && path[mount.alias.Length()] == '/';
    });
}

VfsMountType Vfs::GetMountType(const char* path)
{
    uint32 idx = _FindMount(path);   
    if (idx != UINT32_MAX)
        return gVfs.mounts[idx].type;
    else
        return VfsMountType::None;
}

bool Vfs::StripMountPath(char* outPath, uint32 outPathSize, const char* path)
{
    bool found = false;
    uint32 index = _FindMount(path);
    if (index != UINT32_MAX) {
        if (path[0] == '/')
            ++path;
        const char* stripped = path + gVfs.mounts[index].alias.Length();
        Str::Copy(outPath, outPathSize, stripped);
        found = true;
    }
    else {
        Str::Copy(outPath, outPathSize, path);
    }
    return found;
}

#if PLATFORM_MOBILE
bool Vfs::MountPackageBundle(const char* alias)
{
    VfsMountPoint mount {
        .type = VfsMountType::PackageBundle,
        .alias = alias
    };

    if (gVfs.mounts.FindIf([&mount](const VfsMountPoint& m)->bool 
                           { return mount.alias == m.alias; }) != UINT32_MAX)
    {
        LOG_ERROR("VirtualFS: Mount point with alias '%s' already added", mount.alias.CStr());
        return false;
    }

    gVfs.mounts.Push(mount);
    LOG_INFO("Mounted app package bundle to alias '%s'", mount.alias.CStr());
    return true;
}
#else   // PLATFORM_MOBILE
bool Vfs::MountPackageBundle(const char*)
{
    ASSERT_MSG(0, "This function must only be used on mobile platforms");
    return false;
}
#endif // !PLATFORM_MOBILE

//    ██████╗ ██╗███████╗██╗  ██╗    ██╗ ██████╗ 
//    ██╔══██╗██║██╔════╝██║ ██╔╝    ██║██╔═══██╗
//    ██║  ██║██║███████╗█████╔╝     ██║██║   ██║
//    ██║  ██║██║╚════██║██╔═██╗     ██║██║   ██║
//    ██████╔╝██║███████║██║  ██╗    ██║╚██████╔╝
//    ╚═════╝ ╚═╝╚══════╝╚═╝  ╚═╝    ╚═╝ ╚═════╝ 
static uint32 Vfs::_ResolveDiskPath(char* dstPath, uint32 dstPathSize, const char* path, VfsFlags flags)
{
    uint32 index = uint32(-1);
    if ((flags & VfsFlags::AbsolutePath) != VfsFlags::AbsolutePath) {
        // search in mount points
        if (path[0] == '/') 
            path = path + 1;

        index = gVfs.mounts.FindIf([path](const VfsMountPoint& mount)->bool {
            return Str::IsEqualCount(path, mount.alias.CStr(), mount.alias.Length()) && path[mount.alias.Length()] == '/';
        });
        if (index != -1) {
            char tmpPath[PATH_CHARS_MAX];
            Str::Copy(tmpPath, sizeof(tmpPath), path + gVfs.mounts[index].alias.Length());
            PathUtils::JoinUnixStyle(dstPath, dstPathSize, gVfs.mounts[index].path.CStr(), tmpPath);
        }
    }
    return index;
}

static Blob Vfs::_DiskReadFile(const char* path, VfsFlags flags, MemAllocator* alloc, Path* outResolvedPath)
{
    auto LoadFromDisk = [](const char* path, VfsFlags flags, MemAllocator* alloc)->Blob {
        File f;
        Blob blob(alloc ? alloc : &gVfs.alloc);

        if (f.Open(path, FileOpenFlags::Read | FileOpenFlags::SeqScan)) {
            uint64 size = f.GetSize();
            if (size) {
                if ((flags & VfsFlags::TextFile) == VfsFlags::TextFile)
                    blob.Reserve(size + 1);
                else
                    blob.Reserve(size);

                size_t bytesRead = f.Read(const_cast<void*>(blob.Data()), size);
                blob.SetSize(bytesRead);

                if ((flags & VfsFlags::TextFile) == VfsFlags::TextFile)
                    blob.Write<char>('\0');
            }

            f.Close();
        }
        return blob;
    };

    ASSERT_MSG(GetMountType(path) != VfsMountType::Remote, "Remote mounts cannot read files in blocking mode");

    char resolvedPath[PATH_CHARS_MAX];
    if (_ResolveDiskPath(resolvedPath, sizeof(resolvedPath), path, flags) != UINT32_MAX) {
        if (outResolvedPath)
            *outResolvedPath = resolvedPath;
        return LoadFromDisk(resolvedPath, flags, alloc);
    }
    else {
        if (outResolvedPath)
            *outResolvedPath = path;
        return LoadFromDisk(path, flags, alloc);
    }
}

static size_t Vfs::_DiskWriteFile(const char* path, VfsFlags flags, const Blob& blob)
{
    auto SaveToDisk = [](const char* path, VfsFlags, const Blob& blob)->size_t 
    {
        File f;
        char tempPath[PATH_CHARS_MAX];
        char tempName[PATH_CHARS_MAX];
        PathUtils::GetFilename(path, tempName, sizeof(tempName));

        #if PLATFORM_WINDOWS
        // On windows, we better use the same path of the destination file
        // Because if it's a different volume, then it does a copy/delete instead
        char tempDir[PATH_CHARS_MAX];
        PathUtils::GetDirectory(path, tempDir, sizeof(tempDir));
        #else
        char* tempDir = nullptr;    // use /tmp on posix
        #endif
        
        bool makeTempSuccess = OS::MakeTempPath(tempPath, sizeof(tempPath), tempName, tempDir);
        if (!makeTempSuccess)
            LOG_WARNING("Making temp file failed: %s", path);

        size_t bytesWritten = 0;
        if (f.Open(makeTempSuccess ? tempPath : path, FileOpenFlags::Write)) {
            bytesWritten = f.Write(blob.Data(), blob.Size());
            f.Close();
            
            if (bytesWritten && makeTempSuccess && !OS::MovePath(tempPath, path)) {
                bytesWritten = 0; // Could not move file from temp to the actual path
            }
        }

        return bytesWritten;
    };

    auto CheckAndCreateDirsRecursive = [](const char* resolvedPath, const char* mountRootDir) {
        Path dirname = Path(resolvedPath).GetDirectory();
        if (!dirname.IsDir()) {
            uint32 mountRootDirLen = mountRootDir ? Str::Len(mountRootDir) : 0;
            uint32 slashIdx = mountRootDirLen;
            while ((slashIdx = dirname.FindChar('/', slashIdx + 1)) != UINT32_MAX) {
                Path subDir(dirname.SubStr(0, slashIdx));
                if (!subDir.IsDir()) {
                    [[maybe_unused]] bool r = OS::CreateDir(subDir.CStr());
                    ASSERT(r);
                }
            }
            if (!dirname.IsDir()) {
                [[maybe_unused]] bool r = OS::CreateDir(dirname.CStr());
                ASSERT(r);
            }
        }
    };

    ASSERT_MSG(GetMountType(path) != VfsMountType::Remote, "Remote mounts cannot read files in blocking mode");
    char resolvedPath[PATH_CHARS_MAX];
    if (uint32 mountIdx = _ResolveDiskPath(resolvedPath, sizeof(resolvedPath), path, flags); mountIdx != UINT32_MAX) {
        if ((flags & VfsFlags::CreateDirs) == VfsFlags::CreateDirs) 
            CheckAndCreateDirsRecursive(resolvedPath, gVfs.mounts[mountIdx].path.CStr());
        return SaveToDisk(resolvedPath, flags, blob);
    }
    else {
        if ((flags & VfsFlags::CreateDirs) == VfsFlags::CreateDirs) 
            CheckAndCreateDirsRecursive(path, nullptr);
        return SaveToDisk(path, flags, blob);
    }
}

#if PLATFORM_ANDROID
static Blob Vfs::_PackageBundleReadFile(const char* path, VfsFlags flags, MemAllocator* alloc)
{
    auto LoadFromAssetManager = [](const char* path, VfsFlags flags, MemAllocator* alloc)->Blob {
        AAsset* asset = AAssetManager_open(App::AndroidGetAssetManager(), path, AASSET_MODE_BUFFER);
        if (!asset)
            return Blob {};
        Blob blob(alloc ? alloc : &gVfs.alloc);
        off_t assetSize = AAsset_getLength(asset);
        if (assetSize > 0) {
            if ((flags & VfsFlags::TextFile) == VfsFlags::TextFile)
                blob.Reserve(assetSize + 1);
            else
                blob.Reserve(assetSize);

            int bytesRead = AAsset_read(asset, const_cast<void*>(blob.Data()), static_cast<size_t>(assetSize));
            AAsset_close(asset);
            if (bytesRead == assetSize) {
                blob.SetSize(assetSize);
                if ((flags & VfsFlags::TextFile) == VfsFlags::TextFile)
                    blob.Write<char>('\0');
            }
            else {
                blob.Free();
            }
        }
        return blob;
    };

    ASSERT_MSG((flags & VfsFlags::AbsolutePath) != VfsFlags::AbsolutePath, "Absoluate paths doesn't work on PackageBundle mounts");
    // search in mount points
    if (path[0] == '/') 
        path = path + 1;

    uint32 idx = gVfs.mounts.FindIf([path](const VfsMountPoint& mount)->bool {
        return Str::IsEqualCount(path, mount.alias.CStr(), mount.alias.Length()) &&
               path[mount.alias.Length()] == '/';
    });
    return idx != UINT32_MAX ? 
        LoadFromAssetManager(path + gVfs.mounts[idx].alias.Length() + 1, flags, alloc) : 
        LoadFromAssetManager(path, flags, alloc);
}
#endif // PLATFORM_ANDROID

Blob Vfs::ReadFile(const char* path, VfsFlags flags, MemAllocator* alloc, Path* outResolvedPath)
{
    ASSERT((flags & VfsFlags::CreateDirs) != VfsFlags::CreateDirs);
    ASSERT((flags & VfsFlags::Append) != VfsFlags::Append);

    uint32 idx = _FindMount(path);
    Blob blob;
    if (idx != UINT32_MAX) {
        VfsMountType type = gVfs.mounts[idx].type;
        
        if (type == VfsMountType::Local) {
            blob = _DiskReadFile(path, flags, alloc, outResolvedPath);
        }
        else if (type == VfsMountType::Remote) {
            LOG_WARNING("Performance warning: Trying to load file '%s' synchronously on a remote mount", path);

            struct SyncReadWorkaroundData
            {
                Signal sig;
                Blob* blob;
            };
            SyncReadWorkaroundData data {};
            data.sig.Initialize();
            data.blob = &blob;

            Vfs::ReadFileAsync(path, flags, [](const char*, const Blob& blob, void* userData) {
                SyncReadWorkaroundData* data = (SyncReadWorkaroundData*)userData;
                if (blob.IsValid()) 
                    const_cast<Blob&>(blob).MoveTo(data->blob);
                data->sig.Set();
                data->sig.Raise();
            }, &data, alloc);

            data.sig.Wait();
            data.sig.Release();
        }
        #if PLATFORM_MOBILE
        else if (type == VfsMountType::PackageBundle) {
            blob = _PackageBundleReadFile(path, flags, alloc);
        }
        #endif
    }
    else {
        #if PLATFORM_ANDROID
        // take a guess, and see if we have 'assets' at the start, 
        const char* normPath = path;
        if (normPath[0] == '/')
            normPath++;
        const char predefinedAssets[] = "assets/";
        if (Str::IsEqualNoCaseCount(normPath, predefinedAssets, sizeof(predefinedAssets)-1))
            return _PackageBundleReadFile(normPath + sizeof(predefinedAssets) - 1, flags, alloc);
        #endif // PALTFORM_ANDROID

        if (outResolvedPath)
            *outResolvedPath = path;
        blob = _DiskReadFile(path, flags, alloc);
    }
    return blob;
}

size_t Vfs::WriteFile(const char* path, const Blob& blob, VfsFlags flags)
{
    uint32 idx = _FindMount(path);
    if (idx != UINT32_MAX) {
        [[maybe_unused]] VfsMountType type = gVfs.mounts[idx].type;
        ASSERT_MSG(type != VfsMountType::Remote, "Remote file requests cannot be done in blocking mode, call vfsWriteFileAsync");
        ASSERT_MSG(type != VfsMountType::PackageBundle, "Cannot write to PackageBundle mounts");
        ASSERT(type == VfsMountType::Local);
        return _DiskWriteFile(path, flags, blob);
    }
    else {
        return _DiskWriteFile(path, flags, blob);
    }
}

uint64 Vfs::GetLastModified(const char* path)
{
    return Vfs::GetFileInfo(path).lastModified;
}

uint64 Vfs::GetFileSize(const char* path)
{
    return Vfs::GetFileInfo(path).size;
}

PathInfo Vfs::GetFileInfo(const char* path)
{
    if (GetMountType(path) != VfsMountType::Remote) {
        char resolvedPath[PATH_CHARS_MAX];
        if (_ResolveDiskPath(resolvedPath, sizeof(resolvedPath), path, VfsFlags::None) != UINT32_MAX)
            return OS::GetPathInfo(resolvedPath);
        else 
            return OS::GetPathInfo(path);
    }
    else {
        LOG_WARNING("Performance warning: Trying to get file info '%s' synchronously on a remote mount", path);

        struct SyncReadInfoWorkaroundData
        {
            Signal sig;
            PathInfo pathInfo;
        };
        SyncReadInfoWorkaroundData data {};
        data.sig.Initialize();

        auto readInfoCallback = [](const char*, const PathInfo& info, void* userData) {
            SyncReadInfoWorkaroundData* data = (SyncReadInfoWorkaroundData*)userData;
            data->pathInfo = info;
            data->sig.Set();
            data->sig.Raise();
        };

        if (Remote::IsConnected()) {
            VfsRemoteManager* mgr = &gVfs.remoteMgr;
            MutexScope mtx(mgr->requestsMtx);
            VfsFileReadWriteRequest req {
                .mountType = VfsMountType::Remote,
                .cmd = VfsCommand::Info,
                .path = path,
                .user = &data,
                .callbacks {
                    .infoFn = readInfoCallback
                }
            };
            mgr->requests.Push(req);
        }

        // Make the remote request
        MemTempAllocator tempAlloc;
        Blob reqBlob(&tempAlloc);
        reqBlob.WriteStringBinary(path);
        Remote::ExecuteCommand(VFS_REMOTE_READ_FILE_INFO_CMD, reqBlob);

        data.sig.Wait();
        data.sig.Release();

        return data.pathInfo;
    }
}

Path Vfs::ResolveFilepath(const char* path)
{
    ASSERT_MSG(GetMountType(path) != VfsMountType::Remote, "Remote mounts cannot read files in blocking mode");

    char resolvedPath[PATH_CHARS_MAX];
    if (_ResolveDiskPath(resolvedPath, sizeof(resolvedPath), path, VfsFlags::None) != UINT32_MAX)
        return Path(resolvedPath);
    else
        return Path(path);
}

bool Vfs::FileExists(const char* path)
{
    ASSERT_MSG(GetMountType(path) != VfsMountType::Remote, "Remote mounts cannot read files in blocking mode");

    char resolvedPath[PATH_CHARS_MAX];
    if (_ResolveDiskPath(resolvedPath, sizeof(resolvedPath), path, VfsFlags::None) != UINT32_MAX)
        return OS::PathExists(resolvedPath);
    else
        return OS::PathExists(path);
}

//    ██╗  ██╗ ██████╗ ████████╗    ██████╗ ███████╗██╗      ██████╗  █████╗ ██████╗ 
//    ██║  ██║██╔═══██╗╚══██╔══╝    ██╔══██╗██╔════╝██║     ██╔═══██╗██╔══██╗██╔══██╗
//    ███████║██║   ██║   ██║       ██████╔╝█████╗  ██║     ██║   ██║███████║██║  ██║
//    ██╔══██║██║   ██║   ██║       ██╔══██╗██╔══╝  ██║     ██║   ██║██╔══██║██║  ██║
//    ██║  ██║╚██████╔╝   ██║       ██║  ██║███████╗███████╗╚██████╔╝██║  ██║██████╔╝
//    ╚═╝  ╚═╝ ╚═════╝    ╚═╝       ╚═╝  ╚═╝╚══════╝╚══════╝ ╚═════╝ ╚═╝  ╚═╝╚═════╝ 
void Vfs::RegisterFileChangeCallback(VfsFileChangeCallback callback)
{
    ASSERT(callback);

    gVfs.fileChangeCallbacks.Push(callback);
}

#if CONFIG_TOOLMODE
static void Vfs::_DmonCallback(dmon_watch_id watchId, dmon_action action, const char* rootDir, const char* filepath, const char*, void*)
{
    switch (action) {
    case DMON_ACTION_CREATE:    // Some programs (like Krita), delete and re-add the file after modification (Ctrl+S)
    case DMON_ACTION_MODIFY:
        {
            Path absFilepath = Path::Join(Path(rootDir), filepath);
            PathInfo info = absFilepath.Stat();
            if (info.type == PathType::File && info.size) {
                for (uint32 i = 0; i < gVfs.mounts.Count(); i++) {
                    const VfsMountPoint& mount = gVfs.mounts[i];
                    if (mount.watchId == watchId.id) {
                        Path aliasFilepath = Path::JoinUnix(mount.alias, Path(filepath));
                        if (mount.type == VfsMountType::Local) {
                            for (uint32 k = 0; k < gVfs.fileChangeCallbacks.Count(); k++) {
                                VfsFileChangeCallback callback = gVfs.fileChangeCallbacks[k];
                                callback(aliasFilepath.CStr());
                            }
                        }
                    
                        if (SettingsJunkyard::Get().tooling.enableServer) {
                            MutexScope mtx(gVfs.fileChangesMtx);
                            uint32 filepathHash = Hash::Fnv32Str(aliasFilepath.CStr());
                            
                            uint32 index = gVfs.fileChanges.FindIf([filepathHash](const VfsFileChangeEvent& e) {
                                return Hash::Fnv32Str(e.filepath.CStr()) == filepathHash;
                            });

                            if (index == -1)
                                gVfs.fileChanges.Push(VfsFileChangeEvent { .filepath = aliasFilepath });
                        }
                        break;
                    } // if mount.watchId == watchId
                }
            }
        }
        break;
    default: 
        break;
    }
}
#endif // CONFIG_TOOLMODE

static void Vfs::_MonitorChangesClientCallback([[maybe_unused]] uint32 cmd, const Blob& incomingData, void*, bool error, const char* errorDesc)
{
    ASSERT(cmd == VFS_REMOTE_MONITOR_CHANGES_CMD);
    UNUSED(error);
    UNUSED(errorDesc);
    
    uint32 numChanges = 0;
    incomingData.Read<uint32>(&numChanges);
    if (numChanges > 0) {
        for (uint32 i = 0; i < numChanges; i++) {
            char filepath[PATH_CHARS_MAX];
            incomingData.ReadStringBinary(filepath, sizeof(filepath));

            // search in mount points
            const char* path = filepath;
            if (path[0] == '/') 
                path = path + 1;

            uint32 index = gVfs.mounts.FindIf([path](const VfsMountPoint& mount) { 
                return Str::IsEqualCount(path, mount.alias.CStr(), mount.alias.Length()) && path[mount.alias.Length()] == '/';
            });

            if (index != UINT32_MAX && gVfs.mounts[index].type == VfsMountType::Remote && gVfs.mounts[index].watchId) {
                for (uint32 k = 0; k < gVfs.fileChangeCallbacks.Count(); k++ ) {
                    VfsFileChangeCallback callback = gVfs.fileChangeCallbacks[k];
                    callback(filepath);
                }
            }
        } // foreach (change-event)
    }
}

static bool Vfs::_MonitorChangesServerCallback([[maybe_unused]] uint32 cmd, const Blob& incomingData, Blob* outgoingData, 
                                            void*, char outgoingErrorDesc[REMOTE_ERROR_SIZE])
{
    ASSERT(cmd == VFS_REMOTE_MONITOR_CHANGES_CMD);
    UNUSED(outgoingErrorDesc);
    UNUSED(incomingData);
        
    MutexScope lk(gVfs.fileChangesMtx);
    uint32 numChanges = gVfs.fileChanges.Count();
    outgoingData->Write<uint32>(numChanges);

    if (numChanges > 0) {
        for (uint32 i = 0; i < numChanges; i++) {
            const VfsFileChangeEvent& event = gVfs.fileChanges[i];
            outgoingData->Write<uint32>(event.filepath.Length());
            outgoingData->Write(event.filepath.CStr(), event.filepath.Length());
        }
    }

    gVfs.fileChanges.Clear();
    return true;
}

//     █████╗ ███████╗██╗   ██╗███╗   ██╗ ██████╗    ██╗ ██████╗ 
//    ██╔══██╗██╔════╝╚██╗ ██╔╝████╗  ██║██╔════╝    ██║██╔═══██╗
//    ███████║███████╗ ╚████╔╝ ██╔██╗ ██║██║         ██║██║   ██║
//    ██╔══██║╚════██║  ╚██╔╝  ██║╚██╗██║██║         ██║██║   ██║
//    ██║  ██║███████║   ██║   ██║ ╚████║╚██████╗    ██║╚██████╔╝
//    ╚═╝  ╚═╝╚══════╝   ╚═╝   ╚═╝  ╚═══╝ ╚═════╝    ╚═╝ ╚═════╝ 
static int Vfs::_AsyncWorkerThread(void*)
{
    VfsAsyncManager* mgr = &gVfs.asyncMgr;

    while (!gVfs.quit) {
        // Read requests
        VfsFileReadWriteRequest req;
        bool haveReq = false;
        {
            MutexScope mtx(mgr->requestsMtx);
            if (mgr->requests.Count()) {
                req = mgr->requests.PopFirst();
                haveReq = true;
            }
        }

        if (haveReq) {
            switch (req.cmd) {
            case VfsCommand::Read: 
            {
                Blob blob;
                if (req.mountType == VfsMountType::Local)
                    blob = _DiskReadFile(req.path.CStr(), req.flags, req.alloc);
                #if PLATFORM_MOBILE
                else if (req.mountType == VfsMountType::PackageBundle)
                    blob = _PackageBundleReadFile(req.path.CStr(), req.flags, req.alloc);
                #endif

                ASSERT(req.callbacks.readFn);
                req.callbacks.readFn(req.path.CStr(), blob, req.user);
                blob.Free();
                break;
            }
            case VfsCommand::Write: 
            {
                ASSERT_MSG(req.mountType == VfsMountType::Local, "Write only supports local mounts");
                ASSERT(req.callbacks.writeFn);
                size_t bytesWritten = _DiskWriteFile(req.path.CStr(), req.flags, req.blob);
                req.callbacks.writeFn(req.path.CStr(), bytesWritten, req.blob, req.user);
                if ((req.flags & VfsFlags::NoCopyWriteBlob) != VfsFlags::NoCopyWriteBlob)
                    req.blob.Free();
                break; 
            }
            case VfsCommand::Info:
                break;
            }
        }

        mgr->semaphore.Wait();
    }

    return 0;
}

void Vfs::ReadFileAsync(const char* path, VfsFlags flags, VfsReadAsyncCallback readResultFn, void* user, MemAllocator* alloc)
{
    ASSERT(gVfs.initialized);

    VfsFileReadWriteRequest req {
        .cmd = VfsCommand::Read,
        .flags = flags,
        .path = path,
        .alloc = alloc,
        .user = user,
        .callbacks = { .readFn = readResultFn }
    };

    uint32 idx = _FindMount(path);
    if (idx != UINT32_MAX && gVfs.mounts[idx].type == VfsMountType::Remote) {
        if (Remote::IsConnected()) {
            req.mountType = VfsMountType::Remote;

            {
                VfsRemoteManager* mgr = &gVfs.remoteMgr;
                MutexScope mtx(mgr->requestsMtx);
                mgr->requests.Push(req);
            }

            MemTempAllocator tmpAlloc;
            Blob paramsBlob;
            paramsBlob.SetAllocator(&tmpAlloc);
            paramsBlob.SetGrowPolicy(Blob::GrowPolicy::Multiply);

            paramsBlob.WriteStringBinary(path, Str::Len(path));

            Remote::ExecuteCommand(VFS_REMOTE_READ_FILE_CMD, paramsBlob);
            paramsBlob.Free();
        }
        else {
            LOG_WARNING("Mount point '%s' connection has lost, file '%s' cannot be loaded", gVfs.mounts[idx].path.CStr(), path);
        }
    }
    else {
        req.mountType = idx != UINT32_MAX ? gVfs.mounts[idx].type : VfsMountType::Local;

        VfsAsyncManager* diskMgr = &gVfs.asyncMgr;
        MutexScope mtx(diskMgr->requestsMtx);
        diskMgr->requests.Push(req);
        diskMgr->semaphore.Post();
    }
}

void Vfs::WriteFileAsync(const char* path, const Blob& blob, VfsFlags flags, VfsWriteAsyncCallback writeResultFn, void* user)
{
    ASSERT(gVfs.initialized);

    VfsFileReadWriteRequest req {
        .cmd = VfsCommand::Write,
        .flags = flags,
        .path = path,
        .user = user,
        .callbacks = { .writeFn = writeResultFn }
    };

    uint32 idx = _FindMount(path);
    if (idx != UINT32_MAX && gVfs.mounts[idx].type == VfsMountType::Remote) {
        if (Remote::IsConnected()) {
            // TODO: see if can add NoCopyWriteBlob flag here as well
            req.mountType = VfsMountType::Remote;

            {
                VfsRemoteManager* mgr = &gVfs.remoteMgr;
                MutexScope mtx(mgr->requestsMtx);
                mgr->requests.Push(req);
            }

            MemTempAllocator tmpAlloc;
            Blob paramsBlob;
            paramsBlob.SetAllocator(&tmpAlloc);
            paramsBlob.SetGrowPolicy(Blob::GrowPolicy::Multiply);

            paramsBlob.WriteStringBinary(path, Str::Len(path));
            paramsBlob.Write<VfsFlags>(flags);

            ASSERT(blob.Size() < UINT32_MAX);
            paramsBlob.Write<uint32>(static_cast<uint32>(blob.Size()));
            paramsBlob.Write(blob.Data(), blob.Size());

            Remote::ExecuteCommand(VFS_REMOTE_WRITE_FILE_CMD, paramsBlob);
            paramsBlob.Free();
        }
        else {
            LOG_WARNING("Mount point '%s' connection has lost, file '%s' cannot be written", gVfs.mounts[idx].path.CStr(), path);
        }
    }
    else {
        req.mountType = idx != UINT32_MAX ? gVfs.mounts[idx].type : VfsMountType::Local;

        VfsAsyncManager* diskMgr = &gVfs.asyncMgr;

        if ((flags & VfsFlags::NoCopyWriteBlob) != VfsFlags::NoCopyWriteBlob) {
            req.blob.SetAllocator(&gVfs.alloc);
            blob.CopyTo(&req.blob);
        }
        else {
            req.blob = blob;
        }

        {
            MutexScope mtx(diskMgr->requestsMtx);
            diskMgr->requests.Push(req);
        }
        
        diskMgr->semaphore.Post();
    }
}


//    ██████╗ ███████╗███╗   ███╗ ██████╗ ████████╗███████╗    ██╗ ██████╗ 
//    ██╔══██╗██╔════╝████╗ ████║██╔═══██╗╚══██╔══╝██╔════╝    ██║██╔═══██╗
//    ██████╔╝█████╗  ██╔████╔██║██║   ██║   ██║   █████╗      ██║██║   ██║
//    ██╔══██╗██╔══╝  ██║╚██╔╝██║██║   ██║   ██║   ██╔══╝      ██║██║   ██║
//    ██║  ██║███████╗██║ ╚═╝ ██║╚██████╔╝   ██║   ███████╗    ██║╚██████╔╝
//    ╚═╝  ╚═╝╚══════╝╚═╝     ╚═╝ ╚═════╝    ╚═╝   ╚══════╝    ╚═╝ ╚═════╝ 
static void Vfs::_RemoteReadFileComplete(const char* path, const Blob& blob, void*)
{
    bool error = !blob.IsValid();
    char errorDesc[REMOTE_ERROR_SIZE];   errorDesc[0] = '\0';
    if (error)
        Str::Copy(errorDesc, sizeof(errorDesc), path);


    if (!error) {
        MemTempAllocator tmpAlloc;
        Blob responseBlob;
        responseBlob.SetAllocator(&tmpAlloc);      // TODO: use temp allocator
        responseBlob.SetGrowPolicy(Blob::GrowPolicy::Multiply);
        responseBlob.WriteStringBinary(path, Str::Len(path));
        responseBlob.Write(blob.Data(), blob.Size());
        Remote::SendResponse(VFS_REMOTE_READ_FILE_CMD, responseBlob, error, errorDesc);
        responseBlob.Free();
    }
    else {
        Remote::SendResponse(VFS_REMOTE_READ_FILE_CMD, blob, error, errorDesc); 
    }
}

static void Vfs::_RemoteWriteFileComplete(const char* path, size_t bytesWritten, Blob&, void*)
{
    bool error = bytesWritten == 0;
    char errorDesc[REMOTE_ERROR_SIZE];   errorDesc[0] = '\0';
    if (error)
        Str::Copy(errorDesc, sizeof(errorDesc), path);

    if (!error) {
        MemTempAllocator tmpAlloc;
        Blob responseBlob;
        responseBlob.SetAllocator(&tmpAlloc);      // TODO: use temp allocator
        responseBlob.SetGrowPolicy(Blob::GrowPolicy::Multiply);
        responseBlob.Write(path, Str::Len(path));
        responseBlob.Write<size_t>(bytesWritten);

        Remote::SendResponse(VFS_REMOTE_WRITE_FILE_CMD, responseBlob, error, errorDesc);
        responseBlob.Free();
    }
    else {
        Remote::SendResponse(VFS_REMOTE_WRITE_FILE_CMD, Blob(), error, errorDesc); 
    }
}

static bool Vfs::_ReadFileHandlerServerFn(uint32 cmd, const Blob& incomingData, Blob* outgoingData, 
                                          void*, char outgoingErrorDesc[REMOTE_ERROR_SIZE])
{
    ASSERT(cmd == VFS_REMOTE_READ_FILE_CMD);
    UNUSED(cmd);
    UNUSED(outgoingData);
    UNUSED(outgoingErrorDesc);

    char filepath[PATH_CHARS_MAX]; 
    incomingData.ReadStringBinary(filepath, sizeof(filepath));

    // The async process finished when the program returns into the callback `vfsRemoteReadFileComplete`
    ReadFileAsync(filepath, VfsFlags::None, _RemoteReadFileComplete, nullptr, &gVfs.alloc);
    
    return true;
}

static bool Vfs::_ReadFileInfoHandlerServerFn(uint32 cmd, const Blob& incomingData, Blob* outgoingData, void*, 
                                              char outgoingErrorDesc[REMOTE_ERROR_SIZE])
{
    ASSERT(cmd == VFS_REMOTE_READ_FILE_INFO_CMD);

    char filepath[PATH_CHARS_MAX];
    incomingData.ReadStringBinary(filepath, sizeof(filepath));

    ASSERT_MSG(GetMountType(filepath) != VfsMountType::Remote, "Remote mounts cannot read files in this mode");
    PathInfo info = Vfs::GetFileInfo(filepath);

    if (info.type != PathType::Invalid) {
        outgoingData->WriteStringBinary(filepath, Str::Len(filepath));
        outgoingData->Write<uint32>(uint32(info.type));
        outgoingData->Write<uint64>(info.size);
        outgoingData->Write<uint64>(info.lastModified);
        
        return true;
    }
    else {
        Str::PrintFmt(outgoingErrorDesc, REMOTE_ERROR_SIZE, "Failed to fetch info for file: %s", filepath);
        return false;
    }
}

static bool Vfs::_WriteFileHandlerServerFn(uint32 cmd, const Blob& incomingData, Blob* outgoingData, 
                                           void*, char outgoingErrorDesc[REMOTE_ERROR_SIZE])
{
    ASSERT(cmd == VFS_REMOTE_READ_FILE_CMD);
    UNUSED(cmd);
    UNUSED(outgoingData);
    UNUSED(outgoingErrorDesc);

    char filepath[PATH_CHARS_MAX];
    incomingData.ReadStringBinary(filepath, sizeof(filepath));
   
    VfsFlags flags;
    incomingData.Read<VfsFlags>(&flags);

    uint32 bufferSize = 0;
    incomingData.Read<uint32>(&bufferSize);

    if (bufferSize) {
        Blob blob;
        blob.Reserve((uint8*)incomingData.Data() + incomingData.ReadOffset(), bufferSize);
        // The async process finished when the program returns into the callback `vfsRemoteReadFileComplete`
        WriteFileAsync(filepath, blob, flags, _RemoteWriteFileComplete, nullptr);
    }
    
    return bufferSize > 0;
}

static void Vfs::_ReadFileHandlerClientFn([[maybe_unused]] uint32 cmd, const Blob& incomingData, void* userData, 
                                          bool error, const char* errorDesc)
{
    ASSERT(cmd == VFS_REMOTE_READ_FILE_CMD);
    UNUSED(userData);

    auto PopRequest = [](const char* filepath, VfsFileReadWriteRequest* pReq)->bool {
        VfsRemoteManager* mgr = &gVfs.remoteMgr;
        MutexScope mtx(mgr->requestsMtx);
        if (uint32 index = mgr->requests.FindIf([filepath](const VfsFileReadWriteRequest& req)->bool 
            { return req.path.IsEqual(filepath); }); index != UINT32_MAX)
        {
            *pReq = mgr->requests.Pop(index);
            return true;
        }
        else {
            ASSERT_MSG(0, "Request '%s' not found", filepath);
            return false;
        }
    };
    
    if (!error) {
        char filepath[PATH_CHARS_MAX];
        incomingData.ReadStringBinary(filepath, sizeof(filepath));
        
        VfsFileReadWriteRequest req;
        if (PopRequest(filepath, &req)) {
            Blob blob(req.alloc ? req.alloc : &gVfs.alloc);

            size_t fileSize = incomingData.Size() - incomingData.ReadOffset();
            blob.Reserve(fileSize);
            incomingData.Read(const_cast<void*>(blob.Data()), blob.Capacity());
            blob.SetSize(fileSize);

            if (req.callbacks.readFn)
                req.callbacks.readFn(filepath, blob, req.user);

            blob.Free();
        }
    }
    else {
        const char* filepath = errorDesc;

        VfsFileReadWriteRequest req;
        if (PopRequest(filepath, &req)) {
            ASSERT(req.callbacks.readFn);
            Blob blob(req.alloc ? req.alloc : &gVfs.alloc);  // empty blob
            req.callbacks.readFn(filepath, blob, req.user);
            blob.Free();
        }
    }
}

static void Vfs::_WriteFileHandlerClientFn([[maybe_unused]] uint32 cmd, const Blob& incomingData, void* userData, 
                                           bool error, const char* errorDesc)
{
    ASSERT(cmd == VFS_REMOTE_WRITE_FILE_CMD);
    UNUSED(userData);

    auto PopRequest = [](const char* filepath, VfsFileReadWriteRequest* pReq)->bool {
        VfsRemoteManager* mgr = &gVfs.remoteMgr;
        MutexScope mtx(mgr->requestsMtx);
        if (uint32 index = mgr->requests.FindIf([filepath](const VfsFileReadWriteRequest& req)->bool 
            { return req.path.IsEqual(filepath); }); index != UINT32_MAX)
        {
            *pReq = mgr->requests.Pop(index);
            return true;
        }
        else {
            ASSERT_MSG(0, "Request '%s' not found", filepath);
            return false;
        }
    };

    if (!error) {
        char filepath[PATH_CHARS_MAX];
        size_t bytesWritten;
        incomingData.ReadStringBinary(filepath, sizeof(filepath));
        incomingData.Read<size_t>(&bytesWritten);
        
        VfsFileReadWriteRequest req;
        if (PopRequest(filepath, &req)) {
            ASSERT(req.callbacks.writeFn);
            Blob emptyBlob;
            req.callbacks.writeFn(filepath, bytesWritten, emptyBlob, req.user);
        }
    }
    else {
        const char* filepath = errorDesc;

        VfsFileReadWriteRequest req;
        if (PopRequest(filepath, &req)) {
            ASSERT(req.callbacks.writeFn);
            Blob emptyBlob;
            req.callbacks.writeFn(filepath, 0, emptyBlob, req.user);
        }
    }
}

static void Vfs::_ReadFileInfoHandlerClientFn([[maybe_unused]] uint32 cmd, const Blob& incomingData, void* userData, 
                                              bool error, const char* errorDesc)
{
    ASSERT(cmd == VFS_REMOTE_READ_FILE_INFO_CMD);
    UNUSED(userData);

    auto PopRequest = [](const char* filepath, VfsFileReadWriteRequest* pReq)->bool {
        VfsRemoteManager* mgr = &gVfs.remoteMgr;
        MutexScope mtx(mgr->requestsMtx);
        if (uint32 index = mgr->requests.FindIf([filepath](const VfsFileReadWriteRequest& req)->bool 
            { return req.path.IsEqual(filepath); }); index != UINT32_MAX)
        {
            *pReq = mgr->requests.Pop(index);
            return true;
        }
        else {
            ASSERT_MSG(0, "Request '%s' not found", filepath);
            return false;
        }
    };

    PathInfo info {};
    if (!error) {
        char filepath[PATH_CHARS_MAX];
        incomingData.ReadStringBinary(filepath, sizeof(filepath));
        incomingData.Read<uint32>((uint32*)&info.type);
        incomingData.Read<uint64>(&info.size);
        incomingData.Read<uint64>(&info.lastModified);

        VfsFileReadWriteRequest req;
        if (PopRequest(filepath, &req)) {
            ASSERT(req.callbacks.infoFn);
            req.callbacks.infoFn(filepath, info, req.user);
        }
    }
    else {
        const char* filepath = errorDesc;

        VfsFileReadWriteRequest req;
        if (PopRequest(filepath, &req)) {
            ASSERT(req.callbacks.infoFn);
            req.callbacks.infoFn(filepath, info, req.user);
        }
    }
}

//    ██╗███╗   ██╗██╗████████╗ ██╗██████╗ ███████╗██╗███╗   ██╗██╗████████╗
//    ██║████╗  ██║██║╚══██╔══╝██╔╝██╔══██╗██╔════╝██║████╗  ██║██║╚══██╔══╝
//    ██║██╔██╗ ██║██║   ██║  ██╔╝ ██║  ██║█████╗  ██║██╔██╗ ██║██║   ██║   
//    ██║██║╚██╗██║██║   ██║ ██╔╝  ██║  ██║██╔══╝  ██║██║╚██╗██║██║   ██║   
//    ██║██║ ╚████║██║   ██║██╔╝   ██████╔╝███████╗██║██║ ╚████║██║   ██║   
//    ╚═╝╚═╝  ╚═══╝╚═╝   ╚═╝╚═╝    ╚═════╝ ╚══════╝╚═╝╚═╝  ╚═══╝╚═╝   ╚═╝   
bool Vfs::Initialize()
{
    Engine::HelperInitializeProxyAllocator(&gVfs.alloc, "VirtualFS", Mem::GetDefaultAlloc());
    Engine::RegisterProxyAllocator(&gVfs.alloc);

    gVfs.mounts.SetAllocator(&gVfs.alloc);

    // Async IO
    {
        VfsAsyncManager* mgr = &gVfs.asyncMgr;
        mgr->requests.SetAllocator(&gVfs.alloc);

        mgr->requestsMtx.Initialize();
        mgr->semaphore.Initialize();

        mgr->thread.Start(ThreadDesc {
            .entryFn = _AsyncWorkerThread, 
            .name = "VfsAsyncWorkerThread"
        });
    }

    // Remote IO
    {
        VfsRemoteManager* mgr = &gVfs.remoteMgr;
        mgr->requestsMtx.Initialize();
        mgr->requests.SetAllocator(&gVfs.alloc);
    }

    // Hot-reload
    {
        gVfs.fileChangesMtx.Initialize();
        gVfs.fileChanges.SetAllocator(&gVfs.alloc);
        gVfs.fileChangeCallbacks.SetAllocator(&gVfs.alloc);
        #if CONFIG_TOOLMODE
        dmon_init();
        #endif
    }

    Remote::RegisterCommand({
        .cmdFourCC = VFS_REMOTE_READ_FILE_CMD,
        .serverFn = _ReadFileHandlerServerFn,
        .clientFn = _ReadFileHandlerClientFn,
        .async = true 
    });

    Remote::RegisterCommand({
        .cmdFourCC = VFS_REMOTE_WRITE_FILE_CMD,
        .serverFn = _WriteFileHandlerServerFn,
        .clientFn = _WriteFileHandlerClientFn,
        .async = true
    });

    Remote::RegisterCommand({
        .cmdFourCC = VFS_REMOTE_READ_FILE_INFO_CMD,
        .serverFn = _ReadFileInfoHandlerServerFn,
        .clientFn = _ReadFileInfoHandlerClientFn,
    });

    Remote::RegisterCommand({
        .cmdFourCC = VFS_REMOTE_MONITOR_CHANGES_CMD,
        .serverFn = _MonitorChangesServerCallback,
        .clientFn = _MonitorChangesClientCallback
    });

    gVfs.initialized = true;

    if constexpr (PLATFORM_WINDOWS) {
        Path curDir;
        curDir.SetToCurrentDir();
        LOG_DEBUG("CWD: %s", curDir.CStr());
    }

    return true;
}

void Vfs::Release()
{
    gVfs.quit = true;

    // Async IO
    {
        VfsAsyncManager* mgr = &gVfs.asyncMgr;
        mgr->semaphore.Post();
        mgr->thread.Stop();
        mgr->requestsMtx.Release();
        mgr->semaphore.Release();
        mgr->requests.Free();
    }

    // Remote IO
    {
        VfsRemoteManager* mgr = &gVfs.remoteMgr;
        mgr->requestsMtx.Release();
        mgr->requests.Free();
    }

    // Hot-reload
    {
        #if CONFIG_TOOLMODE
        dmon_deinit();
        #endif
    
        gVfs.reqFileChangesThrd.Stop();
        gVfs.fileChangesMtx.Release();
        gVfs.fileChanges.Free();
        gVfs.fileChangeCallbacks.Free();
    }

    gVfs.mounts.Free();

    gVfs.initialized = false;
}

void Vfs::HelperMountDataAndShaders(bool remote, const char* dataDir)
{
    // Assume that we are in the root directory of the project with "data" and "code" folders under it
    if (remote) {
        Vfs::MountRemote(dataDir, true);
        Vfs::MountRemote("shaders", true);
    }
    else {        
        Vfs::MountLocal(dataDir, "data", true);
        Vfs::MountLocal("code/Shaders", "shaders", true);
    }
}
