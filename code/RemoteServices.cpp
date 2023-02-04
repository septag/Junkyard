#include "RemoteServices.h"

#include "Core/System.h"
#include "Core/Settings.h"
#include "Core/SocketIO.h"
#include "Core/Array.h"
#include "Core/Buffers.h"
#include "Core/String.h"
#include "Core/Log.h"

static constexpr uint32 kCmdFlag = MakeFourCC('U', 'S', 'R', 'C');
static constexpr uint32 kCmdHello = MakeFourCC('H', 'E', 'L', 'O');
static constexpr uint32 kCmdBye = MakeFourCC('B', 'Y', 'E', '0');

// Only used in client response packets
static constexpr uint32 kResultError = MakeFourCC('E', 'R', 'O', 'R');
static constexpr uint32 kResultOk = MakeFourCC('O', 'K', '0', '0');

struct RemoteServicesContext
{
    Socket serverSock;
    Socket serverPeerSock;
    Mutex serverPeerMtx;
    Thread serverThread;
    bool serverQuit;
    Array<RemoteCommandDesc> commands;

    Socket clientSock;
    Mutex clientMtx;
    Thread clientThread;
    RemoteDisconnectCallback disconnectFn;
    bool clientQuit;
    bool clientIsConnected;

    String<128> peerUrl;
};

static RemoteServicesContext gRemoteServices;

// MT: This function is thread-safe, but be aware of too many calls at the same time, because it locks the thread
void remoteSendResponse(uint32 cmdCode, const Blob& data, bool error, const char* errorDesc)
{
    uint32 cmdIdx = gRemoteServices.commands.FindIf([cmdCode](const RemoteCommandDesc& cmd) { return cmd.cmdFourCC == cmdCode; });
    if (cmdIdx != INVALID_INDEX) {
        MutexScope mtx(gRemoteServices.serverPeerMtx);
        Socket* sock = &gRemoteServices.serverPeerSock;
        if (socketIsValid(sock) && socketIsConnected(sock)) {
            uint32 dataSize = static_cast<uint32>(data.Size());
            const uint32 cmdHeader[] = {kCmdFlag, cmdCode, !error ? kResultOk : kResultError, dataSize};

            MemTempAllocator tmp;
            Blob dataOut(&tmp);
            dataOut.SetGrowPolicy(Blob::GrowPolicy::Multiply);
            dataOut.Reserve(dataSize + sizeof(cmdHeader) + (error ? kRemoteErrorDescSize : 0));

            dataOut.Write(cmdHeader, sizeof(cmdHeader));
            if (dataSize)
                dataOut.Write(data.Data(), dataSize);

            // Append error message to the end
            if (error) {
                ASSERT(errorDesc);
                dataOut.WriteStringBinary(errorDesc, strLen(errorDesc));
            }

            socketWrite(sock, dataOut.Data(), static_cast<uint32>(dataOut.Size()));
            dataOut.Free();
        }
    }
    else {
        logDebug("RemoteServices: Invalid command: 0x%x", cmdCode);
        ASSERT(0);
    }
}

