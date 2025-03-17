#define __STDC_WANT_LIB_EXT1__ 1
#include "Base.h"

// MemPro
#if MEMPRO_ENABLED
    #define OVERRIDE_NEW_DELETE
    #define WAIT_FOR_CONNECT true
    #define MEMPRO_BACKTRACE(_stackframes, _maxStackframes, _hashPtr) Debug::CaptureStacktrace(_stackframes, _maxStackframes, 3, _hashPtr)
    #include "External/mempro/MemPro.cpp"
    #define MEMPRO_TRACK_REALLOC(oldPtr, ptr, size) do { if (oldPtr)  { MEMPRO_TRACK_FREE(oldPtr); } MEMPRO_TRACK_ALLOC(ptr, size);} while(0)
#else
    #define MEMPRO_TRACK_ALLOC(ptr, size) 
    #define MEMPRO_TRACK_REALLOC(oldPtr, ptr, size)
    #define MEMPRO_TRACK_FREE(ptr)
#endif

#if PLATFORM_APPLE
    #define strcpy_s(dest, size, src)  strlcpy(dest, src, size)
    #define strcat_s(dest, size, src)  strlcat(dest, src, size)
#elif PLATFORM_ANDROID || PLATFORM_LINUX
    // Ensure bounds-checking interface for string functions (string.h)
    // TODO: test this on platforms other than android. because at least android doesn't seem to have strcpy_s/strcat_s
    static size_t strcpy_s(char *dest, size_t size, const char *src);
    static size_t strcat_s(char *dst, size_t size, const char *src);
#elif PLATFORM_WINDOWS
    #if !MEMPRO_ENABLED
    // ugh! just to get rid of "System.h" dependency here
    extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char* lpOutputString);
    #endif
#else
    #define __STDC_WANT_LIB_EXT1__ 1
#endif

#include <time.h>   // time
#include <stdio.h>  // puts
#include <string.h>
#include <stdarg.h>

#if PLATFORM_ANDROID
    #include <android/log.h>
#endif

#if PLATFORM_POSIX
    #include <stdlib.h>
#else
    #include <malloc.h>
#endif

#include "TracyHelper.h"
#include "System.h"

//    ██████╗  █████╗ ███╗   ██╗██████╗  ██████╗ ███╗   ███╗
//    ██╔══██╗██╔══██╗████╗  ██║██╔══██╗██╔═══██╗████╗ ████║
//    ██████╔╝███████║██╔██╗ ██║██║  ██║██║   ██║██╔████╔██║
//    ██╔══██╗██╔══██║██║╚██╗██║██║  ██║██║   ██║██║╚██╔╝██║
//    ██║  ██║██║  ██║██║ ╚████║██████╔╝╚██████╔╝██║ ╚═╝ ██║
//    ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═══╝╚═════╝  ╚═════╝ ╚═╝     ╚═╝
// https://github.com/mattiasgustavsson/libs/blob/master/rnd.h
struct RandomContextCtor
{
    RandomContext ctx;

    RandomContextCtor() 
    {
        ctx = Random::CreateContext();
    }
};

