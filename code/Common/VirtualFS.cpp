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

#if PLATFORM_ANDROID
    #include "Application.h"    // appGetNativeAssetManagerHandle
    #include <android/asset_manager.h>
#endif

#if CONFIG_TOOLMODE
    #define DMON_IMPL
    #define DMON_MALLOC(size) Mem::Alloc(size)
    #define DMON_FREE(ptr) Mem::Free(ptr)
    #define DMON_REALLOC(ptr, size) Mem::Realloc(ptr, size)
    #define DMON_LOG_ERROR(s) LOG_ERROR(s)
    #define DMON_LOG_DEBUG(s) LOG_DEBUG(s)
    
    #include <stdio.h>  // snprintf 
    PRAGMA_DIAGNOSTIC_PUSH()
    PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4505)
    #include "../External/dmon/dmon.h"
    PRAGMA_DIAGNOSTIC_POP()
#endif


//     ██████╗ ██╗      ██████╗ ██████╗  █████╗ ██╗     ███████╗
//    ██╔════╝ ██║     ██╔═══██╗██╔══██╗██╔══██╗██║     ██╔════╝
//    ██║  ███╗██║     ██║   ██║██████╔╝███████║██║     ███████╗
//    ██║   ██║██║     ██║   ██║██╔══██╗██╔══██║██║     ╚════██║
//    ╚██████╔╝███████╗╚██████╔╝██████╔╝██║  ██║███████╗███████║
//     ╚═════╝ ╚══════╝ ╚═════╝ ╚═════╝ ╚═╝  ╚═╝╚══════╝╚══════╝
                                                              
static constexpr uint32 VFS_REMOTE_READ_FILE_CMD = MakeFourCC('F', 'R', 'D', '0');
static constexpr uint32 VFS_REMOTE_WRITE_FILE_CMD = MakeFourCC('F', 'W', 'T', '0');
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
    Write
};

struct VfsFileChangeEvent
{
    Path filepath;
};

struct VfsRequest
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
    } callbacks;
};

struct VfsAsyncManager
{
    Thread thread;
    Array<VfsRequest> requests;
    uint8 _padding[40];
    Mutex requestsMtx;
    Semaphore semaphore;
};

struct VfsRemoteManager
{
    Mutex requestsMtx;
    Array<VfsRequest> requests;
};

struct VfsManager
{
    MemAllocator* alloc;
    Array<VfsMountPoint> mounts;
    uint8 _padding1[32];

    VfsAsyncManager asyncMgr;
    VfsRemoteManager remoteMgr;
    Array<VfsFileChangeEvent> fileChanges;
    Array<VfsFileChangeCallback> fileChangeCallbacks;
    uint8 _padding2[16];
    Thread reqFileChangesThrd;
    Mutex fileChangesMtx;
    bool quit;
    bool initialized;
};

static VfsManager gVfs;

//----------------------------------------------------------------------------------------------------------------------
// @fwd
#if CONFIG_TOOLMODE
static void vfsDmonFn(dmon_watch_id watchId, dmon_action action, const char* rootDir, const char* filepath, const char*, void*);
#endif


//    ███╗   ███╗ ██████╗ ██╗   ██╗███╗   ██╗████████╗███████╗
//    ████╗ ████║██╔═══██╗██║   ██║████╗  ██║╚══██╔══╝██╔════╝
//    ██╔████╔██║██║   ██║██║   ██║██╔██╗ ██║   ██║   ███████╗
//    ██║╚██╔╝██║██║   ██║██║   ██║██║╚██╗██║   ██║   ╚════██║
//    ██║ ╚═╝ ██║╚██████╔╝╚██████╔╝██║ ╚████║   ██║   ███████║
//    ╚═╝     ╚═╝ ╚═════╝  ╚═════╝ ╚═╝  ╚═══╝   ╚═╝   ╚══════╝
bool Vfs::MountLocal(const char* rootDir, const char* alias, [[maybe_unused]] bool watch)
{
    if (Path::Stat_CStr(rootDir).type != PathType::Directory) {
        LOG_ERROR("VirtualFS: RootDir '%s' is not a valid directory", rootDir);
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
        mount.watchId = dmon_watch(rootDir, vfsDmonFn, DMON_WATCHFLAGS_RECURSIVE, nullptr).id;
    #endif

    gVfs.mounts.Push(mount);
    LOG_INFO("Mounted local path '%s' to alias '%s'", mount.path.CStr(), mount.alias.CStr());
    return true;
}

bool Vfs::MountRemote(const char* alias, bool watch)
{
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

    if (watch) {
        mount.watchId = 1;

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
                .name = "VfsRequestFileChanges",
                .stackSize = 64*SIZE_KB
            });
            gVfs.reqFileChangesThrd.SetPriority(ThreadPriority::Idle);
        }
    }

    gVfs.mounts.Push(mount);
    LOG_INFO("Mounted '%s' on remote service '%s'", alias, url);
    return true;
}