static int serverPeerThreadFn(void* userData)
{
    uint8 tmpBuffer[4096];
    Socket* sock = reinterpret_cast<Socket*>(userData);

    bool saidHello = false;
    bool quit = false;

    while (!gRemoteServices.serverQuit && !quit) {
        uint32 packet[3] = {0, 0, 0};       // {kCmdFlag, CmdCode, DataSize}
        uint32 bytesRead = socketRead(sock, packet, sizeof(packet));
        if (bytesRead == UINT32_MAX || bytesRead == 0) {
            SocketErrorCode errCode = socketGetError(sock);
            if (errCode == SocketErrorCode::ConnectionReset || bytesRead == 0) {
                logInfo("RemoteServices: Disconnected from client '%s'", gRemoteServices.peerUrl.CStr());
            }
            else {
                logDebug("RemoteServices: Socket Error: %s", socketGetErrorString(errCode));
            }
            break;
        }

        // Drop packets that does not have the header
        if (packet[0] != kCmdFlag) {
            logDebug("RemoteServices: Invalid packet");
            break;
        }
        
        uint32 cmdCode = packet[1];
        if (!saidHello && cmdCode == kCmdHello) {
            // Hello back
            const uint32 hello[] = {kCmdFlag, kCmdHello, 0};
            socketWrite(sock, hello, sizeof(hello));
            saidHello = true;
        }
        else if (saidHello) {
            if (cmdCode == kCmdBye) {
                // bye back and close
                const uint32 bye[] = {kCmdFlag, kCmdBye, 0};
                socketWrite(sock, bye, sizeof(bye));
                saidHello = false;
                quit = true;
            }   
            else {
                // Read custom command and execute it's callbacks if found
                // Custom commands have an extra size+data section ([header] + [cmd] + [size] = 3*uint32)
                uint32 cmdIdx = gRemoteServices.commands.FindIf([cmdCode](const RemoteCommandDesc& cmd) { return cmd.cmdFourCC == cmdCode; });
                if (cmdIdx != INVALID_INDEX && bytesRead >= sizeof(uint32)*3) {
                    const RemoteCommandDesc& cmd = gRemoteServices.commands[cmdIdx];
                    ASSERT(cmd.serverFn);

                    uint32 dataSize = packet[2];

                    MemTempAllocator tmpAlloc;
                    Blob dataBlob(&tmpAlloc);
                    if (dataSize > 0) {
                        dataBlob.SetGrowPolicy(Blob::GrowPolicy::Multiply);
                        dataBlob.Reserve(dataSize);
                        
                        while (dataSize) {
                            bytesRead = socketRead(sock, tmpBuffer, Min<uint32>(dataSize, sizeof(tmpBuffer)));
                            if (bytesRead == UINT32_MAX) {
                                SocketErrorCode errCode = socketGetError(sock);
                                if (errCode == SocketErrorCode::ConnectionReset) {
                                    logInfo("RemoteServices: Disconnected from client '%s'", gRemoteServices.peerUrl.CStr());
                                }
                                else {
                                    logDebug("RemoteServices: Socket Error: %s", socketGetErrorString(errCode));
                                }
                                quit = true;
                                break;
                            }

                            dataBlob.Write(tmpBuffer, bytesRead);
                            dataSize -= bytesRead;
                        }
                    }

                    
                    Blob outgoingDataBlob(&tmpAlloc);
                    outgoingDataBlob.SetGrowPolicy(Blob::GrowPolicy::Multiply);

                    char errorDesc[kRemoteErrorDescSize];   errorDesc[0] = '\0';
                    bool r = cmd.serverFn(cmd.cmdFourCC, dataBlob, &outgoingDataBlob, cmd.serverUserData, errorDesc);

                    // send the reply back to the client only if the callback is not async
                    if (!cmd.async || !r)
                        remoteSendResponse(cmdCode, outgoingDataBlob, !r, errorDesc);

                    outgoingDataBlob.Free();
                    dataBlob.Free();
                }
                else { 
                    logDebug("RemoteServices: Invalid incoming command: 0x%x (%c%c%c%c)", cmdCode, 
                        cmdCode&0xff, (cmdCode>>8)&0xff, (cmdCode>>16)&0xff, cmdCode>>24);
                }
            }
        }
        else {
            // Something went wrong. Handshake is not complete. Drop the connection
            quit = true;
        }
    }
    
    socketClose(sock);
    return 0;
}

static int serverThreadFn(void*)
{
    gRemoteServices.serverSock = socketOpenServer();
    
    if (socketListen(&gRemoteServices.serverSock, settingsGetTooling().serverPort, 1)) {
        char peerUrl[128];
        while (!gRemoteServices.serverQuit) {
            gRemoteServices.serverPeerSock = socketAccept(&gRemoteServices.serverSock, peerUrl, sizeof(peerUrl));
            if (socketIsValid(&gRemoteServices.serverPeerSock)) {
                logInfo("RemoteServices: Incoming connection: %s", peerUrl);
                gRemoteServices.peerUrl = peerUrl;

                Thread thrd;
                thrd.Start(ThreadDesc {
                    .entryFn = serverPeerThreadFn,
                    .userData = &gRemoteServices.serverPeerSock,
                    .name = "ServerClientPipe"
                });
                thrd.SetPriority(ThreadPriority::Low);
                thrd.Stop(); // wait on the service to finish
            }
        }
    }

    socketClose(&gRemoteServices.serverSock);    
    return 0;
}


bool _private::remoteInitialize()
{
    gRemoteServices.serverPeerMtx.Initialize();
    gRemoteServices.clientMtx.Initialize();

    if (settingsGetTooling().enableServer) {
        logInfo("(init) RemoteServices: Starting RemoteServices server in port %u...", settingsGetTooling().serverPort);
        gRemoteServices.serverThread.Start(ThreadDesc {
            .entryFn = serverThreadFn, 
            .name = "RemoteServicesServer"
        });

        gRemoteServices.serverThread.SetPriority(ThreadPriority::Low);
    }
    return true;
}

