#include "VirtualFS.h"

#include "Core/Log.h"
#include "Core/FileIO.h"
#include "Core/System.h"
#include "Core/String.h"
#include "Core/Settings.h"
#include "Core/TracyHelper.h"
#include "Core/Buffers.h"

#if PLATFORM_ANDROID
    #include "Application.h"    // appGetNativeAssetManagerHandle
    #include <android/asset_manager.h>
#endif

#if CONFIG_TOOLMODE
    #define DMON_IMPL
    #define DMON_MALLOC(size) memAlloc(size)
    #define DMON_FREE(ptr) memFree(ptr)
    #define DMON_REALLOC(ptr, size) memRealloc(ptr, size)
    #define DMON_LOG_ERROR(s) logError(s)
    #define DMON_LOG_DEBUG(s) logDebug(s)
    
    #include <stdio.h>  // snprintf 
    PRAGMA_DIAGNOSTIC_PUSH()
    PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4505)
    #include "External/dmon/dmon.h"
    PRAGMA_DIAGNOSTIC_POP()
#endif

#include "RemoteServices.h"

static constexpr uint32 kRemoteCmdReadFile = MakeFourCC('F', 'R', 'D', '0');
static constexpr uint32 kRemoteCmdWriteFile = MakeFourCC('F', 'W', 'T', '0');
static constexpr uint32 kRemoteCmdMonitorChanges = MakeFourCC('D', 'M', 'O', 'N');
static constexpr uint32 kRequestFileChangesIntervalMs = 1000;

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
    Allocator* alloc;
    void* user;
    union Callbacks 
    {
        VfsReadAsyncCallback readFn;
        VfsWriteAsyncCallback writeFn;
    } callbacks;
};

struct VfsLocalDiskManager
{
    Thread thread;
    Mutex requestsMtx;
    Array<VfsRequest> requests;
    Semaphore semaphore;
};

struct vfsRemoteDiskManager
{
    Mutex requestsMtx;
    Array<VfsRequest> requests;
};

struct VfsManager
{
    Allocator* alloc;
    Array<VfsMountPoint> mounts;
    VfsLocalDiskManager diskMgr;
    vfsRemoteDiskManager remoteMgr;
    Array<VfsFileChangeEvent> fileChanges;
    Mutex fileChangesMtx;
    Array<vfsFileChangeCallback> fileChangeFns;
    Thread reqFileChangesThrd;
    bool quit;
    bool initialized;
};

static VfsManager gVfs;

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
            char tmpPath[kMaxPath];
            strCopy(tmpPath, sizeof(tmpPath), path + gVfs.mounts[idx].alias.Length());
            pathJoinUnixStyle(dstPath, dstPathSize, gVfs.mounts[idx].path.CStr(), tmpPath);
            return idx;
        }
        else {
            return UINT32_MAX;
        }
    }
}