namespace Random
{

NO_INLINE static RandomContextCtor& RandomCtx() 
{ 
    static thread_local RandomContextCtor randomCtx;
    return randomCtx; 
}

// Convert a randomized uint32_t value to a float value x in the range 0.0f <= x < 1.0f. Contributed by Jonatan Hedborg
PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wstrict-aliasing")
static inline float FloatNormalized(uint32 value)
{
    uint32 exponent = 127;
    uint32 mantissa = value >> 9;
    uint32 result = (exponent << 23) | mantissa;
    float fresult = *(float*)(&result);
    return fresult - 1.0f;
}
PRAGMA_DIAGNOSTIC_POP()

INLINE uint64 Avalanche64(uint64 h)
{
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccd;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53;
    h ^= h >> 33;
    return h;
}

uint32 Seed()
{
    return static_cast<uint32>(time(nullptr));
}

RandomContext CreateContext(uint32 seed)
{
    RandomContext ctx = {{0, 0}};
    uint64 value = (((uint64)seed) << 1ull) | 1ull;    // make it odd
    value = Avalanche64(value);
    ctx.state[0] = 0ull;
    ctx.state[1] = (value << 1ull) | 1ull;
    Int(&ctx);
    ctx.state[0] += Avalanche64(value);
    Int(&ctx);
    return ctx;
}

uint32 Int(RandomContext* ctx)
{
    uint64 oldstate = ctx->state[0];
    ctx->state[0] = oldstate * 0x5851f42d4c957f2dull + ctx->state[1];
    uint32 xorshifted = uint32(((oldstate >> 18ull) ^ oldstate) >> 27ull);
    uint32 rot = uint32(oldstate >> 59ull);
    return (xorshifted >> rot) | (xorshifted << ((-(int)rot) & 31));
}

float Float(RandomContext* ctx)
{
    return FloatNormalized(Int(ctx));
}

float Float(RandomContext* ctx, float _min, float _max)
{
    ASSERT(_min <= _max);
    
    float r = Float(ctx);
    return _min + r*(_max - _min);
}

int Int(RandomContext* ctx, int _min, int _max)
{
    ASSERT(_min <= _max);
    
    uint32 range = static_cast<uint32>(_max - _min) + 1;
    return _min + static_cast<int>(Int(ctx) % range);
}

uint32 Int()
{
    return Int(&RandomCtx().ctx);
}

float Float()
{
    return Float(&RandomCtx().ctx);
}

float Float(float _min, float _max)
{
    return Float(&RandomCtx().ctx, _min, _max);
}

int Int(int _min, int _max)
{
    return Int(&RandomCtx().ctx, _min, _max);
}
} // Random

//     █████╗ ███████╗███████╗███████╗██████╗ ████████╗
//    ██╔══██╗██╔════╝██╔════╝██╔════╝██╔══██╗╚══██╔══╝
//    ███████║███████╗███████╗█████╗  ██████╔╝   ██║   
//    ██╔══██║╚════██║╚════██║██╔══╝  ██╔══██╗   ██║   
//    ██║  ██║███████║███████║███████╗██║  ██║   ██║   
//    ╚═╝  ╚═╝╚══════╝╚══════╝╚══════╝╚═╝  ╚═╝   ╚═╝   
static AssertFailCallback gAssertFailCallback;
static void* gAssertFailUserData;

void Assert::DebugMessage(const char* fmt, ...)
{
    char msgFmt[4972];
    char msg[4972];
    
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgFmt, sizeof(msgFmt), fmt, args);
    va_end(args);

    const char* closeBracket = "] ";
    char threadName[32];
    Thread::GetCurrentThreadName(threadName, sizeof(threadName));
    strcpy_s(msg, sizeof(msg), "[ASSERT_FAIL: ");
    strcat_s(msg, sizeof(msg), threadName);
    strcat_s(msg, sizeof(msg), closeBracket);
    strcat_s(msg, sizeof(msg), msgFmt);
    
    puts(msg);

    #if PLATFORM_WINDOWS
    strcat_s(msg, sizeof(msg), "\n");
    OutputDebugStringA(msg);
    #elif PLATFORM_ANDROID
    __android_log_write(ANDROID_LOG_FATAL, CONFIG_APP_NAME, msg);
    #endif

    // TODO: Add more reports + callstack
}

void Assert::SetFailCallback(AssertFailCallback callback, void* userdata)
{
    gAssertFailCallback = callback;
    gAssertFailUserData = userdata;
}

void Assert::RunFailCallback()
{
    if (gAssertFailCallback)
        gAssertFailCallback(gAssertFailUserData);
}

//     █████╗ ██╗     ██╗      ██████╗  ██████╗ █████╗ ████████╗██╗ ██████╗ ███╗   ██╗
//    ██╔══██╗██║     ██║     ██╔═══██╗██╔════╝██╔══██╗╚══██╔══╝██║██╔═══██╗████╗  ██║
//    ███████║██║     ██║     ██║   ██║██║     ███████║   ██║   ██║██║   ██║██╔██╗ ██║
//    ██╔══██║██║     ██║     ██║   ██║██║     ██╔══██║   ██║   ██║██║   ██║██║╚██╗██║
//    ██║  ██║███████╗███████╗╚██████╔╝╚██████╗██║  ██║   ██║   ██║╚██████╔╝██║ ╚████║
//    ╚═╝  ╚═╝╚══════╝╚══════╝ ╚═════╝  ╚═════╝╚═╝  ╚═╝   ╚═╝   ╚═╝ ╚═════╝ ╚═╝  ╚═══╝
struct MemHeapAllocator final : MemAllocator 
{
    void* Malloc(size_t size, uint32 align) override;
    void* Realloc(void* ptr, size_t size, uint32 align) override;
    void  Free(void* ptr, uint32 align) override;
    MemAllocatorType GetType() const override { return MemAllocatorType::Heap; }
};