static uint32 vfsFindMount(const char* path)
{
    if (path[0] == '/') 
        path = path + 1;
    return gVfs.mounts.FindIf([path](const VfsMountPoint& mount)->bool 
    {
        return strIsEqualCount(path, mount.alias.CStr(), mount.alias.Length()) && path[mount.alias.Length()] == '/';
    });
}

VfsMountType Vfs::GetMountType(const char* path)
{
    uint32 idx = vfsFindMount(path);   
    if (idx != UINT32_MAX)
        return gVfs.mounts[idx].type;
    else
        return VfsMountType::None;
}

bool Vfs::StripMountPath(char* outPath, uint32 outPathSize, const char* path)
{
    uint32 index = vfsFindMount(path);
    if (index != UINT32_MAX) {
        if (path[0] == '/')
            ++path;
        const char* stripped = path + gVfs.mounts[index].alias.Length();
        strCopy(outPath, outPathSize, stripped);
        return true;
    }
    else {
        strCopy(outPath, outPathSize, path);
        return false;
    }
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
static uint32 vfsDiskResolvePath(char* dstPath, uint32 dstPathSize, const char* path, VfsFlags flags)
{
    if ((flags & VfsFlags::AbsolutePath) == VfsFlags::AbsolutePath) {
        return UINT32_MAX;
    }
    else {
        // search in mount points
        if (path[0] == '/') 
            path = path + 1;

        uint32 idx = gVfs.mounts.FindIf([path](const VfsMountPoint& mount)->bool { 
            return strIsEqualCount(path, mount.alias.CStr(), mount.alias.Length()) && path[mount.alias.Length()] == '/';
        });
        if (idx != UINT32_MAX) {
            char tmpPath[PATH_CHARS_MAX];
            strCopy(tmpPath, sizeof(tmpPath), path + gVfs.mounts[idx].alias.Length());
            Path::JoinUnixStyle_CStr(dstPath, dstPathSize, gVfs.mounts[idx].path.CStr(), tmpPath);
            return idx;
        }
        else {
            return UINT32_MAX;
        }
    }
}

static Blob vfsDiskReadFile(const char* path, VfsFlags flags, MemAllocator* alloc, Path* outResolvedPath = nullptr)
{
    PROFILE_ZONE_WITH_TEXT(path, strLen(path), true);

    auto LoadFromDisk = [](const char* path, VfsFlags flags, MemAllocator* alloc)->Blob {
        File f;
        if (f.Open(path, FileOpenFlags::Read | FileOpenFlags::SeqScan)) {
            Blob blob(alloc ? alloc : gVfs.alloc);
            
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
            return blob;
        }
        else {
            return Blob {};
        }
    };

    ASSERT_MSG(Vfs::GetMountType(path) != VfsMountType::Remote, "Remote mounts cannot read files in blocking mode");

    char resolvedPath[PATH_CHARS_MAX];
    if (vfsDiskResolvePath(resolvedPath, sizeof(resolvedPath), path, flags) != UINT32_MAX) {
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

static size_t vfsDiskWriteFile(const char* path, VfsFlags flags, const Blob& blob)
{
    PROFILE_ZONE_WITH_TEXT(path, strLen(path), true);

    auto SaveToDisk = [](const char* path, VfsFlags, const Blob& blob)->size_t 
    {
        File f;
        char tempPath[PATH_CHARS_MAX];
        char tempName[PATH_CHARS_MAX];
        Path::GetFilename_CStr(path, tempName, sizeof(tempName));

        #if PLATFORM_WINDOWS
        // On windows, we better use the same path of the destination file
        // Because if it's a different volume, then it does a copy/delete instead
        char tempDir[PATH_CHARS_MAX];
        Path::GetDirectory_CStr(path, tempDir, sizeof(tempDir));
        #else
        char* tempDir = nullptr;    // use /tmp on posix
        #endif
        
        bool makeTempSuccess = Path::MakeTemp_CStr(tempPath, sizeof(tempPath), tempName, tempDir);
        if (!makeTempSuccess)
            LOG_WARNING("Making temp file failed: %s", path);

        if (f.Open(makeTempSuccess ? tempPath : path, FileOpenFlags::Write)) {
            size_t bytesWritten = f.Write(blob.Data(), blob.Size());
            f.Close();
            
            if (bytesWritten && makeTempSuccess)
                return Path::Move_CStr(tempPath, path);
            else
                return bytesWritten;
        }

        return 0;
    };

    auto CheckAndCreateDirsRecursive = [](const char* resolvedPath, const char* mountRootDir) {
        Path dirname = Path(resolvedPath).GetDirectory_CStr();
        if (!dirname.IsDir()) {
            uint32 mountRootDirLen = mountRootDir ? strLen(mountRootDir) : 0;
            uint32 slashIdx = mountRootDirLen;
            while ((slashIdx = dirname.FindChar('/', slashIdx + 1)) != UINT32_MAX) {
                Path subDir(dirname.SubStr(0, slashIdx));
                if (!subDir.IsDir()) {
                    [[maybe_unused]] bool r = Path::CreateDir_CStr(subDir.CStr());
                    ASSERT(r);
                }
            }
            if (!dirname.IsDir()) {
                [[maybe_unused]] bool r = Path::CreateDir_CStr(dirname.CStr());
                ASSERT(r);
            }
        }
    };

    ASSERT_MSG(Vfs::GetMountType(path) != VfsMountType::Remote, "Remote mounts cannot read files in blocking mode");
    char resolvedPath[PATH_CHARS_MAX];
    if (uint32 mountIdx = vfsDiskResolvePath(resolvedPath, sizeof(resolvedPath), path, flags); mountIdx != UINT32_MAX) {
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
static Blob vfsPackageBundleReadFile(const char* path, VfsFlags flags, MemAllocator* alloc)
{
    auto LoadFromAssetManager = [](const char* path, VfsFlags flags, MemAllocator* alloc)->Blob {
        AAsset* asset = AAssetManager_open(App::AndroidGetAssetManager(), path, AASSET_MODE_BUFFER);
        if (!asset)
            return Blob {};
        Blob blob(alloc ? alloc : gVfs.alloc);
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
                return blob;        // Finally, asset read successfully
            } 
            else {
                blob.Free();
            }
        }
        return Blob {};
    };


    ASSERT_MSG((flags & VfsFlags::AbsolutePath) != VfsFlags::AbsolutePath, "Absoluate paths doesn't work on PackageBundle mounts");
    // search in mount points
    if (path[0] == '/') 
        path = path + 1;

    uint32 idx = gVfs.mounts.FindIf([path](const VfsMountPoint& mount)->bool {
        return strIsEqualCount(path, mount.alias.CStr(), mount.alias.Length()) &&
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

    uint32 idx = vfsFindMount(path);
    if (idx != UINT32_MAX) {
        VfsMountType type = gVfs.mounts[idx].type;
        ASSERT_MSG(type != VfsMountType::Remote, "Remote file requests cannot be done in blocking mode, call vfsReadFileAsync");
        if (type == VfsMountType::Local)
            return vfsDiskReadFile(path, flags, alloc, outResolvedPath);
        #if PLATFORM_MOBILE
        else if (type == VfsMountType::PackageBundle)
            return vfsPackageBundleReadFile(path, flags, alloc);
        #endif
        else
            return Blob{};
    }
    else {
        #if PLATFORM_ANDROID
        // take a guess, and see if we have 'assets' at the start, 
        const char* normPath = path;
        if (normPath[0] == '/')
            normPath++;
        const char predefinedAssets[] = "assets/";
        if (strIsEqualNoCaseCount(normPath, predefinedAssets, sizeof(predefinedAssets)-1))
            return vfsPackageBundleReadFile(normPath + sizeof(predefinedAssets) - 1, flags, alloc);
        #endif // PALTFORM_ANDROID

        if (outResolvedPath)
            *outResolvedPath = path;
        return vfsDiskReadFile(path, flags, alloc);
    }
}

size_t Vfs::WriteFile(const char* path, const Blob& blob, VfsFlags flags)
{
    uint32 idx = vfsFindMount(path);
    if (idx != UINT32_MAX) {
        [[maybe_unused]] VfsMountType type = gVfs.mounts[idx].type;
        ASSERT_MSG(type != VfsMountType::Remote, "Remote file requests cannot be done in blocking mode, call vfsWriteFileAsync");
        ASSERT_MSG(type != VfsMountType::PackageBundle, "Cannot write to PackageBundle mounts");
        ASSERT(type == VfsMountType::Local);
        return vfsDiskWriteFile(path, flags, blob);
    }
    else {
        return vfsDiskWriteFile(path, flags, blob);
    }
}

uint64 Vfs::GetLastModified(const char* path)
{
    ASSERT_MSG(Vfs::GetMountType(path) != VfsMountType::Remote, "Remote mounts cannot read files in blocking mode");

    char resolvedPath[PATH_CHARS_MAX];
    if (vfsDiskResolvePath(resolvedPath, sizeof(resolvedPath), path, VfsFlags::None) != UINT32_MAX)
    return Path::Stat_CStr(resolvedPath).lastModified;
    else
    return Path::Stat_CStr(resolvedPath).lastModified;
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
static void vfsDmonFn(dmon_watch_id watchId, dmon_action action, const char* rootDir, const char* filepath, const char*, void*)
{
    switch (action) {
    case DMON_ACTION_MODIFY: {
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
                        gVfs.fileChanges.Push(VfsFileChangeEvent { .filepath = aliasFilepath });
                    }
                    break;
                } // if mount.watchId == watchId
            }
        }
    } break;
    default: break;
    }
}
#endif // CONFIG_TOOLMODE

static void vfsMonitorChangesClientFn([[maybe_unused]] uint32 cmd, const Blob& incomingData, void*, bool error, const char* errorDesc)
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

            uint32 idx = gVfs.mounts.FindIf([path](const VfsMountPoint& mount)->bool 
                { return strIsEqualCount(path, mount.alias.CStr(), mount.alias.Length()) && 
                     path[mount.alias.Length()] == '/';
                });
            if (idx != UINT32_MAX && gVfs.mounts[idx].type == VfsMountType::Remote && gVfs.mounts[idx].watchId) {
                for (uint32 k = 0; k < gVfs.fileChangeCallbacks.Count(); k++ ) {
                    VfsFileChangeCallback callback = gVfs.fileChangeCallbacks[k];
                    callback(filepath);
                }
            }
        } // foreach (change-event)
    }
}