void _private::remoteRelease()
{
    gRemoteServices.serverQuit = true;
    if (socketIsValid(&gRemoteServices.serverPeerSock))
        socketClose(&gRemoteServices.serverPeerSock);
    if (socketIsValid(&gRemoteServices.serverSock))
        socketClose(&gRemoteServices.serverSock);
    gRemoteServices.serverThread.Stop();

    gRemoteServices.clientQuit = true;
    if (socketIsValid(&gRemoteServices.clientSock)) 
        socketClose(&gRemoteServices.clientSock);
    gRemoteServices.clientThread.Stop();
    
    gRemoteServices.serverPeerMtx.Release();
    gRemoteServices.clientMtx.Release();
    gRemoteServices.commands.Free();
}

static int remoteClientThreadFn(void*)
{
    uint8 tmpBuffer[4096];
    Socket* sock = &gRemoteServices.clientSock;
    ASSERT(socketIsValid(sock));

    bool quit = false;
    while (!gRemoteServices.clientQuit && !quit) {
        uint32 packet[4] = {0, 0, 0, 0};       // {kCmdFlag, CmdCode, ErrorIndicator, DataSize}
        uint32 bytesRead = socketRead(sock, packet, sizeof(packet));
        if (bytesRead == UINT32_MAX) {
            logDebug("RemoteServices: Socket Error: %s", socketGetErrorString(socketGetError(sock)));
            break;
        }

        // Drop packets that does not have the header
        if (packet[0] != kCmdFlag) {
            logDebug("RemoteServices: Invalid packet");
            break;
        }

        uint32 cmdCode = packet[1];
        if (cmdCode == kCmdBye) {
            // bye back and close
            const uint32 bye[] = {kCmdFlag, kCmdBye, 0};
            socketWrite(sock, bye, sizeof(bye));
            quit = true;
        }
        else {
            uint32 cmdIdx = gRemoteServices.commands.FindIf([cmdCode](const RemoteCommandDesc& cmd) { return cmd.cmdFourCC == cmdCode; });
            if (cmdIdx != INVALID_INDEX) {
                const RemoteCommandDesc& cmd = gRemoteServices.commands[cmdIdx];
                ASSERT(cmd.clientFn);

                uint32 dataSize = packet[3];
                
                MemTempAllocator tmpAlloc;
                Blob incomingDataBlob(&tmpAlloc);
                if (dataSize > 0) {
                    incomingDataBlob.SetGrowPolicy(Blob::GrowPolicy::Multiply);
                    incomingDataBlob.Reserve(dataSize);
                        
                    while (dataSize) {
                        bytesRead = socketRead(sock, tmpBuffer, Min<uint32>(dataSize, sizeof(tmpBuffer)));
                        if (bytesRead == UINT32_MAX) {
                            logDebug("RemoteServices: Socket Error: %s", socketGetErrorString(socketGetError(sock)));
                            quit = true;
                            break;
                        }
                        incomingDataBlob.Write(tmpBuffer, bytesRead);
                        dataSize -= bytesRead;
                    }
                }

                // Check for errors
                char errorDesc[kRemoteErrorDescSize];   
                memset(errorDesc, 0x0, sizeof(errorDesc));

                if (packet[2] == kResultError) {
                    uint32 errorLen;
                    socketRead<uint32>(sock, &errorLen);
                    if (errorLen)
                        socketRead(sock, errorDesc, sizeof(errorDesc));
                }
                else {
                    ASSERT(packet[2] == kResultOk);
                }

                cmd.clientFn(cmdCode, incomingDataBlob, cmd.clientUserData, packet[2] == kResultError, errorDesc);
            }
            else {
                logDebug("RemoteServices: Invalid response command from server: 0x%x (%c%c%c%c)", cmdCode, 
                    cmdCode&0xff, (cmdCode>>8)&0xff, (cmdCode>>16)&0xff, cmdCode>>24);
            }
        }
    }   // while not quit

    SocketErrorCode errCode = socketGetError(sock);
    socketClose(sock);

    if (gRemoteServices.disconnectFn)
        gRemoteServices.disconnectFn(gRemoteServices.peerUrl.CStr(), gRemoteServices.clientQuit, errCode);
    gRemoteServices.clientIsConnected = false;
    return 0;
}