static Blob vfsDiskReadFile(const char* path, VfsFlags flags, Allocator* alloc)
{
    PROFILE_ZONE_WITH_TEXT(path, strLen(path), true);

    auto LoadFromDisk = [](const char* path, VfsFlags flags, Allocator* alloc)->Blob {
        File f;
        if (f.Open(path, FileIOFlags::Read | FileIOFlags::SeqScan)) {
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

    ASSERT_MSG(vfsGetMountType(path) != VfsMountType::Remote, "Remote mounts cannot read files in blocking mode");

    char resolvedPath[kMaxPath];
    if (vfsDiskResolvePath(resolvedPath, sizeof(resolvedPath), path, flags) != UINT32_MAX) {
        return LoadFromDisk(resolvedPath, flags, alloc);
    }
    else {
        return LoadFromDisk(path, flags, alloc);
    }
}

static size_t vfsDiskWriteFile(const char* path, VfsFlags flags, const Blob& blob)
{
    PROFILE_ZONE_WITH_TEXT(path, strLen(path), true);

    auto SaveToDisk = [](const char* path, VfsFlags flags, const Blob& blob)->size_t 
    {
        File f;
        if (f.Open(path, FileIOFlags::Write)) {

            if ((flags & VfsFlags::TextFile) == VfsFlags::TextFile)
                const_cast<Blob&>(blob).Write<char>('\0');

            size_t bytesWritten = f.Write(blob.Data(), blob.Size());

            f.Close();
            return bytesWritten;
        }

        return 0;
    };

    auto CheckAndCreateDirsRecursive = [](const char* resolvedPath, const char* mountRootDir) {
        Path dirname = Path(resolvedPath).GetDirectory();
        if (!dirname.IsDir()) {
            uint32 mountRootDirLen = mountRootDir ? strLen(mountRootDir) : 0;
            uint32 slashIdx = mountRootDirLen;
            while ((slashIdx = dirname.FindChar('/', slashIdx + 1)) != UINT32_MAX) {
                Path subDir(dirname.SubStr(0, slashIdx));
                if (!subDir.IsDir()) {
                    [[maybe_unused]] bool r = pathCreateDir(subDir.CStr());
                    ASSERT(r);
                }
            }
            if (!dirname.IsDir()) {
                [[maybe_unused]] bool r = pathCreateDir(dirname.CStr());
                ASSERT(r);
            }
        }
    };

    ASSERT_MSG(vfsGetMountType(path) != VfsMountType::Remote, "Remote mounts cannot read files in blocking mode");
    char resolvedPath[kMaxPath];
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
static Blob vfsPackageBundleReadFile(const char* path, VfsFlags flags, Allocator* alloc)
{
    auto LoadFromAssetManager = [](const char* path, VfsFlags flags, Allocator* alloc)->Blob {
        AAsset* asset = AAssetManager_open(appAndroidGetAssetManager(), path, AASSET_MODE_BUFFER);
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

// Thread
static int vfsLocalDiskManagerThreadFn(void*)
{
    VfsLocalDiskManager* mgr = &gVfs.diskMgr;

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


#if CONFIG_TOOLMODE
static void vfsDmonFn(dmon_watch_id watchId, dmon_action action, const char* rootDir,
    const char* filepath, const char*, void*)
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
                        for (uint32 k = 0; k < gVfs.fileChangeFns.Count(); k++) {
                            vfsFileChangeCallback callback = gVfs.fileChangeFns[k];
                            callback(aliasFilepath.CStr());
                        }
                    }
                    
                    if (settingsGetTooling().enableServer) {
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

bool vfsMountLocal(const char* rootDir, const char* alias, [[maybe_unused]] bool watch)
{
    if (pathStat(rootDir).type != PathType::Directory) {
        logError("VirtualFS: RootDir '%s' is not a valid directory", rootDir);
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
        logError("VirtualFS: Mount point with RootDir '%s' already added", mount.path.CStr());
        return false;
    }

    #if CONFIG_TOOLMODE
        if (watch) {
            mount.watchId = dmon_watch(rootDir, vfsDmonFn, DMON_WATCHFLAGS_RECURSIVE, nullptr).id;
        }
    #endif

    gVfs.mounts.Push(mount);
    logInfo("Mounted local path '%s' to alias '%s'", mount.path.CStr(), mount.alias.CStr());
    return true;
}

int vfsReqFileChangesThreadFn(void*)
{
    while (!gVfs.quit) {
        remoteExecuteCommand(kRemoteCmdMonitorChanges, Blob());
        threadSleep(kRequestFileChangesIntervalMs);
    }
    return 0;
}

bool vfsMountRemote(const char* alias, bool watch)
{
    ASSERT_MSG(settingsGetEngine().connectToServer, "Remote services is not enabled in settings");
    const char* url = settingsGetEngine().remoteServicesUrl.CStr();
    
    VfsMountPoint mount {
        .type = VfsMountType::Remote,
        .path = url,
        .alias = alias
    };

    if (gVfs.mounts.FindIf([alias](const VfsMountPoint& m)->bool 
        { return m.type == VfsMountType::Remote && m.alias.IsEqual(alias); }) != UINT32_MAX)
    {
        logError("VirtualFS: Remote mount point with alias '%s' already added", url);
        return false;
    }

    if (watch) {
        mount.watchId = 1;
        // TODO: maybe create a coroutine instead of a thread here ?
        if (!gVfs.reqFileChangesThrd.IsRunning()) {
            gVfs.reqFileChangesThrd.Start(ThreadDesc {
                .entryFn = vfsReqFileChangesThreadFn, 
                .name = "VfsRequestFileChanges",
                .stackSize = 64*kKB
            });
            gVfs.reqFileChangesThrd.SetPriority(ThreadPriority::Idle);
        }
    }

    gVfs.mounts.Push(mount);
    logInfo("Mounted '%s' on remote service '%s'", alias, url);
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

VfsMountType vfsGetMountType(const char* path)
{
    uint32 idx = vfsFindMount(path);   
    if (idx != UINT32_MAX)
        return gVfs.mounts[idx].type;
    else
        return VfsMountType::None;
}

void vfsReadFileAsync(const char* path, VfsFlags flags, VfsReadAsyncCallback readResultFn, void* user, Allocator* alloc)
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
        if (remoteIsConnected()) {
            req.mountType = VfsMountType::Remote;

            {
                vfsRemoteDiskManager* mgr = &gVfs.remoteMgr;
                MutexScope mtx(mgr->requestsMtx);
                mgr->requests.Push(req);
            }

            MemTempAllocator tmpAlloc;
            Blob paramsBlob;
            paramsBlob.SetAllocator(&tmpAlloc);
            paramsBlob.SetGrowPolicy(Blob::GrowPolicy::Multiply);

            paramsBlob.WriteStringBinary(path, strLen(path));

            remoteExecuteCommand(kRemoteCmdReadFile, paramsBlob);
            paramsBlob.Free();
        }
        else {
            logWarning("Mount point '%s' connection has lost, file '%s' cannot be loaded", gVfs.mounts[idx].path.CStr(), path);
        }
    }
    else {
        req.mountType = idx != UINT32_MAX ? gVfs.mounts[idx].type : VfsMountType::Local;

        VfsLocalDiskManager* diskMgr = &gVfs.diskMgr;
        MutexScope mtx(diskMgr->requestsMtx);
        diskMgr->requests.Push(req);
        diskMgr->semaphore.Post();
    }
}

void vfsWriteFileAsync(const char* path, const Blob& blob, VfsFlags flags, VfsWriteAsyncCallback writeResultFn, void* user)
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
        if (remoteIsConnected()) {
            req.mountType = VfsMountType::Remote;

            {
                vfsRemoteDiskManager* mgr = &gVfs.remoteMgr;
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

            remoteExecuteCommand(kRemoteCmdWriteFile, paramsBlob);
            paramsBlob.Free();
        }
        else {
            logWarning("Mount point '%s' connection has lost, file '%s' cannot be written", gVfs.mounts[idx].path.CStr(), path);
        }
    }
    else {
        req.mountType = idx != UINT32_MAX ? gVfs.mounts[idx].type : VfsMountType::Local;

        VfsLocalDiskManager* diskMgr = &gVfs.diskMgr;
        MutexScope mtx(diskMgr->requestsMtx);
        req.blob.SetAllocator(gVfs.alloc);
        blob.CopyTo(&req.blob);
        diskMgr->requests.Push(req);
        diskMgr->semaphore.Post();
    }
}

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
        remoteSendResponse(kRemoteCmdReadFile, responseBlob, error, errorDesc);
        responseBlob.Free();
    }
    else {
        remoteSendResponse(kRemoteCmdReadFile, blob, error, errorDesc); 
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

        remoteSendResponse(kRemoteCmdWriteFile, responseBlob, error, errorDesc);
        responseBlob.Free();
    }
    else {
        remoteSendResponse(kRemoteCmdWriteFile, Blob(), error, errorDesc); 
    }
}

static bool vfsInitializeDiskManager()
{
    VfsLocalDiskManager* mgr = &gVfs.diskMgr;

    mgr->requests.SetAllocator(gVfs.alloc);

    mgr->requestsMtx.Initialize();
    mgr->semaphore.Initialize();

    mgr->thread.Start(ThreadDesc {
        .entryFn = vfsLocalDiskManagerThreadFn, 
        .name = "VfsDiskManager"
    });
    mgr->thread.SetPriority(ThreadPriority::Low);

    return true;
}

static void vfsReleaseDiskManager()
{
    VfsLocalDiskManager* mgr = &gVfs.diskMgr;
        
    mgr->semaphore.Post();
    mgr->thread.Stop();
    mgr->requestsMtx.Release();
    mgr->semaphore.Release();
    mgr->requests.Free();
}

static bool vfsReadFileHandlerServerFn(uint32 cmd, const Blob& incomingData, Blob* outgoingData, 
                                     void*, char outgoingErrorDesc[kRemoteErrorDescSize])
{
    ASSERT(cmd == kRemoteCmdReadFile);
    UNUSED(cmd);
    UNUSED(outgoingData);
    UNUSED(outgoingErrorDesc);

    char filepath[kMaxPath]; 
    incomingData.ReadStringBinary(filepath, sizeof(filepath));

    // The async process finished when the program returns into the callback `vfsRemoteReadFileComplete`
    vfsReadFileAsync(filepath, VfsFlags::None, vfsRemoteReadFileComplete, nullptr, gVfs.alloc);
    
    return true;
}

static bool vfsWriteFileHandlerServerFn(uint32 cmd, const Blob& incomingData, Blob* outgoingData, 
                                        void*, char outgoingErrorDesc[kRemoteErrorDescSize])
{
    ASSERT(cmd == kRemoteCmdReadFile);
    UNUSED(cmd);
    UNUSED(outgoingData);
    UNUSED(outgoingErrorDesc);

    char filepath[kMaxPath];
    incomingData.ReadStringBinary(filepath, sizeof(filepath));
   
    VfsFlags flags;
    incomingData.Read<VfsFlags>(&flags);

    uint32 bufferSize = 0;
    incomingData.Read<uint32>(&bufferSize);

    if (bufferSize) {
        Blob blob;
        blob.Reserve((uint8*)incomingData.Data() + incomingData.ReadOffset(), bufferSize);
        // The async process finished when the program returns into the callback `vfsRemoteReadFileComplete`
        vfsWriteFileAsync(filepath, blob, flags, vfsRemoteWriteFileComplete, nullptr);
        return true;
    }
    else {
        return false;
    }
}

static void vfsReadFileHandlerClientFn([[maybe_unused]] uint32 cmd, const Blob& incomingData, void* userData, bool error, 
                                       const char* errorDesc)
{
    ASSERT(cmd == kRemoteCmdReadFile);
    UNUSED(userData);

    auto PopRequest = [](const char* filepath, VfsRequest* pReq)->bool {
        vfsRemoteDiskManager* mgr = &gVfs.remoteMgr;
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
        char filepath[kMaxPath];
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
    ASSERT(cmd == kRemoteCmdWriteFile);
    UNUSED(userData);

    auto PopRequest = [](const char* filepath, VfsRequest* pReq)->bool {
        vfsRemoteDiskManager* mgr = &gVfs.remoteMgr;
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
        char filepath[kMaxPath];
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

static bool vfsInitializeRemoteManager()
{
    vfsRemoteDiskManager* mgr = &gVfs.remoteMgr;
    mgr->requestsMtx.Initialize();
    mgr->requests.SetAllocator(gVfs.alloc);
    return true;
}

static void vfsReleaseRemoteManager()
{
    vfsRemoteDiskManager* mgr = &gVfs.remoteMgr;
    mgr->requestsMtx.Release();
    mgr->requests.Free();
}

static bool vfsMonitorChangesServerFn([[maybe_unused]] uint32 cmd, const Blob& incomingData, Blob* outgoingData, 
                                       void*, char outgoingErrorDesc[kRemoteErrorDescSize])
{
    ASSERT(cmd == kRemoteCmdMonitorChanges);
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

static void vfsMonitorChangesClientFn([[maybe_unused]] uint32 cmd, const Blob& incomingData, void*, bool error, 
                                    const char* errorDesc)
{
    ASSERT(cmd == kRemoteCmdMonitorChanges);
    UNUSED(error);
    UNUSED(errorDesc);
    
    uint32 numChanges = 0;
    incomingData.Read<uint32>(&numChanges);
    if (numChanges > 0) {
        for (uint32 i = 0; i < numChanges; i++) {
            char filepath[kMaxPath];
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
                for (uint32 k = 0; k < gVfs.fileChangeFns.Count(); k++ ) {
                    vfsFileChangeCallback callback = gVfs.fileChangeFns[k];
                    callback(filepath);
                }
            }
        } // foreach (change-event)
    }
}

bool _private::vfsInitialize()
{
    gVfs.alloc = memDefaultAlloc();
    gVfs.mounts.SetAllocator(gVfs.alloc);
    gVfs.fileChanges.SetAllocator(gVfs.alloc);
    gVfs.fileChangeFns.SetAllocator(gVfs.alloc);

    gVfs.fileChangesMtx.Initialize();

    if (!vfsInitializeDiskManager())
        return false;
    if (!vfsInitializeRemoteManager())
        return false;

    #if CONFIG_TOOLMODE
        dmon_init();
    #endif

    remoteRegisterCommand(RemoteCommandDesc {
        .cmdFourCC = kRemoteCmdReadFile,
        .serverFn = vfsReadFileHandlerServerFn,
        .clientFn = vfsReadFileHandlerClientFn,
        .async = true 
    });

    remoteRegisterCommand(RemoteCommandDesc {
        .cmdFourCC = kRemoteCmdWriteFile,
        .serverFn = vfsWriteFileHandlerServerFn,
        .clientFn = vfsWriteFileHandlerClientFn,
        .async = true
    });

    remoteRegisterCommand(RemoteCommandDesc {
        .cmdFourCC = kRemoteCmdMonitorChanges,
        .serverFn = vfsMonitorChangesServerFn,
        .clientFn = vfsMonitorChangesClientFn
    });

    gVfs.initialized = true;

    if constexpr (PLATFORM_WINDOWS) {
        Path curDir;
        curDir.SetToCurrentDir();
        logDebug("CWD: %s", curDir.CStr());
    }

    return true;
}

void _private::vfsRelease()
{
    gVfs.quit = true;
    vfsReleaseDiskManager();
    vfsReleaseRemoteManager();

    #if CONFIG_TOOLMODE
        dmon_deinit();
    #endif
    
    gVfs.reqFileChangesThrd.Stop();
    gVfs.fileChangesMtx.Release();

    gVfs.mounts.Free();
    gVfs.fileChanges.Free();
    gVfs.fileChangeFns.Free();

    gVfs.initialized = false;
}

uint64 vfsGetLastModified(const char* path)
{
    ASSERT_MSG(vfsGetMountType(path) != VfsMountType::Remote, "Remote mounts cannot read files in blocking mode");

    char resolvedPath[kMaxPath];
    if (vfsDiskResolvePath(resolvedPath, sizeof(resolvedPath), path, VfsFlags::None) != UINT32_MAX)
        return pathStat(resolvedPath).lastModified;
    else
        return pathStat(resolvedPath).lastModified;
}

bool vfsStripMountPath(char* outPath, uint32 outPathSize, const char* path)
{
    uint32 index = vfsFindMount(path);
    if (index != UINT32_MAX) {
        const char* stripped = path + gVfs.mounts[index].path.Length();
        strCopy(outPath, outPathSize, stripped);
        return true;
    }
    else {
        strCopy(outPath, outPathSize, path);
        return false;
    }
}

#if PLATFORM_MOBILE
bool vfsMountPackageBundle(const char* alias)
{
    VfsMountPoint mount {
        .type = VfsMountType::PackageBundle,
        .alias = alias
    };

    if (gVfs.mounts.FindIf([&mount](const VfsMountPoint& m)->bool 
        { return mount.alias == m.alias; }) != UINT32_MAX)
    {
        logError("VirtualFS: Mount point with alias '%s' already added", mount.alias.CStr());
        return false;
    }

    gVfs.mounts.Push(mount);
    logInfo("Mounted app package bundle to alias '%s'", mount.alias.CStr());
    return true;
}
#else   // PLATFORM_MOBILE
bool vfsMountPackageBundle(const char*)
{
    ASSERT_MSG(0, "This function must only be used on mobile platforms");
    return false;
}
#endif // !PLATFORM_MOBILE

Blob vfsReadFile(const char* path, VfsFlags flags, Allocator* alloc)
{
    ASSERT((flags & VfsFlags::CreateDirs) != VfsFlags::CreateDirs);
    ASSERT((flags & VfsFlags::Append) != VfsFlags::Append);

    uint32 idx = vfsFindMount(path);
    if (idx != UINT32_MAX) {
        VfsMountType type = gVfs.mounts[idx].type;
        ASSERT_MSG(type != VfsMountType::Remote, "Remote file requests cannot be done in blocking mode, call vfsReadFileAsync");
        if (type == VfsMountType::Local)
            return vfsDiskReadFile(path, flags, alloc);
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
        return vfsDiskReadFile(path, flags, alloc);
    }
}

size_t vfsWriteFile(const char* path, const Blob& blob, VfsFlags flags)
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

void vfsRegisterFileChangeCallback(vfsFileChangeCallback callback)
{
    ASSERT(callback);

    gVfs.fileChangeFns.Push(callback);
}

