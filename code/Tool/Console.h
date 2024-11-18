#pragma once

// implemented in Settings.cpp
#include "../Core/Base.h"

using ConCommandCallback = bool(*)(int argc, const char* argv[], char* outResponse, uint32 responseSize, void* userData);
struct ConCommandDesc
{
    const char* name;
    const char* help;
    ConCommandCallback callback;
    void* userData;
    uint32 minArgc;
    const char* shortcutKeys;
};

namespace Console
{
    API bool Execute(const char* cmd, char* outResponse = nullptr, uint32 responseSize = 0);
    API void ExecuteRemote(const char* cmd);

    // Used for sending response back to console for async responses
    API void SendResponse(const char* response);

    API void RegisterCommand(const ConCommandDesc& desc);
    API void UnregisterCommand();

    API bool Initialize(MemAllocator* alloc);
    API void Release();
}