bool _private::remoteConnect(const char* url, RemoteDisconnectCallback disconnectFn)
{
    ASSERT_MSG(!gRemoteServices.clientIsConnected, "Client is already connected");
    
    MutexScope mtx(gRemoteServices.clientMtx);
    
    if (gRemoteServices.clientIsConnected) {
        ASSERT(socketIsConnected(&gRemoteServices.clientSock));
        return true;
    }

    gRemoteServices.clientThread.Stop();
    logInfo("(init) RemoteServices: Connecting to remote server: %s ...", url);

    Socket* sock = &gRemoteServices.clientSock;
    *sock = socketConnect(url);
    if (!socketIsValid(sock) || !socketIsConnected(sock)) {
        logError("RemoteServices: Connecting to remote url '%s' failed", url);
        return false;
    }

    // Say hello
    const uint32 hello[] = {kCmdFlag, kCmdHello, 0};
    if (socketWrite(sock, hello, sizeof(hello)) != sizeof(hello)) {
        logError("RemoteServices: Connecting to remote url '%s' failed", url);
        socketClose(sock);
        return false;        
    }

    // Receive hello and complete the handshake
    uint32 response[3];
    if (socketRead(sock, response, sizeof(response)) != sizeof(response) ||
        response[0] != kCmdFlag || response[1] != kCmdHello)
    {
        logError("RemoteServices: Invalid response from disk server: %s", url);
        socketClose(sock);
        return false;
    }
    
    gRemoteServices.clientThread.Start(ThreadDesc {
        .entryFn = remoteClientThreadFn, 
        .name = "RemoteServicesClient"
    });
    gRemoteServices.clientThread.SetPriority(ThreadPriority::Low);

    logInfo("(init) RemoteServices: Connected to remote server: %s", url);
    gRemoteServices.disconnectFn = disconnectFn;
    gRemoteServices.peerUrl = url;
    gRemoteServices.clientIsConnected = true;
    return true;
}

void _private::remoteDisconnect()
{
    gRemoteServices.clientQuit = true;
    if (socketIsValid(&gRemoteServices.clientSock)) 
        socketClose(&gRemoteServices.clientSock);
    gRemoteServices.clientThread.Stop();
    gRemoteServices.clientQuit = false;
    gRemoteServices.disconnectFn = nullptr;
    gRemoteServices.peerUrl = "";
}

bool remoteIsConnected()
{
    MutexScope mtx(gRemoteServices.clientMtx);
    return gRemoteServices.clientIsConnected && socketIsConnected(&gRemoteServices.clientSock);
}

// MT: This function is thread-safe, multiple calls to remoteExecuteCommand from several threads locks it
void remoteExecuteCommand(uint32 cmdCode, const Blob& data)
{
    uint32 cmdIdx = gRemoteServices.commands.FindIf([cmdCode](const RemoteCommandDesc& cmd) { return cmd.cmdFourCC == cmdCode; });
    if (cmdIdx != INVALID_INDEX) {
        MutexScope mtx(gRemoteServices.clientMtx);
        Socket* sock = &gRemoteServices.clientSock;
        if (socketIsValid(sock) && socketIsConnected(sock)) {
            uint32 dataSize = static_cast<uint32>(data.Size());
            const uint32 cmdHeader[] = {kCmdFlag, cmdCode, dataSize};

            Blob outgoing;      // TODO: use temp allocator
            outgoing.Reserve(sizeof(cmdHeader) + dataSize);
            outgoing.Write(cmdHeader, sizeof(cmdHeader));
            if (dataSize)
                outgoing.Write(data.Data(), dataSize);
            socketWrite(sock, outgoing.Data(), static_cast<uint32>(outgoing.Size()));
            outgoing.Free();
        }
    }
    else {
        logDebug("RemoteServices: Invalid command: 0x%x (%c%c%c%c)", cmdCode, 
            cmdCode&0xff, (cmdCode>>8)&0xff, (cmdCode>>16)&0xff, cmdCode>>24);
        ASSERT(0);
    }
}

void remoteRegisterCommand(const RemoteCommandDesc& desc)
{
    if (gRemoteServices.commands.FindIf([fourCC = desc.cmdFourCC](const RemoteCommandDesc& cmd) 
                                        { return cmd.cmdFourCC == fourCC; }) != INVALID_INDEX) 
    {
        logError("Remote command with FourCC 0x%x (%c%c%c%c) is already registered", desc.cmdFourCC, 
            desc.cmdFourCC&0xff, (desc.cmdFourCC>>8)&0xff, (desc.cmdFourCC>>16)&0xff, desc.cmdFourCC>>24);
        ASSERT(0);
        return;
    }

    gRemoteServices.commands.Push(desc);
}

