#include "SocketIO.h"

#if PLATFORM_WINDOWS
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/types.h> 
    #include <sys/socket.h> // socket funcs
    #include <errno.h>      // error-codes
    #include <unistd.h>     // close
    #include <netdb.h>      // getaddrinfo, freeaddrinfo
    #include <netinet/in.h> // sockaddr_in
    #include <arpa/inet.h>  // inet_ntop
#endif  // PLATFORM_WINDOWS (else)

#include "String.h"
#include "Log.h"

#if PLATFORM_WINDOWS
struct SocketPlatform 
{
    SOCKET s;
    SocketErrorCode errCode;
    uint16 live;
};

#define SOCKET_INVALID INVALID_SOCKET
static bool gSocketInitialized;

static void socketInitialize()
{
    if (!gSocketInitialized) {
        logDebug("Socket: Initialize");
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(1, 0), &wsaData) != 0) {
            ASSERT_ALWAYS(false, "Windows sockets initialization failed");
            return;
        }
        
        gSocketInitialized = true;
    }
}
#else // PLATFORM_WINDOWS
struct SocketPlatform 
{
    int s;
    SocketErrorCode errCode;
    uint16 live;
};

#define SOCKET_INVALID -1
#define SOCKET_ERROR -1

static void socketInitialize()
{
}
#endif // PLATFORM_WINDOWS (else)

static bool socketParseUrl(const char* url, char* address, size_t addressSize, 
                           char* port, size_t portSize, const char** pResource = nullptr)
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
        memcpy (port, addressEnd, portLen);
        port[portLen] = '\0';
    }
    else {
        return false;
    }    

    if (pResource)
        *pResource = portEnd;    
    return true;    
}

static SocketErrorCode socketTranslatePlatformErrorCode()
{
    #if PLATFORM_WINDOWS
        int errorCode = WSAGetLastError();
        switch (errorCode) {
        case WSAEADDRINUSE:     return SocketErrorCode::AddressInUse;
        case WSAECONNREFUSED:   return SocketErrorCode::ConnectionRefused;
        case WSAEISCONN:        return SocketErrorCode::AlreadyConnected;
        case WSAENETUNREACH: 
        case WSAENETDOWN:
        case WSAEHOSTUNREACH:   return SocketErrorCode::HostUnreachable;
        case WSAETIMEDOUT:      return SocketErrorCode::Timeout;
        case WSAECONNRESET:
        case WSAEINTR:
        case WSAENETRESET:      return SocketErrorCode::ConnectionReset;
        case WSAEADDRNOTAVAIL:  return SocketErrorCode::AddressNotAvailable;
        case WSAEAFNOSUPPORT:   return SocketErrorCode::AddressUnsupported;
        case WSAESHUTDOWN:      return SocketErrorCode::SocketShutdown;
        case WSAEMSGSIZE:       return SocketErrorCode::MessageTooLarge;
        case WSAENOTCONN:       return SocketErrorCode::NotConnected;
        default:                ASSERT_MSG(0, "Unknown socket error: %d", WSAGetLastError()); return SocketErrorCode::Unknown;
        }
    #else
        switch (errno) {
        case EADDRINUSE:        return SocketErrorCode::AddressInUse;
        case ECONNREFUSED:      return SocketErrorCode::ConnectionRefused;
        case EISCONN:           return SocketErrorCode::AlreadyConnected;
        case EHOSTUNREACH: 
        case ENETUNREACH:       return SocketErrorCode::HostUnreachable;
        case EWOULDBLOCK:
        case ETIMEDOUT:         return SocketErrorCode::Timeout;
        case ECONNRESET:        return SocketErrorCode::ConnectionReset;
        case EADDRNOTAVAIL:     return SocketErrorCode::AddressNotAvailable;
        case EAFNOSUPPORT:      return SocketErrorCode::AddressUnsupported;
        case ESHUTDOWN:         return SocketErrorCode::SocketShutdown;
        case EMSGSIZE:          return SocketErrorCode::MessageTooLarge;
        case ENOTCONN:          return SocketErrorCode::NotConnected;
        default:                return SocketErrorCode::Unknown;
        }
    #endif
}