static bool vfsMonitorChangesServerFn([[maybe_unused]] uint32 cmd, const Blob& incomingData, Blob* outgoingData, 
                                      void*, char outgoingErrorDesc[kRemoteErrorDescSize])
{
    ASSERT(cmd == VFS_REMOTE_MONITOR_CHANGES_CMD);
    UNUSED(outgoingErrorDesc);
    UNUSED(incomingData);
        
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
static int vfsAsyncWorkerThread(void*)
{
    VfsAsyncManager* mgr = &gVfs.asyncMgr;

    while (!gVfs.quit) {
        // Read requests
        VfsRequest req;
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
            case VfsCommand::Read: {
                Blob blob;
                if (req.mountType == VfsMountType::Local)
                    blob = vfsDiskReadFile(req.path.CStr(), req.flags, req.alloc);
                #if PLATFORM_MOBILE
                else if (req.mountType == VfsMountType::PackageBundle)
                    blob = vfsPackageBundleReadFile(req.path.CStr(), req.flags, req.alloc);
                #endif

                ASSERT(req.callbacks.readFn);
                req.callbacks.readFn(req.path.CStr(), blob, req.user);
                blob.Free();
                break;
            }
            case VfsCommand::Write: {
                ASSERT_MSG(req.mountType == VfsMountType::Local, "Write only supports local mounts");
                ASSERT(req.callbacks.writeFn);
                size_t bytesWritten = vfsDiskWriteFile(req.path.CStr(), req.flags, req.blob);
                req.callbacks.writeFn(req.path.CStr(), bytesWritten, req.blob, req.user);
                req.blob.Free();
                break; 
            }
            }
        }

        mgr->semaphore.Wait();
    }

    return 0;
}

