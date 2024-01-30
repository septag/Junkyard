#pragma once

#include "../Core/Base.h"

struct Blob;
enum class SocketErrorCode : uint16;

static constexpr uint32 kRemoteErrorDescSize = 1024;

// Note: These Handlers are called from RemoteServices worker threads, implementations should consider making their state thread-safe
using RemoteCommandServerHandlerCallback = bool(*)(uint32 cmd, const Blob& incomingData, Blob* outgoingData, 
                                                   void* userData, char outgoingErrorDesc[kRemoteErrorDescSize]);
using RemoteCommandClientHandlerCallback = void(*)(uint32 cmd, const Blob& incomingData,
                                                   void* userData, bool error, const char* errorDesc);
using RemoteDisconnectCallback = void(*)(const char* url, bool onPurpose, SocketErrorCode errCode);

struct RemoteCommandDesc
{
    uint32 cmdFourCC;
    RemoteCommandServerHandlerCallback serverFn;
    RemoteCommandClientHandlerCallback clientFn;
    void* serverUserData;
    void* clientUserData;
    bool async;     // This means that server doesn't return immediate results, they are sent with `remoteSendResponse`
};

API void remoteRegisterCommand(const RemoteCommandDesc& desc);
API void remoteExecuteCommand(uint32 cmdCode, const Blob& data);
API bool remoteIsConnected();
API void remoteSendResponse(uint32 cmdCode, const Blob& data, bool error, const char* errorDesc);

namespace _private
{
    bool remoteInitialize();
    void remoteRelease();

    bool remoteConnect(const char* url, RemoteDisconnectCallback disconnectFn);
    void remoteDisconnect();
} // _private
