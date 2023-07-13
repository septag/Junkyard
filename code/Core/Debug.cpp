#include "Base.h"

#ifdef BUILD_UNITY
    #if PLATFORM_WINDOWS
        #include "DebugWin.cpp"
    #elif PLATFORM_ANDROID  
        #include "DebugAndroid.cpp"
    #endif
#endif 

#include <stdarg.h> // va_list/va_start
#include <stdio.h>  // puts

#include "String.h"
#include "System.h"         // sysAndroidPrintXXX/sysWinDebugger..
#include "Buffers.h"
#include "Log.h"

static bool gDebugCaptureStacktraceForFiberProtector;

void debugBreakMessage(const char* fmt, ...)
{
    char msgFmt[4972];
    char msg[4972];
    
    va_list args;
    va_start(args, fmt);
    strPrintFmtArgs(msgFmt, sizeof(msgFmt), fmt, args);
    va_end(args);

    strConcat(strCopy(msg, sizeof(msg), "[ASSERT_FAIL] "), sizeof(msg), msgFmt);
    
    puts(msg);

    #if PLATFORM_WINDOWS
        strConcat(msg, sizeof(msg), "\n");
        sysWin32PrintToDebugger(msg);
    #elif PLATFORM_ANDROID
        sysAndroidPrintToLog(SysAndroidLogType::Debug, logGetAppNameAndroid(), msg);
    #elif PLATFORM_APPLE
        sysApplePrintToLog(msg);
    #else
        debugPrint(msg);
    #endif
}

void debugPrint(const char* text)
{
    #if PLATFORM_WINDOWS
        sysWin32PrintToDebugger(text);
    #elif PLATFORM_ANDROID
        sysAndroidPrintToLog(SysAndroidLogType::Debug, logGetAppNameAndroid(), text);
    #elif PLATFORM_APPLE
        sysApplePrintToLog(text);
    #else
        puts(text);
    #endif
}

void debugSetCaptureStacktraceForFiberProtector(bool capture)
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

void debugFiberScopeProtector_RegisterCallback(DebugFiberScopeProtectorCallback callback, void* userData)
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

uint16 debugFiberScopeProtector_Push(const char* name)
{
    if (debugFiberScopeProtector_IsInFiber()) {
        ASSERT(name);
        DebugFiberProtectorThreadContext::Item* item = FiberProtectorCtx().items.Push();
        memset(item, 0x0, sizeof(*item));
        item->name = name;
        if (gDebugCaptureStacktraceForFiberProtector) 
            item->numStackframes = debugCaptureStacktrace(item->stackframes, kDebugMaxFiberProtectorStackframes, 2);
        uint16 id = ++FiberProtectorCtx().idGen;
        if (id == 0)
            id = 1;
        item->id = id;
        return id;
    }
    return 0;
}

void debugFiberScopeProtector_Pop(uint16 id)
{
    if (id == 0)
        return;
    
    ASSERT_MSG(debugFiberScopeProtector_IsInFiber(), "Item was pushed in the fiber, but not popping in any fibers");
    ASSERT(FiberProtectorCtx().items.Count());

    uint32 index = FiberProtectorCtx().items.FindIf([id](const DebugFiberProtectorThreadContext::Item& item) { return item.id == id; });
    ASSERT_MSG(index != UINT32_MAX, "Something went wrong. Very likely, you are not popping the protected item in the correct thread");
    FiberProtectorCtx().items.Pop(index);
}

void debugFiberScopeProtector_Check()
{
    if (FiberProtectorCtx().items.Count()) {
        debugBreakMessage("Found %u protected items in the fiber that are not destructed in the scope:", FiberProtectorCtx().items.Count());
        
        DebugStacktraceEntry stacktraces[kDebugMaxFiberProtectorStackframes];
        for (const DebugFiberProtectorThreadContext::Item& item : FiberProtectorCtx().items) {
            debugBreakMessage("\t%s:", item.name);
            if (item.numStackframes) {
                debugResolveStacktrace(item.numStackframes, item.stackframes, stacktraces);
                for (uint16 i = 0; i < item.numStackframes; i++)
                    debugBreakMessage("\t\t%s(%u): %s", stacktraces[i].filename, stacktraces[i].line, stacktraces[i].name);
            }
        }

        DEBUG_BREAK();
    }
}
#else
void debugFiberScopeProtector_RegisterCallback(DebugFiberScopeProtectorCallback, void*) {}
uint16 debugFiberScopeProtector_Push(const char*) { return 0; }
void debugFiberScopeProtector_Pop(uint16) {}
void debugFiberScopeProtector_Check() {}
#endif  // CONFIG_ENABLE_ASSERT

