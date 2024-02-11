#define __STDC_WANT_LIB_EXT1__ 1
#include "Base.h"

// MemPro
#if MEMPRO_ENABLED
    #define OVERRIDE_NEW_DELETE
    #define WAIT_FOR_CONNECT true
    #define MEMPRO_BACKTRACE(_stackframes, _maxStackframes, _hashPtr) debugCaptureStacktrace(_stackframes, _maxStackframes, 3, _hashPtr)
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
#elif PLATFORM_ANDROID
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

// Random
// https://github.com/mattiasgustavsson/libs/blob/master/rnd.h
struct RandomContextCtor
{
    RandomContext ctx;

    RandomContextCtor() 
    {
        ctx = randomCreateContext();
    }
};

NO_INLINE static RandomContextCtor& RandomCtx() 
{ 
    static thread_local RandomContextCtor randomCtx;
    return randomCtx; 
}

// Assert
static AssertFailCallback gAssertFailCallback;
static void* gAssertFailUserData;

// Memory
struct MemHeapAllocator final : Allocator 
{
    void* Malloc(size_t size, uint32 align) override;
    void* Realloc(void* ptr, size_t size, uint32 align) override;
    void  Free(void* ptr, uint32 align) override;
    AllocatorType GetType() const override { return AllocatorType::Heap; }
};

struct MemBaseContext
{
    MemFailCallback  memFailFn;
    void* 			 memFailUserdata;
    Allocator*		 defaultAlloc = &heapAlloc;
    MemHeapAllocator heapAlloc;
    bool             enableMemPro;
};

static MemBaseContext gMemBase;

// Convert a randomized uint32_t value to a float value x in the range 0.0f <= x < 1.0f. Contributed by Jonatan Hedborg
PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wstrict-aliasing")
static inline float randomFloatNormalized(uint32 value)
{
    uint32 exponent = 127;
    uint32 mantissa = value >> 9;
    uint32 result = (exponent << 23) | mantissa;
    float fresult = *(float*)(&result);
    return fresult - 1.0f;
}
PRAGMA_DIAGNOSTIC_POP()

INLINE uint64 randomAvalanche64(uint64 h)
{
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccd;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53;
    h ^= h >> 33;
    return h;
}

uint32 randomGenSeed()
{
    return static_cast<uint32>(time(nullptr));
}

RandomContext randomCreateContext(uint32 seed)
{
    RandomContext ctx = {{0, 0}};
    uint64 value = (((uint64)seed) << 1ull) | 1ull;    // make it odd
    value = randomAvalanche64(value);
    ctx.state[0] = 0ull;
    ctx.state[1] = (value << 1ull) | 1ull;
    randomNewUint(&ctx);
    ctx.state[0] += randomAvalanche64(value);
    randomNewUint(&ctx);
    return ctx;
}

uint32 randomNewUint(RandomContext* ctx)
{
    uint64 oldstate = ctx->state[0];
    ctx->state[0] = oldstate * 0x5851f42d4c957f2dull + ctx->state[1];
    uint32 xorshifted = uint32(((oldstate >> 18ull) ^ oldstate) >> 27ull);
    uint32 rot = uint32(oldstate >> 59ull);
    return (xorshifted >> rot) | (xorshifted << ((-(int)rot) & 31));
}

float randomNewFloat(RandomContext* ctx)
{
    return randomFloatNormalized(randomNewUint(ctx));
}

float randomNewFloatInRange(RandomContext* ctx, float _min, float _max)
{
    ASSERT(_min <= _max);
    
    float r = randomNewFloat(ctx);
    return _min + r*(_max - _min);
}

int randomNewIntInRange(RandomContext* ctx, int _min, int _max)
{
    ASSERT(_min <= _max);
    
    uint32 range = static_cast<uint32>(_max - _min) + 1;
    return _min + static_cast<int>(randomNewUint(ctx) % range);
}

uint32 randomNewUint()
{
    return randomNewUint(&RandomCtx().ctx);
}

float randomNewFloat()
{
    return randomNewFloat(&RandomCtx().ctx);
}

float randomNewFloatInRange(float _min, float _max)
{
    return randomNewFloatInRange(&RandomCtx().ctx, _min, _max);
}