void socketClose(Socket* socket)
{
    SocketPlatform* sockInternal = reinterpret_cast<SocketPlatform*>(socket);
    if (sockInternal && sockInternal->s != SOCKET_INVALID) {
        #if PLATFORM_WINDOWS
            if (sockInternal->live)
                shutdown(sockInternal->s, SD_BOTH);
            closesocket(sockInternal->s);
        #else   
            if (sockInternal->live)
                shutdown(sockInternal->s, SHUT_RDWR);
            close(sockInternal->s);
        #endif
        sockInternal->s = SOCKET_INVALID;
        sockInternal->errCode = SocketErrorCode::None;
        sockInternal->live = false;
    }
}

Socket socketOpenServer()
{
    socketInitialize();

    Socket sock {};
    SocketPlatform* sockInternal = reinterpret_cast<SocketPlatform*>(&sock);
    sockInternal->s = SOCKET_INVALID;

    sockInternal->s = socket(AF_INET, SOCK_STREAM, 0);
    if (sockInternal->s == SOCKET_INVALID) {
        sockInternal->errCode = socketTranslatePlatformErrorCode();
        logError("Socket: Opening the socket failed");
        return sock;
    }
    return sock;    
}

bool socketListen(Socket* sock, uint16 port, uint32 maxConnections)
{
    ASSERT(socketIsValid(sock));

    SocketPlatform* sockInternal = reinterpret_cast<SocketPlatform*>(sock);
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sockInternal->s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        sockInternal->errCode = socketTranslatePlatformErrorCode();
        logError("Socket: failed binding the socket to port: %d", port);
        return false;
    }

    logVerbose("Socket: Listening on port '%d' for incoming connections ...", port);
    int _maxConnections = maxConnections > INT32_MAX ? INT32_MAX : static_cast<int>(maxConnections);
    bool success = listen(sockInternal->s, _maxConnections) >= 0;
    
    if (!success) 
        sockInternal->errCode = socketTranslatePlatformErrorCode();
    else
        sockInternal->live = true;

    return success;
}

Socket socketAccept(Socket* sock, char* clientUrl, uint32 clientUrlSize)
{
    ASSERT(socketIsValid(sock));

    Socket newSock {};
    SocketPlatform* sockInternal = reinterpret_cast<SocketPlatform*>(sock);
    SocketPlatform* newSockInternal = reinterpret_cast<SocketPlatform*>(&newSock);
    newSockInternal->s = SOCKET_INVALID;

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    newSockInternal->s = accept(sockInternal->s, (struct sockaddr*)&addr, &addrlen);
    if (sockInternal->live && newSockInternal->s == SOCKET_INVALID) {
        newSockInternal->errCode = socketTranslatePlatformErrorCode();
        logError("Socket: failed to accept the new socket");
        return newSock;
    }

    if (clientUrl && clientUrlSize) {
        char ip[256];
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        uint16 port = htons(addr.sin_port);
        
        strPrintFmt(clientUrl, clientUrlSize, "%s:%d", ip, port);
    }

    newSockInternal->live = true;
    return newSock;
}