struct MemBaseContext
{
    MemFailCallback  memFailFn;
    void* 			 memFailUserdata;
    MemAllocator*		 defaultAlloc = &heapAlloc;
    MemHeapAllocator heapAlloc;
    bool             enableMemPro;
};

static MemBaseContext gMemBase;

#if PLATFORM_WINDOWS
    #define aligned_malloc(_align, _size) _aligned_malloc(_size, _align)
    #define aligned_realloc(_ptr, _align, _size) _aligned_realloc(_ptr, _size, _align)
    #define aligned_free(_ptr) _aligned_free(_ptr)
#else
    INLINE void* aligned_malloc(uint32 align, size_t size);
    INLINE void* aligned_realloc(void*, uint32, size_t);
    INLINE void  aligned_free(void* ptr);
#endif

void Mem::SetFailCallback(MemFailCallback callback, void* userdata)
{
    gMemBase.memFailFn = callback;
    gMemBase.memFailUserdata = userdata;
}

void Mem::RunFailCallback()
{
    if (gMemBase.memFailFn) {
        gMemBase.memFailFn(gMemBase.memFailUserdata);
    }
}

void* Mem::AlignPointer(void* ptr, size_t extra, uint32 align)
{
    union {
        void* ptr;
        uintptr_t addr;
    } un;
    un.ptr = ptr;
    uintptr_t unaligned = un.addr + extra;    // space for header
    uintptr_t aligned = AlignValue<uintptr_t>(unaligned, align);
    un.addr = aligned;
    return un.ptr;
}

MemAllocator* Mem::GetDefaultAlloc()
{
    return static_cast<MemAllocator*>(&gMemBase.heapAlloc);
}

void Mem::SetDefaultAlloc(MemAllocator* alloc)
{
    gMemBase.defaultAlloc = alloc != nullptr ? alloc : &gMemBase.heapAlloc;
}

void Mem::EnableMemPro(bool enable)
{
    #if MEMPRO_ENABLED
    gMemBase.enableMemPro = enable;
    #else
    UNUSED(enable);
    #endif
}

bool Mem::IsMemProEnabled()
{
    #if MEMPRO_ENABLED
    return gMemBase.enableMemPro;
    #else
    return false;
    #endif
}

void Mem::TrackMalloc([[maybe_unused]] void* ptr, [[maybe_unused]] size_t size)
{
    if constexpr (MEMPRO_ENABLED) {
        if (gMemBase.enableMemPro)
            MEMPRO_TRACK_ALLOC(ptr, size);
    }    
}

void Mem::TrackFree([[maybe_unused]] void* ptr)
{
    if constexpr (MEMPRO_ENABLED) {
        if (gMemBase.enableMemPro)
            MEMPRO_TRACK_FREE(ptr);
    }
}

void Mem::TrackRealloc([[maybe_unused]] void* oldPtr, [[maybe_unused]] void* ptr, [[maybe_unused]] size_t size)
{
    if constexpr (MEMPRO_ENABLED) {
        if (gMemBase.enableMemPro)
            MEMPRO_TRACK_REALLOC(oldPtr, ptr, size);
    }
}

inline void* MemHeapAllocator::Malloc(size_t size, uint32 align)
{
    void* ptr;
    if (align <= CONFIG_MACHINE_ALIGNMENT) {
        ptr = malloc(size);
        ASSERT((uintptr_t(ptr) % CONFIG_MACHINE_ALIGNMENT) == 0);   // Validate machine alignment with malloc
    }
    else {
        align = Max(align, CONFIG_MACHINE_ALIGNMENT);
        ptr = aligned_malloc(align, size);
    }
    if (!ptr) {
        MEM_FAIL();
        return nullptr;
    }

    TracyCAlloc(ptr, size);        

    Mem::TrackMalloc(ptr, size);
    return ptr;
}
    