int randomNewIntInRange(int _min, int _max)
{
    return randomNewIntInRange(&RandomCtx().ctx, _min, _max);
}

void assertDebugMessage(const char* fmt, ...)
{
    char msgFmt[4972];
    char msg[4972];
    
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgFmt, sizeof(msgFmt), fmt, args);
    va_end(args);

    strcpy_s(msg, sizeof(msg), "[ASSERT_FAIL] ");
    strcat_s(msg, sizeof(msg), msgFmt);
    
    puts(msg);

    #if PLATFORM_WINDOWS
    strcat_s(msg, sizeof(msg), "\n");
    OutputDebugStringA(msg);
    #elif PLATFORM_ANDROID
    __android_log_write(ANDROID_LOG_FATAL, CONFIG_APP_NAME, msg);
    #endif
}

void assertSetFailCallback(AssertFailCallback callback, void* userdata)
{
    gAssertFailCallback = callback;
    gAssertFailUserData = userdata;
}

void assertRunFailCallback()
{
    if (gAssertFailCallback)
        gAssertFailCallback(gAssertFailUserData);
}

#if PLATFORM_WINDOWS
    #define aligned_malloc(_align, _size) _aligned_malloc(_size, _align)
    #define aligned_realloc(_ptr, _align, _size) _aligned_realloc(_ptr, _size, _align)
    #define aligned_free(_ptr) _aligned_free(_ptr)
#else
    INLINE void* aligned_malloc(uint32 align, size_t size);
    INLINE void* aligned_realloc(void*, uint32, size_t);
    INLINE void  aligned_free(void* ptr);
#endif

void memSetFailCallback(MemFailCallback callback, void* userdata)
{
    gMemBase.memFailFn = callback;
    gMemBase.memFailUserdata = userdata;
}

void memRunFailCallback()
{
    if (gMemBase.memFailFn) {
        gMemBase.memFailFn(gMemBase.memFailUserdata);
    }
}

void* memAlignPointer(void* ptr, size_t extra, uint32 align)
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

Allocator* memDefaultAlloc()
{
    return static_cast<Allocator*>(&gMemBase.heapAlloc);
}

void memSetDefaultAlloc(Allocator* alloc)
{
    gMemBase.defaultAlloc = alloc != nullptr ? alloc : &gMemBase.heapAlloc;
}

void memEnableMemPro(bool enable)
{
    #if MEMPRO_ENABLED
    gMemBase.enableMemPro = enable;
    #else
    UNUSED(enable);
    #endif
}

bool memIsMemProEnabled()
{
    #if MEMPRO_ENABLED
    return gMemBase.enableMemPro;
    #else
    return false;
    #endif
}

void memTrackMalloc([[maybe_unused]] void* ptr, [[maybe_unused]] size_t size)
{
    if constexpr (MEMPRO_ENABLED) {
        if (gMemBase.enableMemPro)
            MEMPRO_TRACK_ALLOC(ptr, size);
    }    
}

void memTrackFree([[maybe_unused]] void* ptr)
{
    if constexpr (MEMPRO_ENABLED) {
        if (gMemBase.enableMemPro)
            MEMPRO_TRACK_FREE(ptr);
    }
}

void memTrackRealloc([[maybe_unused]] void* oldPtr, [[maybe_unused]] void* ptr, [[maybe_unused]] size_t size)
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
        MEMORY_FAIL();
        return nullptr;
    }

    TracyCAlloc(ptr, size);        

    memTrackMalloc(ptr, size);
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
        MEMORY_FAIL();
        return nullptr;
    }
    
    TracyCRealloc(freePtr, ptr, size);
    memTrackRealloc(freePtr, ptr, size);
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
        memTrackFree(ptr);

        if constexpr (MEMPRO_ENABLED) {
            if (gMemBase.enableMemPro) 
                MEMPRO_TRACK_FREE(ptr);
        }
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
    uint8* aligned = (uint8*)memAlignPointer(ptr, sizeof(uint32), align);
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
        uint8* newAligned = (uint8*)memAlignPointer(ptr, sizeof(uint32), align);
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
#if PLATFORM_ANDROID
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
