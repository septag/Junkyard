#pragma once

#include "../Core/Base.h"
#include "../Core/System.h"

struct Blob;

static constexpr uint32 REMOTE_ERROR_SIZE = 1024;

// Note: These Handlers are called from RemoteServices worker threads, implementations should consider making their state thread-safe
using RemoteCommandServerHandlerCallback = bool(*)(uint32 cmd, const Blob& incomingData, Blob* outgoingData, 
                                                   void* userData, char outgoingErrorDesc[REMOTE_ERROR_SIZE]);
using RemoteCommandClientHandlerCallback = void(*)(uint32 cmd, const Blob& incomingData,
                                                   void* userData, bool error, const char* errorDesc);
using RemoteDisconnectCallback = void(*)(const char* url, bool onPurpose, SocketErrorCode::Enum errCode);

struct RemoteCommandDesc
{
    uint32 cmdFourCC;
    RemoteCommandServerHandlerCallback serverFn;
    RemoteCommandClientHandlerCallback clientFn;
    void* serverUserData;
    void* clientUserData;
    bool async;     // This means that server doesn't return immediate results, they are sent with `Remote::SendResponse`
};

namespace Remote
{
    API void RegisterCommand(const RemoteCommandDesc& desc);
    API void ExecuteCommand(uint32 cmdCode, const Blob& data);
    API bool IsConnected();
    API void SendResponse(uint32 cmdCode, const Blob& data, bool error, const char* errorDesc);
    API void SendResponseMerge(uint32 cmdCode, const Blob* blobs, uint32 numBlobs, bool error, const char* errorDesc);

    API bool Initialize();
    API void Release();

    API bool Connect(const char* url, RemoteDisconnectCallback disconnectFn);
    API void Disconnect();
};