inline void* MemHeapAllocator::Realloc(void* ptr, size_t size, uint32 align)
{
    [[maybe_unused]] void* freePtr = ptr;

    if (align <= CONFIG_MACHINE_ALIGNMENT) {
        ptr = realloc(ptr, size);
    }
    else {
        align = Max(align, CONFIG_MACHINE_ALIGNMENT);
        ptr = aligned_realloc(ptr, align, size);
    }
    
    if (!ptr) {
        MEM_FAIL();
        return nullptr;
    }
    
    TracyCRealloc(freePtr, ptr, size);
    Mem::TrackRealloc(freePtr, ptr, size);
    return ptr;
}
    
inline void MemHeapAllocator::Free(void* ptr, uint32 align)
{
    if (ptr != nullptr) {
        if (align <= CONFIG_MACHINE_ALIGNMENT) {
            free(ptr);
        }
        else {
            aligned_free(ptr);
        }
    
        TracyCFree(ptr);
        Mem::TrackFree(ptr);
    }
}

//----------------------------------------------------------------------------------------------------------------------
// Custom implementation for aligned allocations
#if !PLATFORM_WINDOWS
INLINE void* aligned_malloc(uint32 align, size_t size)
{
    ASSERT(align >= CONFIG_MACHINE_ALIGNMENT);
    
    size_t total = size + align + sizeof(uint32);
    uint8* ptr = (uint8*)malloc(total);
    if (!ptr)
        return nullptr;
    uint8* aligned = (uint8*)Mem::AlignPointer(ptr, sizeof(uint32), align);
    uint32* header = (uint32*)aligned - 1;
    *header = PtrToInt<uint32>((void*)(aligned - ptr));  // Save the offset needed to move back from aligned pointer
    return aligned;
}

INLINE void* aligned_realloc(void* ptr, uint32 align, size_t size)
{
    ASSERT(align >= CONFIG_MACHINE_ALIGNMENT);

    if (ptr) {
        uint8* aligned = (uint8*)ptr;
        uint32 offset = *((uint32*)aligned - 1);
        ptr = aligned - offset;

        size_t total = size + align + sizeof(uint32);
        ptr = realloc(ptr, total);
        if (!ptr)
            return nullptr;
        uint8* newAligned = (uint8*)Mem::AlignPointer(ptr, sizeof(uint32), align);
        if (newAligned == aligned)
            return aligned;

        aligned = (uint8*)ptr + offset;
        memmove(newAligned, aligned, size);
        uint32* header = (uint32*)newAligned - 1;
        *header = PtrToInt<uint32>((void*)(newAligned - (uint8*)ptr));
        return newAligned;
    }
    else {
        return aligned_malloc(align, size);
    }
}

INLINE void aligned_free(void* ptr)
{
    if (ptr) {
        uint8* aligned = (uint8*)ptr;
        uint32* header = (uint32*)aligned - 1;
        ptr = aligned - *header;
        free(ptr);
    }
}
#endif  // !PLATFORM_WINDOWS

//----------------------------------------------------------------------------------------------------------------------
// Custom implementation for strcpy_s and strcat_s
#if PLATFORM_ANDROID || PLATFORM_LINUX
// https://github.com/git/git/blob/master/compat/strlcpy.c
static size_t strcpy_s(char *dest, size_t size, const char *src)
{
    size_t ret = strlen(src);

    if (size) {
        size_t len = (ret >= size) ? size - 1 : ret;
        memcpy(dest, src, len);
        dest[len] = '\0';
    }
    return ret;
}

// https://codereview.stackexchange.com/questions/147234/c-strlcat-implementation
static size_t strcat_s(char *dst, size_t size, const char *src)
{
    size_t  len;
    size_t  slen;

    len = 0;
    slen = strlen(src);
    while (*dst && size > 0) // added @JS1 edit
    {
        dst++;
        len++;
        size--;
    }
    while (*src && size-- > 1) //added @JS1 edit
        *dst++ = *src++;
    if (size == 1 || *src == 0) // **VERY IMPORTANT:** read update below
        *dst = '\0';
    return (slen + len);
}
#endif  // PLATFORM_ANDROID