void Vfs::ReadFileAsync(const char* path, VfsFlags flags, VfsReadAsyncCallback readResultFn, void* user, MemAllocator* alloc)
{
    ASSERT(gVfs.initialized);

    VfsRequest req {
        .cmd = VfsCommand::Read,
        .flags = flags,
        .path = path,
        .alloc = alloc,
        .user = user,
        .callbacks = { .readFn = readResultFn }
    };

    uint32 idx = vfsFindMount(path);
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

            paramsBlob.WriteStringBinary(path, strLen(path));

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

    VfsRequest req {
        .cmd = VfsCommand::Write,
        .flags = flags,
        .path = path,
        .user = user,
        .callbacks = { .writeFn = writeResultFn }
    };

    uint32 idx = vfsFindMount(path);
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

            paramsBlob.WriteStringBinary(path, strLen(path));
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
        MutexScope mtx(diskMgr->requestsMtx);
        req.blob.SetAllocator(gVfs.alloc);

        blob.CopyTo(&req.blob);
        diskMgr->requests.Push(req);
        diskMgr->semaphore.Post();
    }
}


//    ██████╗ ███████╗███╗   ███╗ ██████╗ ████████╗███████╗    ██╗ ██████╗ 
//    ██╔══██╗██╔════╝████╗ ████║██╔═══██╗╚══██╔══╝██╔════╝    ██║██╔═══██╗
//    ██████╔╝█████╗  ██╔████╔██║██║   ██║   ██║   █████╗      ██║██║   ██║
//    ██╔══██╗██╔══╝  ██║╚██╔╝██║██║   ██║   ██║   ██╔══╝      ██║██║   ██║
//    ██║  ██║███████╗██║ ╚═╝ ██║╚██████╔╝   ██║   ███████╗    ██║╚██████╔╝
//    ╚═╝  ╚═╝╚══════╝╚═╝     ╚═╝ ╚═════╝    ╚═╝   ╚══════╝    ╚═╝ ╚═════╝ 
static void vfsRemoteReadFileComplete(const char* path, const Blob& blob, void*)
{
    bool error = !blob.IsValid();
    char errorDesc[kRemoteErrorDescSize];   errorDesc[0] = '\0';
    if (error)
        strCopy(errorDesc, sizeof(errorDesc), path);


    if (!error) {
        MemTempAllocator tmpAlloc;
        Blob responseBlob;
        responseBlob.SetAllocator(&tmpAlloc);      // TODO: use temp allocator
        responseBlob.SetGrowPolicy(Blob::GrowPolicy::Multiply);
        responseBlob.WriteStringBinary(path, strLen(path));
        responseBlob.Write(blob.Data(), blob.Size());
        Remote::SendResponse(VFS_REMOTE_READ_FILE_CMD, responseBlob, error, errorDesc);
        responseBlob.Free();
    }
    else {
        Remote::SendResponse(VFS_REMOTE_READ_FILE_CMD, blob, error, errorDesc); 
    }
}

