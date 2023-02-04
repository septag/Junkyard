#pragma once

#include "Base.h"

struct Socket
{
    uint8 data[16];
};

enum class SocketErrorCode : uint16
{
    None = 0,
    AddressInUse,
    AddressNotAvailable,
    AddressUnsupported,
    AlreadyConnected,
    ConnectionRefused,
    Timeout,
    HostUnreachable,
    ConnectionReset,
    SocketShutdown,
    MessageTooLarge,
    NotConnected,
    Unknown
};

API void socketClose(Socket* sock);

// Server API
API Socket socketOpenServer();
API bool socketListen(Socket* sock, uint16 port, uint32 maxConnections = UINT32_MAX);
// Accepts incoming TCP connections, must be called after a successfull listen
// Blocks the program until incoming connection request comes in
API Socket socketAccept(Socket* sock, char* clientUrl = nullptr, uint32 clientUrlSize = 0);

// Client API
API Socket socketConnect(const char* url);

API bool socketIsValid(Socket* sock);
API bool socketIsConnected(Socket* sock);

// IO
// Returns number of bytes written/read
// Returns 0 if connection is closed gracefully
// Returns UINT32_MAX if there was an error. check socketGetError()
API uint32 socketWrite(Socket* sock, const void* src, uint32 size);
API uint32 socketRead(Socket* sock, void* dst, uint32 dstSize);
API uint32 socketReadFile(Socket* sock, void* dst, uint32 size);

// Error handling
API SocketErrorCode socketGetError(Socket* sock);
API const char* socketGetErrorString(SocketErrorCode errCode);

// Template helpers
template <typename _T>
uint32 socketWrite(Socket* sock, const _T& val)
{
    return socketWrite(sock, &val, sizeof(_T));
}

template <typename _T>
uint32 socketRead(Socket* sock, _T* dst)
{
    return socketRead(sock, dst, sizeof(_T));
}
