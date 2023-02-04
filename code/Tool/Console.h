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

API bool conExecute(const char* cmd, char* outResponse = nullptr, uint32 responseSize = 0);
API void conExecuteRemote(const char* cmd);

// Used for sending response back to console for async responses
API void conSendResponse(const char* response);

API void conRegisterCommand(const ConCommandDesc& desc);
API void conUnregisterCommand();

namespace _private
{
    bool conInitialize();
    void conRelease();
}