static void vfsRemoteWriteFileComplete(const char* path, size_t bytesWritten, const Blob&, void*)
{
    bool error = bytesWritten == 0;
    char errorDesc[kRemoteErrorDescSize];   errorDesc[0] = '\0';
    if (error)
        strCopy(errorDesc, sizeof(errorDesc), path);

    if (!error) {
        MemTempAllocator tmpAlloc;
        Blob responseBlob;
        responseBlob.SetAllocator(&tmpAlloc);      // TODO: use temp allocator
        responseBlob.SetGrowPolicy(Blob::GrowPolicy::Multiply);
        responseBlob.Write(path, strLen(path));
        responseBlob.Write<size_t>(bytesWritten);

        Remote::SendResponse(VFS_REMOTE_WRITE_FILE_CMD, responseBlob, error, errorDesc);
        responseBlob.Free();
    }
    else {
        Remote::SendResponse(VFS_REMOTE_WRITE_FILE_CMD, Blob(), error, errorDesc); 
    }
}

static bool vfsReadFileHandlerServerFn(uint32 cmd, const Blob& incomingData, Blob* outgoingData, 
                                     void*, char outgoingErrorDesc[kRemoteErrorDescSize])
{
    ASSERT(cmd == VFS_REMOTE_READ_FILE_CMD);
    UNUSED(cmd);
    UNUSED(outgoingData);
    UNUSED(outgoingErrorDesc);

    char filepath[PATH_CHARS_MAX]; 
    incomingData.ReadStringBinary(filepath, sizeof(filepath));

    // The async process finished when the program returns into the callback `vfsRemoteReadFileComplete`
    Vfs::ReadFileAsync(filepath, VfsFlags::None, vfsRemoteReadFileComplete, nullptr, gVfs.alloc);
    
    return true;
}

