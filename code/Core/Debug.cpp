#include "Debug.h"

#include <stdarg.h> // va_list/va_start
#include <stdio.h>  // puts

#include "System.h"         // sysAndroidPrintXXX/sysWinDebugger..
#include "StringUtil.h"
#include "Arrays.h"

#ifdef BUILD_UNITY
    #if PLATFORM_WINDOWS
        #include "DebugWin.cpp"
    #elif PLATFORM_POSIX
        #include "DebugClang.cpp"
    #endif
#endif 

static bool gDebugCaptureStacktraceForFiberProtector;

void Debug::PrintLine(const char* text)
{
    #if PLATFORM_WINDOWS
    OS::Win32PrintToDebugger(text);
    OS::Win32PrintToDebugger("\n");
    #elif PLATFORM_ANDROID
    OS::AndroidPrintToLog(OSAndroidLogType::Debug, CONFIG_APP_NAME, text);
    #else
    puts(text);
    #endif
}

void Debug::PrintLineFmt(const char* fmt, ...)
{
    char text[1024];
    va_list args;
    va_start(args, fmt);
    [[maybe_unused]] uint32 len = Str::PrintFmtArgs(text, sizeof(text)-1, fmt, args);
    va_end(args);

    #if PLATFORM_WINDOWS
    text[len] = '\n';
    text[len+1] = '\0';
    OS::Win32PrintToDebugger(text);
    #elif PLATFORM_ANDROID
    OS::AndroidPrintToLog(OSAndroidLogType::Debug, CONFIG_APP_NAME, text);
    #else
    puts(text);
    #endif
}

void Debug::SetCaptureStacktraceForFiberProtector(bool capture)
{
    gDebugCaptureStacktraceForFiberProtector = capture;
}

#if CONFIG_ENABLE_ASSERT
static constexpr uint16 kDebugMaxFiberProtectorStackframes = 8;

using DebugFiberScopeProtectorCallbackPair = Pair<DebugFiberScopeProtectorCallback, void*>;
struct DebugFiberProtector
{
    StaticArray<DebugFiberScopeProtectorCallbackPair, 4> callbacks;
};

struct DebugFiberProtectorThreadContext
{
    struct Item 
    {
        const char* name;
        void* stackframes[kDebugMaxFiberProtectorStackframes];
        uint16 numStackframes;
        uint16 id;
    };

    ~DebugFiberProtectorThreadContext()
    {
        items.Free();
    }

    uint16 idGen;
    Array<Item> items;
};
 
static DebugFiberProtector gFiberProtector;
NO_INLINE static DebugFiberProtectorThreadContext& FiberProtectorCtx() 
{ 
    static thread_local DebugFiberProtectorThreadContext fiberProtectorCtx;
    return fiberProtectorCtx; 
}

void Debug::FiberScopeProtector_RegisterCallback(DebugFiberScopeProtectorCallback callback, void* userData)
{
    ASSERT_MSG(gFiberProtector.callbacks.FindIf([callback](const DebugFiberScopeProtectorCallbackPair& p) { return p.first == callback; }) == UINT32_MAX,
               "Callback already added");
    gFiberProtector.callbacks.Push(DebugFiberScopeProtectorCallbackPair(callback, userData));
}

INLINE bool debugFiberScopeProtector_IsInFiber()
{
    bool inFiber = false;
    for (const DebugFiberScopeProtectorCallbackPair p : gFiberProtector.callbacks)
        inFiber |= p.first(p.second);
    return inFiber;
}

uint16 Debug::FiberScopeProtector_Push(const char* name)
{
    if (debugFiberScopeProtector_IsInFiber()) {
        ASSERT(name);
        DebugFiberProtectorThreadContext::Item* item = FiberProtectorCtx().items.Push();
        memset(item, 0x0, sizeof(*item));
        item->name = name;
        if (gDebugCaptureStacktraceForFiberProtector) 
            item->numStackframes = Debug::CaptureStacktrace(item->stackframes, kDebugMaxFiberProtectorStackframes, 2);
        uint16 id = ++FiberProtectorCtx().idGen;
        if (id == 0)
            id = 1;
        item->id = id;
        return id;
    }
    return 0;
}

void Debug::FiberScopeProtector_Pop(uint16 id)
{
    if (id == 0)
        return;
    
    ASSERT_MSG(debugFiberScopeProtector_IsInFiber(), "Item was pushed in the fiber, but not popping in any fibers");
    ASSERT(FiberProtectorCtx().items.Count());

    uint32 index = FiberProtectorCtx().items.FindIf([id](const DebugFiberProtectorThreadContext::Item& item) { return item.id == id; });
    ASSERT_MSG(index != UINT32_MAX, "Something went wrong. Very likely, you are not popping the protected item in the correct thread");
    FiberProtectorCtx().items.Pop(index);
}

void Debug::FiberScopeProtector_Check()
{
    if (FiberProtectorCtx().items.Count()) {
        Debug::PrintLineFmt("Found %u protected items in the fiber that are not destructed in the scope:", FiberProtectorCtx().items.Count());
        
        DebugStacktraceEntry stacktraces[kDebugMaxFiberProtectorStackframes];
        for (const DebugFiberProtectorThreadContext::Item& item : FiberProtectorCtx().items) {
            Debug::PrintLineFmt("\t%s:", item.name);
            if (item.numStackframes) {
                Debug::ResolveStacktrace(item.numStackframes, item.stackframes, stacktraces);
                for (uint16 i = 0; i < item.numStackframes; i++) {
                    Debug::PrintLineFmt("\t\t%s(%u): %s", stacktraces[i].filename, stacktraces[i].line, stacktraces[i].name);
                }
            }
        }

        DEBUG_BREAK();
    }
}
#else
void Debug::FiberScopeProtector_RegisterCallback(DebugFiberScopeProtectorCallback, void*) {}
uint16 Debug::FiberScopeProtector_Push(const char*) { return 0; }
void Debug::FiberScopeProtector_Pop(uint16) {}
void Debug::FiberScopeProtector_Check() {}
#endif  // CONFIG_ENABLE_ASSERT