Socket socketConnect(const char* url)
{
    socketInitialize();

    Socket sock {};
    SocketPlatform* sockInternal = reinterpret_cast<SocketPlatform*>(&sock);
    sockInternal->s = SOCKET_INVALID;

    char address[256];
    char port[16];
    if (!socketParseUrl(url, address, sizeof(address), port, sizeof(port))) {
        logError("Socket: failed parsing the url: %s", url);
        return sock;
    }

    struct addrinfo hints;
    memset(&hints, 0x0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* addri = nullptr;
    if (getaddrinfo(address, port, &hints, &addri) != 0) {
        logError("Socket: failed to resolve url: %s", url);
        return sock;
    }

    sockInternal->s = socket(addri->ai_family, addri->ai_socktype, addri->ai_protocol);
    if (sockInternal->s == SOCKET_INVALID) {
        freeaddrinfo(addri);
        logError("Socket: failed to create socket");
        return sock;
    }

    if (connect(sockInternal->s, addri->ai_addr, (int)addri->ai_addrlen) == -1) {
        freeaddrinfo(addri);
        sockInternal->errCode = socketTranslatePlatformErrorCode();
        logError("Socket: failed to connect to url: %s", url);
        socketClose(&sock);
        return sock;
    }

    freeaddrinfo(addri);

    sockInternal->live = true;
    return sock;
}

uint32 socketWrite(Socket* sock, const void* src, uint32 size)
{
    ASSERT(socketIsValid(sock));

    SocketPlatform* sockInternal = reinterpret_cast<SocketPlatform*>(sock);
    ASSERT(sockInternal->live);
    uint32 totalBytesSent = 0;

    while (size > 0) {
        int bytesSent = send(sockInternal->s, reinterpret_cast<const char*>(src) + totalBytesSent, size, 0);
        if (bytesSent == 0) {
            break;
        }
        else if (bytesSent == -1) {
            sockInternal->errCode = socketTranslatePlatformErrorCode();
            if (sockInternal->errCode == SocketErrorCode::SocketShutdown ||
                sockInternal->errCode == SocketErrorCode::NotConnected)
            {
                logDebug("Socket: socket connection closed forcefully by the peer");
                sockInternal->live = false;
            }
            return UINT32_MAX;
        }

        totalBytesSent += static_cast<uint32>(bytesSent);
        size -= static_cast<uint32>(bytesSent);
    }

    return totalBytesSent;
}

uint32 socketRead(Socket* sock, void* dst, uint32 dstSize)
{
    ASSERT(socketIsValid(sock));
    
    SocketPlatform* sockInternal = reinterpret_cast<SocketPlatform*>(sock);
    ASSERT(sockInternal->live);

    int bytesRecv = recv(sockInternal->s, reinterpret_cast<char*>(dst), dstSize, 0);
    if (bytesRecv == -1) {
        sockInternal->errCode = socketTranslatePlatformErrorCode();
        if (sockInternal->errCode == SocketErrorCode::SocketShutdown ||
            sockInternal->errCode == SocketErrorCode::NotConnected)
        {
            logDebug("Socket: socket connection closed forcefully by the peer");
            sockInternal->live = false;
        }
        return UINT32_MAX;
    }

    return static_cast<uint32>(bytesRecv);
}


uint32 socketReadFile(Socket* sock, void* dst, uint32 size)
{
    ASSERT(socketIsValid(sock));
    
    SocketPlatform* sockInternal = reinterpret_cast<SocketPlatform*>(sock);
    ASSERT(sockInternal->live);
    uint32 totalBytesRecv = 0;

    while (size > 0) {
        int bytesRecv = recv(sockInternal->s, reinterpret_cast<char*>(dst) + totalBytesRecv, size, 0);
        if (bytesRecv == 0) {
            break;
        }
        else if (bytesRecv == -1) {
            sockInternal->errCode = socketTranslatePlatformErrorCode();
            if (sockInternal->errCode == SocketErrorCode::SocketShutdown ||
                sockInternal->errCode == SocketErrorCode::NotConnected)
            {
                logDebug("Socket: socket connection closed forcefully by the peer");
                sockInternal->live = false;
            }
            return UINT32_MAX;
        }

        totalBytesRecv += static_cast<uint32>(bytesRecv);
        size -= static_cast<uint32>(bytesRecv);
    }
    
    return totalBytesRecv;
}

bool socketIsValid(Socket* sock)
{
    if (((SocketPlatform*)sock)->s != SOCKET_INVALID) 
        return true;
    return false;
}

bool socketIsConnected(Socket* sock)
{
    return reinterpret_cast<SocketPlatform*>(sock)->live;
}

SocketErrorCode socketGetError(Socket* sock)
{
    return reinterpret_cast<SocketPlatform*>(sock)->errCode;
}

const char* socketGetErrorString(SocketErrorCode errCode)
{
    switch (errCode) {
    case SocketErrorCode::AddressInUse:         return "AddressInUse";
    case SocketErrorCode::AddressNotAvailable:  return "AddressNotAvailable";
    case SocketErrorCode::AddressUnsupported:   return "AddressUnsupported";
    case SocketErrorCode::AlreadyConnected:     return "AlreadyConnected";
    case SocketErrorCode::ConnectionRefused:    return "ConnectionRefused";        
    case SocketErrorCode::Timeout:              return "Timeout";
    case SocketErrorCode::HostUnreachable:      return "HostUnreachable";
    case SocketErrorCode::ConnectionReset:      return "ConnectionReset";
    case SocketErrorCode::SocketShutdown:       return "SocketShutdown";
    case SocketErrorCode::MessageTooLarge:      return "MessageTooLarge";
    case SocketErrorCode::NotConnected:         return "NotConnected";
    default:                                    return "Unknown";
    }
}