static bool vfsWriteFileHandlerServerFn(uint32 cmd, const Blob& incomingData, Blob* outgoingData, 
                                        void*, char outgoingErrorDesc[kRemoteErrorDescSize])
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
        Vfs::WriteFileAsync(filepath, blob, flags, vfsRemoteWriteFileComplete, nullptr);
        return true;
    }
    else {
        return false;
    }
}

static void vfsReadFileHandlerClientFn([[maybe_unused]] uint32 cmd, const Blob& incomingData, void* userData, bool error, 
                                       const char* errorDesc)
{
    ASSERT(cmd == VFS_REMOTE_READ_FILE_CMD);
    UNUSED(userData);

    auto PopRequest = [](const char* filepath, VfsRequest* pReq)->bool {
        VfsRemoteManager* mgr = &gVfs.remoteMgr;
        MutexScope mtx(mgr->requestsMtx);
        if (uint32 index = mgr->requests.FindIf([filepath](const VfsRequest& req)->bool 
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
        
        VfsRequest req;
        if (PopRequest(filepath, &req)) {
            Blob blob(req.alloc ? req.alloc : gVfs.alloc);

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

        VfsRequest req;
        if (PopRequest(filepath, &req)) {
            ASSERT(req.callbacks.readFn);
            Blob blob(req.alloc ? req.alloc : gVfs.alloc);  // empty blob
            req.callbacks.readFn(filepath, blob, req.user);
            blob.Free();
        }
    }
}

static void vfsWriteFileHandlerClientFn([[maybe_unused]] uint32 cmd, const Blob& incomingData, void* userData, bool error, 
                                        const char* errorDesc)
{
    ASSERT(cmd == VFS_REMOTE_WRITE_FILE_CMD);
    UNUSED(userData);

    auto PopRequest = [](const char* filepath, VfsRequest* pReq)->bool {
        VfsRemoteManager* mgr = &gVfs.remoteMgr;
        MutexScope mtx(mgr->requestsMtx);
        if (uint32 index = mgr->requests.FindIf([filepath](const VfsRequest& req)->bool 
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
        
        VfsRequest req;
        if (PopRequest(filepath, &req)) {
            ASSERT(req.callbacks.writeFn);
            req.callbacks.writeFn(filepath, bytesWritten, Blob(), req.user);
        }
    }
    else {
        const char* filepath = errorDesc;

        VfsRequest req;
        if (PopRequest(filepath, &req)) {
            ASSERT(req.callbacks.writeFn);
            req.callbacks.writeFn(filepath, 0, Blob(), req.user);
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
    gVfs.alloc = Mem::GetDefaultAlloc();
    gVfs.mounts.SetAllocator(gVfs.alloc);

    // Async IO
    {
        VfsAsyncManager* mgr = &gVfs.asyncMgr;
        mgr->requests.SetAllocator(gVfs.alloc);

        mgr->requestsMtx.Initialize();
        mgr->semaphore.Initialize();

        mgr->thread.Start(ThreadDesc {
            .entryFn = vfsAsyncWorkerThread, 
            .name = "VfsAsyncWorkerThread"
        });
        mgr->thread.SetPriority(ThreadPriority::Low);
    }

    // Remote IO
    {
        VfsRemoteManager* mgr = &gVfs.remoteMgr;
        mgr->requestsMtx.Initialize();
        mgr->requests.SetAllocator(gVfs.alloc);
    }

    // Hot-reload
    {
        gVfs.fileChangesMtx.Initialize();
        gVfs.fileChanges.SetAllocator(gVfs.alloc);
        gVfs.fileChangeCallbacks.SetAllocator(gVfs.alloc);
        #if CONFIG_TOOLMODE
        dmon_init();
        #endif
    }

    Remote::RegisterCommand(RemoteCommandDesc {
        .cmdFourCC = VFS_REMOTE_READ_FILE_CMD,
        .serverFn = vfsReadFileHandlerServerFn,
        .clientFn = vfsReadFileHandlerClientFn,
        .async = true 
    });

    Remote::RegisterCommand(RemoteCommandDesc {
        .cmdFourCC = VFS_REMOTE_WRITE_FILE_CMD,
        .serverFn = vfsWriteFileHandlerServerFn,
        .clientFn = vfsWriteFileHandlerClientFn,
        .async = true
    });

    Remote::RegisterCommand(RemoteCommandDesc {
        .cmdFourCC = VFS_REMOTE_MONITOR_CHANGES_CMD,
        .serverFn = vfsMonitorChangesServerFn,
        .clientFn = vfsMonitorChangesClientFn
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


