#include "Debug.h"

#include <stdarg.h> // va_list/va_start
#include <stdio.h>  // puts

#include "System.h"         // sysAndroidPrintXXX/sysWinDebugger..
#include "StringUtil.h"
#include "Arrays.h"

#ifdef BUILD_UNITY
    #if PLATFORM_WINDOWS
        #include "DebugWin.cpp"
    #elif PLATFORM_ANDROID  
        #include "DebugClang.cpp"
    #endif
#endif 

static bool gDebugCaptureStacktraceForFiberProtector;

void Debug::Print(const char* text)
{
    #if PLATFORM_WINDOWS
        OS::Win32PrintToDebugger(text);
    #elif PLATFORM_ANDROID
        OS::AndroidPrintToLog(SysAndroidLogType::Debug, CONFIG_APP_NAME, text);
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
    gFiberProtector.callbacks.Add(DebugFiberScopeProtectorCallbackPair(callback, userData));
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
    char msg[512];
    
    if (FiberProtectorCtx().items.Count()) {
        strPrintFmt(msg, sizeof(msg), "Found %u protected items in the fiber that are not destructed in the scope:", FiberProtectorCtx().items.Count());
        Debug::Print(msg);
        if constexpr (PLATFORM_WINDOWS) Debug::Print("\n");
        
        DebugStacktraceEntry stacktraces[kDebugMaxFiberProtectorStackframes];
        for (const DebugFiberProtectorThreadContext::Item& item : FiberProtectorCtx().items) {
            strPrintFmt(msg, sizeof(msg), "\t%s:", item.name);
            Debug::Print(msg);
            if constexpr (PLATFORM_WINDOWS) Debug::Print("\n");
            if (item.numStackframes) {
                Debug::ResolveStacktrace(item.numStackframes, item.stackframes, stacktraces);
                for (uint16 i = 0; i < item.numStackframes; i++) {
                    strPrintFmt(msg, sizeof(msg), "\t\t%s(%u): %s", stacktraces[i].filename, stacktraces[i].line, stacktraces[i].name);
                    Debug::Print(msg);
                    if constexpr (PLATFORM_WINDOWS) Debug::Print("\n");
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

