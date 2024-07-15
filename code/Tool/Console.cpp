#include "Console.h"

#include "../Core/StringUtil.h"
#include "../Core/System.h"
#include "../Core/Log.h"
#include "../Core/IniParser.h"
#include "../Core/Allocators.h"
#include "../Core/Arrays.h"

#include "../Common/RemoteServices.h"
#include "../Common/VirtualFS.h"

#include "../Engine.h"

constexpr uint32 CONSOLE_REMOTE_CMD = MakeFourCC('C', 'O', 'N', 'X');

struct ConCustomVar
{
    String32 name;
    String<PATH_CHARS_MAX> value;
};

struct ConContext
{
    Array<ConCommandDesc> commands;
    Array<ConCustomVar> vars;
};

static ConContext gConsole;

namespace Console
{

bool Execute(const char* cmd, char* outResponse, uint32 responseSize)
{
    ASSERT(cmd);

    MemTempAllocator tmpAlloc;

    // match variables (begin and end with {} sign) with their values
    auto LookupVariable = [](const char* name)->const char* {
        uint32 index = gConsole.vars.FindIf([name](const ConCustomVar& var) { return var.name.IsEqualNoCase(name); });
        return index != UINT32_MAX ? gConsole.vars[index].value.CStr() : nullptr;        
    };
    Blob cmdBuffer(&tmpAlloc);
    cmdBuffer.SetGrowPolicy(Blob::GrowPolicy::Linear, 256);
    
    const char* cmdStart = cmd;
    const char* bracket;
    while ((bracket = strFindChar(cmdStart, '{')) != nullptr) {
        const char* closingBracket = strFindChar(bracket+1, '}');
        if (closingBracket) {
            cmdBuffer.Write(cmd, size_t(bracket - cmdStart));

            if (closingBracket > bracket + 1) {
                char varName[32] {};
                strCopyCount(varName, sizeof(varName), bracket+1, uint32(closingBracket-bracket-1));
                
                const char* replacement = LookupVariable(varName);
                if (replacement) 
                    cmdBuffer.Write(replacement, strLen(replacement));
            }

            cmdStart = closingBracket + 1;
        }
        else {
            cmdStart = bracket + 1;
        }
    }

    if (cmdStart[0])
        cmdBuffer.Write(cmdStart, strLen(cmdStart));
    cmdBuffer.Write<char>('\0');

    // tokenize space
    Array<char*> argv(&tmpAlloc);
    char* cmdCopy;
    size_t cmdCopySize;
    cmdBuffer.Detach((void**)&cmdCopy, &cmdCopySize);

    // spaces between quote/unquote are ignored
    char* c = cmdCopy;
    char* argStart = c;
    char quote = 0;
    while (*c != 0) {
        if (strIsWhitespace(*c) && quote == 0) {
            *c = 0;
            if (c != argStart)
                argv.Push(argStart);
            argStart = c + 1;
        }
        else if (quote == 0 && (*c == '"' || *c == '\'')) {
            quote = *c;
        }
        else if (quote != 0 && quote == *c) {
            quote = 0;
        }

        ++c;
    }
    if (c != argStart)
        argv.Push(argStart);

    if (outResponse && responseSize)
        memset(outResponse, 0x0, responseSize);

    if (argv.Count()) {
        const char* name = argv[0];
        uint32 index = gConsole.commands.FindIf([name](const ConCommandDesc& desc) { return strIsEqualNoCase(name, desc.name); });
        if (index != UINT32_MAX) {
            const ConCommandDesc& cmdDesc = gConsole.commands[index];
            if (argv.Count() < cmdDesc.minArgc) {
                strPrintFmt(outResponse, responseSize, "Command '%s' failed. Invalid number of arguments (expected %u)", 
                            name, cmdDesc.minArgc);
                return false;
            }
            else {
                return gConsole.commands[index].callback((int)argv.Count(), (const char**)argv.Ptr(), 
                                                         outResponse, responseSize, cmdDesc.userData);
            }
        }
        else {
            if (outResponse && responseSize)
                strPrintFmt(outResponse, responseSize, "Command not found: %s", name);
            LOG_WARNING("Command not found: %s", name);
            return false;
        }
    }
    else {
        if (outResponse && responseSize) 
            strPrintFmt(outResponse, responseSize, "Cannot parse command: %s", cmd);
        LOG_WARNING("Cannot parse command: %s", cmd);
        return false;
    }
}

void ExecuteRemote(const char* cmd)
{
    ASSERT(cmd);
    ASSERT(cmd[0]);

    if (Remote::IsConnected()) {
        MemTempAllocator tmpAlloc;
        Blob blob(&tmpAlloc);
        blob.SetGrowPolicy(Blob::GrowPolicy::Multiply);
        blob.WriteStringBinary(cmd, strLen(cmd));
    
        Remote::ExecuteCommand(CONSOLE_REMOTE_CMD, blob);
    }
}

static void HandlerClientCallback([[maybe_unused]] uint32 cmd, const Blob& incomingData, void*, bool error, const char* errorDesc)
{
    ASSERT(cmd == CONSOLE_REMOTE_CMD);
    if (!error) {
        char responseText[512];
        incomingData.ReadStringBinary(responseText, sizeof(responseText));
        LOG_INFO(responseText);
    }
    else {
        LOG_ERROR(errorDesc);
    }
}

static bool HandlerServerCallback([[maybe_unused]] uint32 cmd, const Blob& incomingData, Blob* outgoingBlob, void*, 
                                  char outgoingErrorDesc[kRemoteErrorDescSize])
{
    ASSERT(cmd == CONSOLE_REMOTE_CMD);

    char cmdline[1024];
    char response[1024];
    incomingData.ReadStringBinary(cmdline, sizeof(cmdline));
    bool r = Execute(cmdline, response, sizeof(response));

    if (r)
        outgoingBlob->WriteStringBinary(response, strLen(response));
    else
        strCopy(outgoingErrorDesc, kRemoteErrorDescSize, response);

    return r;
}

static bool conRunExternalCommand(int argc, const char* argv[])
{
    #if PLATFORM_DESKTOP
        ASSERT(argc > 1);
    
        const char* prefixCmd = nullptr;
        Path ext = Path(argv[1]).GetFileExtension_CStr();
        SysProcessFlags flags = SysProcessFlags::None;
        if (ext.IsEqualNoCase(".bat") || ext.IsEqualNoCase(".cmd")) {
            prefixCmd = "cmd /k";
            flags |= SysProcessFlags::ForceCreateConsole;
        }
    
        char* cmdline;
        uint32 cmdlineSize;
        MemTempAllocator tmpAlloc;
        OS::GenerateCmdLineFromArgcArgv(argc - 1, &argv[1], &cmdline, &cmdlineSize, Mem::GetDefaultAlloc(), prefixCmd);
        SysProcess process;
        bool r = process.Run(cmdline, flags);
        Mem::Free(cmdline);
    
        return r;
    #else
        UNUSED(argc);
        UNUSED(argv);
        return false;
    #endif
}

bool Initialize()
{
    gConsole.commands.SetAllocator(Mem::GetDefaultAlloc());
    gConsole.vars.SetAllocator(Mem::GetDefaultAlloc());

    // Custom variables that are used to be replaced with the value if they come in between {} sign
    {
        MemTempAllocator tmpAlloc;
        INIFileContext varsIni = INIFile::Load("Vars.ini");
        if (varsIni.IsValid()) {
            INIFileSection root = varsIni.GetRootSection();
            for (uint32 i = 0; i < root.GetPropertyCount(); i++) {
                INIFileProperty prop = root.GetProperty(i);
                gConsole.vars.Push(ConCustomVar {
                    .name = prop.GetName(),
                    .value = prop.GetValue()
                });                
            }
            varsIni.Destroy();

            for (uint32 i = 0; i < gConsole.vars.Count(); i++) {
                gConsole.vars[i].name.Trim();
                gConsole.vars[i].value.Trim();
            }
        }
    }

    //
    Remote::RegisterCommand(RemoteCommandDesc {
        .cmdFourCC = CONSOLE_REMOTE_CMD,
        .serverFn = HandlerServerCallback,
        .clientFn = HandlerClientCallback
    });

    // Some common commands
    // Execute process
    auto ExecFn = [](int argc, const char* argv[], char* outResponse, uint32 responseSize, void*)->bool {
        UNUSED(outResponse);
        UNUSED(responseSize);
        return conRunExternalCommand(argc, argv);
    };

    RegisterCommand(ConCommandDesc {
        .name = "exec",
        .help = "execute a process with command-line",
        .callback = ExecFn,
        .minArgc = 2
    });

    auto ExecUniqueFn = [](int argc, const char* argv[], char* outResponse, uint32 responseSize, void*)->bool {
        UNUSED(outResponse);
        UNUSED(responseSize);
        #if PLATFORM_WINDOWS
            ASSERT(argc > 0);
            Path processName = Path(argv[0]).GetFileName();
            if (!OS::Win32IsProcessRunning(processName.CStr()))
                return conRunExternalCommand(argc, argv);
            else
                return true;
        #else
            UNUSED(argc);
            UNUSED(argv);
            return false;
        #endif
    };

    RegisterCommand(ConCommandDesc {
        .name = "exec-once",
        .help = "execute the process once, meaning that it will skip execution if the process is already running",
        .callback = ExecUniqueFn,
        .minArgc = 2
    });

    return true;
}

void Release()
{
    gConsole.commands.Free();
    gConsole.vars.Free();
}

void RegisterCommand(const ConCommandDesc& desc)
{
    [[maybe_unused]] uint32 index = gConsole.commands.FindIf([name=desc.name](const ConCommandDesc& desc) { return strIsEqual(desc.name, name); });
    ASSERT_MSG(index == UINT32_MAX, "Command '%s' already registered", desc.name);

    ConCommandDesc* data = gConsole.commands.Push(desc);
    if (desc.shortcutKeys && desc.shortcutKeys[0])
        Engine::RegisterShortcut(desc.shortcutKeys, [](void* userData) { Execute(((ConCommandDesc*)userData)->name); }, data);
}

} // Console