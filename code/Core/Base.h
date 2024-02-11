#pragma once

//
// Base: This file is pretty much included in every other file. 
//       So keep this as light as possible and with minimum amount of includes
//       Most functions and macros here are for potability and usually one/two-liner inlines
// Note: This header also includes Debug.h, which includes ASSERT macros and basic debugging functions
//       Also includes "..\Config.h" which is user defined configurations
//
//  Macros:
//      - ARCH_32BIT/ARCH_64BIT: 32 or 64 bit architecture. ARCH_NAME is the string representation
//      - COMPILER_CLANG/GCC/MSVC: Compiler. COMPILER_[COMPILER]_VERSION sets the version for the compiler
//      - CPU_ARM/X86: Cpu architecture for the source code. CPU_NAME is the string representation
//      - PLATFORM_ANDROID/IOS/LINUX/OSX/WINDOWS: source code platform/OS. PLATFORM_NAME is the string representation 
//      - UNUSED(x): removes the 'unused' compiler warning from variables
//      - STRINGIZE(x): Turns an expression into string
//      - CONCAT(a, b): Concats the two expressions with preprocessor
//      - API: Function attribute for pulibc APIs
//      - INVALID_INDEX: most functions return invalid index if nothing is found (Array::FindIf). This equals UINT32_MAX
//      - IS_ASAN_ENABLED: Equals t, if address sanitizer is enabled. CL(/fsanitize=address), clang(-fsanitize=address)
//      - NO_ASAN: Function attribute that omits from address sanitizer code
//      - HAS_INCLUDE(include): Preprocessor checks if the file exists
//      - FORCE_INLINE: Function attribute that forces inlining
//      - FUNCTION: Pretty representation of function name (string)
//      - NO_INLINE: Prevents inlining by the compiler
//      - NO_OPT_BEGIN/END: Begins/End optimization for a specific function. Put Begin/End around the function definition
//      - INLINE: Inline attribute for functions. Compiler usually decides on itself, but this is useful for debugging.
//                Setting CONFIG_FORCE_INLINE_DEBUG=1 forces all INLINE keywords to not be inlined, BUT leaves FORCE_INLINE 
//                to still be inlined. (in CL, you should use this with /Ob1 flag)
//      - Ignoring warnings with Pragma. The Portable way:
//        Warnings are different for each compiler of course. so for a piece of code that you need to omit specific warnings:
//          - Add PRAGMA_DIAGNOSTIC_PUSH() to the beginning
//          - Put any msvc warning number in PRAGMA_DIAGNOSTIC_IGNORED_MSVC(Number)
//          - Put any clang/gcc warniing as a string in PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("warning")
//          - Add PRAGMA_DIAGNOSTIC_POP() to the end
//      - ENABLE_BITMASK(enum_class): Adds required bitwise operators for `enum class` types
//      - CACHE_LINE_SIZE: Size of a line of CPU cache, in bytes
//      - DEBUG_BREAK: Breaks the program with INT interrupt
//      - ASSERT(e): Breaks the program if the expression is false (only enabled with _DEBUG/CONFIG_ENABLE_ASSERT=1)
//      - ASSERT_MSG(e, comment_fmt, ...): Breaks the program and throws a formatted comment if the expression is false (only enabled with _DEBUG/CONFIG_ENABLE_ASSERT=1)
//      - ASSERT_ALWAYS(e, comment_fmt, ...): Same as ASSERT_MSG, but is always enabled regardless of CONFIG_ENABLE_ASSERT
//
//  Utility Functions: 
//      - Max<T>(a, b): Calculates the maximum between two values
//      - Min<T>(a, b): Calculates the minimum between two values
//      - Clamp<T>(v, a, b): Clamps the value (v) between two values (a, b)
//      - Swap<T>(&a, &b): Swaps a with b
//      - CountOf<T>(a): Returns the count of items in an static array (doesn't work on raw pointers!)
//      - AlignValue<T>(value, align): Aligns the value's upper bound to the align argument
//      - DivCeil<T>(value, divider): Returns the upper bound of the division of value with the divider. 
//      - MakeFourCC(a, b, c, d): Makes fourcc code from 4 characters
//      - PtrToInt<T>(void*): Turns pointer into an opaque variable
//      - IntToPtr<T>(T i): Turns integers into pointer
//      - IndexToId(index): Turns index to Id, mainly for verbosity. Ids start from 1 (=0 invalid) and indexes start from 0 obviously
//      - IdToIndex(id): Turns Id to index, mainly for verbosity. 
//  
//  Helper classes:
//      - Pair<First, Second>: Makes a struct containing pair of values with any type 
//      - RelativePtr<T>: Defines a relative pointer of type T (-> gets you T*). 
//                        Instead of the actual pointer, stores a 32bit offset from the RelativePtr object to the pointer you assign to it
//                        It is very powerful, but use with care. Because allocate memory should always lineary placed in memory after the RelativePtr
//      - AtomicLock: Used for SpinLocks. See Atomic.h for the actual locking functions. Placed here to reduce Atomic.h inclusion overhead
//      - Span: A pair of templated pointer/size type. Used for holding a range of data in memory. 
//
//  Random Generator: Base has a default PCG random number generator, with two approaches:
//      - Context based random gen: You need to have a RandomContext stored and created. Gives more flexibility but needs bookkeeping
//        randomGenSeed()/randomCreateContext()/randomNewUint(ctx)/randomNewFloat(ctx)/randomNewFloatInRange(ctx)/randomIntInRange(ctx)
//      - Context-free random gen: For every thread, there is a RandomContext automatically generated and can be used without any bookkeeping
//        Notice, the functions below, does not need context and thus context creation. But cannot be seeded manually
//        randomNewUint()/randomNewFloat()/randomNewFloatInRange()/randomIntInRange()
//
//  Base memory abstraction: Includes the main interface "Allocator" for interfacing with memory allocations
//

#include <stdint.h>     // uint32, int64_t, etc..
#include <stdbool.h>    // bool
#include <stddef.h>     // NULL, size_t, offsetof
#include <memory.h>		// memset, memcpy
#if PLATFORM_APPLE
#include <string.h>
#endif

#include "Config.h"

// Cpu Architecture  
#define ARCH_32BIT 0
#define ARCH_64BIT 0
#define ARCH_PTRSIZE 0

// Compiler
#define COMPILER_CLANG 0
#define COMPILER_CLANG_ANALYZER 0
#define COMPILER_CLANG_CL 0
#define COMPILER_GCC 0
#define COMPILER_MSVC 0

// CPU
#define CPU_ARM 0
#define CPU_X86 0

// Platform
#define PLATFORM_ANDROID 0
#define PLATFORM_IOS 0
#define PLATFORM_LINUX 0
#define PLATFORM_OSX 0
#define PLATFORM_WINDOWS 0

// useful macros
#define __STRINGIZE__(_x) #_x
#define STRINGIZE(_x) __STRINGIZE__(_x)

#define __CONCAT__(a, b) a##b
#define CONCAT(a, b) __CONCAT__(a, b)

// http://sourceforge.net/apps/mediawiki/predef/index.php?title=Compilers
#if defined(__clang__)
    // clang defines __GN_ or _MSC_VER
    #undef COMPILER_CLANG
    #define COMPILER_CLANG 1
    #define COMPILER_CLANG_VERSION (__clang_major__ * 10000 + __clang_minor__ * 100 + __clang_patchlevel__)
    #if defined(__clang_analyzer__)
        #undef COMPILER_CLANG_ANALYZER
        #define COMPILER_CLANG_ANALYZER 1
    #endif    // defined(__clang_analyzer__)
    #if defined(_MSC_VER)
        #undef COMPILER_MSVC
        #define COMPILER_MSVC 1
        #undef COMPILER_CLANG_CL
        #define COMPILER_CLANG_CL_VERSION COMPILER_CLANG
    #endif
#elif defined(_MSC_VER)
    #undef COMPILER_MSVC
    #define COMPILER_MSVC 1
    #define COMPILER_MSVC_VERSION _MSC_VER
#elif defined(__GNUC__)
    #undef COMPILER_GCC
    #define COMPILER_GCC 1
    #define COMPILER_GCC_VERIONS (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#else
    #error "COMPILER_* is not defined!"
#endif    //

// http://sourceforge.net/apps/mediawiki/predef/index.php?title=Architectures
#if defined(__arm__) || defined(__aarch64__) || defined(_M_ARM)
    #undef CPU_ARM
    #define CPU_ARM 1
    #define CACHE_LINE_SIZE 64u
#elif defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
    #undef CPU_X86
    #define CPU_X86 1
    #define CACHE_LINE_SIZE 64u
#else
    #error "Cpu not supported"
#endif    //

#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(__64BIT__) || \
    defined(__mips64) || defined(__powerpc64__) || defined(__ppc64__) || defined(__LP64__)
    #undef ARCH_64BIT
    #define ARCH_64BIT 1
    #undef ARCH_PTRSIZE
    #define ARCH_PTRSIZE 8
#else
    #undef ARCH_32BIT
    #define ARCH_32BIT 1
    #undef ARCH_PTRSIZE
    #define ARCH_PTRSIZE 4
#endif    //

// http://sourceforge.net/apps/mediawiki/predef/index.php?title=Operating_Systems
#if defined(__ANDROID__) || defined(__android__) || defined(ANDROID) || defined(__ANDROID_API__)
    // Android compiler defines __linux__
    #include <sys/cdefs.h>    // Defines __BIONIC__ and includes android/api-level.h
    #undef PLATFORM_ANDROID
    #define PLATFORM_ANDROID 1
    #define  PLATFORM_ANDROID_VERSION __ANDROID__
#elif defined(_WIN32) || defined(_WIN64)
    // http://msdn.microsoft.com/en-us/library/6sehtctf.aspx
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif    // NOMINMAX
    //  If _USING_V110_SDK71_ is defined it means we are using the v110_xp or v120_xp toolset.
    #if defined(_MSC_VER) && (_MSC_VER >= 1700) && (!_USING_V110_SDK71_)
        #include <winapifamily.h>
    #endif    // defined(_MSC_VER) && (_MSC_VER >= 1700) && (!_USING_V110_SDK71_)
    #undef PLATFORM_WINDOWS
    #if !defined(WINVER) && !defined(_WIN32_WINNT)
        #if ARCH_64BIT
            // When building 64-bit target Win10 and above.
            #define WINVER 0x0a00
            #define _WIN32_WINNT 0x0a00
        #else
            // Windows Server 2003 with SP1, Windows XP with SP2 and above
            #define WINVER 0x0502
            #define _WIN32_WINNT 0x0502
        #endif    // ARCH_64BIT
    #endif        // !defined(WINVER) && !defined(_WIN32_WINNT)
    #define PLATFORM_WINDOWS 1
    #define PLATFORM_WINDOWS_VERSION WINVER
#elif defined(__linux__)
    #undef PLATFORM_LINUX
    #define PLATFORM_LINUX 1
#elif defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__) || defined(__ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__)
    #undef PLATFORM_IOS
    #define PLATFORM_IOS 1
#elif defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
    #undef PLATFORM_OSX
    #define PLATFORM_OSX __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__
#endif    //

#if COMPILER_GCC
    #define COMPILER_NAME \
        "GCC " STRINGIZE(__GN_) "." STRINGIZE(__GNMINOR__) "." STRINGIZE(__GNPATCHLEVEL__)
#elif COMPILER_CLANG
    #define COMPILER_NAME \
        "Clang " STRINGIZE(__clang_major__) "." STRINGIZE(__clang_minor__) "." STRINGIZE(__clang_patchlevel__)
#elif COMPILER_MSVC
    #if COMPILER_MSVC_VERSION >= 1930
        #define COMPILER_NAME "MSVC 17 (" STRINGIZE(COMPILER_MSVC_VERSION)  ")"
    #elif COMPILER_MSVC_VERSION >= 1920      // Visual Studio 2019
        #define COMPILER_NAME "MSVC 16 (" STRINGIZE(COMPILER_MSVC_VERSION)  ")"
    #elif COMPILER_MSVC_VERSION >= 1910    // Visual Studio 2017
        #define COMPILER_NAME "MSVC 15"
    #elif COMPILER_MSVC_VERSION >= 1900    // Visual Studio 2015
        #define COMPILER_NAME "MSVC 14"
    #elif COMPILER_MSVC_VERSION >= 1800    // Visual Studio 2013
        #define COMPILER_NAME "MSVC 12"
    #elif COMPILER_MSVC_VERSION >= 1700    // Visual Studio 2012
        #define COMPILER_NAME "MSVC 11"
    #elif COMPILER_MSVC_VERSION >= 1600    // Visual Studio 2010
        #define COMPILER_NAME "MSVC 10"
    #elif COMPILER_MSVC_VERSION >= 1500    // Visual Studio 2008
        #define COMPILER_NAME "MSVC 9"
    #else
        #define COMPILER_NAME "MSVC"
    #endif    
#endif        // COMPILER_NAME

#if PLATFORM_ANDROID
    #define PLATFORM_NAME "Android " STRINGIZE(PLATFORM_ANDROID)
#elif PLATFORM_IOS
    #define PLATFORM_NAME "iOS"
#elif PLATFORM_LINUX
    #define PLATFORM_NAME "Linux"
#elif PLATFORM_NX
    #define PLATFORM_NAME "NX"
#elif PLATFORM_OSX
    #define PLATFORM_NAME "OSX"
#elif PLATFORM_WINDOWS
    #define PLATFORM_NAME "Windows"
#else
    #error "Unknown PLATFORM!"
#endif    // PLATFORM_

#define PLATFORM_APPLE (0 || PLATFORM_IOS || PLATFORM_OSX)

#if CPU_ARM
    #define CPU_NAME "ARM"
#elif CPU_X86
    #define CPU_NAME "x86"
#endif    // CPU_

#if ARCH_32BIT
    #define ARCH_NAME "32-bit"
#elif ARCH_64BIT
    #define ARCH_NAME "64-bit"
#endif    // ARCH_

#if defined(__has_feature)
    #define CLANG_HAS_FEATURE(_x) __has_feature(_x)
#else
    #define CLANG_HAS_FEATURE(_x) 0
#endif    // defined(__has_feature)

#if defined(__has_extension)
    #define CLANG_HAS_EXTENSION(_x) __has_extension(_x)
#else
    #define CLANG_HAS_EXTENSION(_x) 0
#endif    // defined(__has_extension)

#if COMPILER_GCC || COMPILER_CLANG
    #define FORCE_INLINE static inline __attribute__((__always_inline__))
    #define FUNCTION __PRETTY_FUNCTION__
    #define NO_OPT_BEGIN __attribute__((optnone))
    #define NO_OPT_END 
    #define NO_INLINE __attribute__((noinline))
    #define CONSTFN __attribute__((const))
    #define RESTRICT __restrict__
    // https://awesomekling.github.io/Smarter-C++-inlining-with-attribute-flatten/
    #define FLATTEN __attribute__((flatten))    // inline everything in the function body
    #if CONFIG_FORCE_INLINE_DEBUG
        #define INLINE NO_INLINE 
    #else
        #define INLINE static inline 
    #endif
    #ifdef _MSC_VER
        #define __stdcall
    #endif
    #define NO_VTABLE 
#elif COMPILER_MSVC
    #define FORCE_INLINE __forceinline
    #define FUNCTION __FUNCTION__
    #define NO_INLINE __declspec(noinline)
    #define NO_OPT_BEGIN __pragma(optimize("", off)) 
    #define NO_OPT_END __pragma(optimize("", on)) 
    #define CONSTFN __declspec(noalias)
    #define RESTRICT __restrict
    #define FLATTEN
    #define NO_VTABLE __declspec(novtable)
    #if CONFIG_FORCE_INLINE_DEBUG
        #define INLINE NO_INLINE 
    #else
        #define INLINE static inline 
    #endif
#else
    #error "Unknown COMPILER_?"
#endif

#if COMPILER_CLANG
    #define PRAGMA_DIAGNOSTIC_PUSH_CLANG_() _Pragma("clang diagnostic push")
    #define PRAGMA_DIAGNOSTIC_POP_CLANG_() _Pragma("clang diagnostic pop")
    #define PRAGMA_DIAGNOSTIC_IGNORED_CLANG(_x) _Pragma(STRINGIZE(clang diagnostic ignored _x))
#else
    #define PRAGMA_DIAGNOSTIC_PUSH_CLANG_()
    #define PRAGMA_DIAGNOSTIC_POP_CLANG_()
    #define PRAGMA_DIAGNOSTIC_IGNORED_CLANG(_x)
#endif    // COMPILER_CLANG

#if COMPILER_GCC && COMPILER_GCC >= 40600
    #define PRAGMA_DIAGNOSTIC_PUSH_GCC_() _Pragma("GCC diagnostic push")
    #define PRAGMA_DIAGNOSTIC_POP_GCC_() _Pragma("GCC diagnostic pop")
    #define PRAGMA_DIAGNOSTIC_IGNORED_GCC(_x) _Pragma(STRINGIZE(GCC diagnostic ignored _x))
#else
    #define PRAGMA_DIAGNOSTIC_PUSH_GCC_()
    #define PRAGMA_DIAGNOSTIC_POP_GCC_()
    #define PRAGMA_DIAGNOSTIC_IGNORED_GCC(_x)
#endif    // COMPILER_GCC

#if COMPILER_MSVC
    #define PRAGMA_DIAGNOSTIC_PUSH_MSVC_() __pragma(warning(push))
    #define PRAGMA_DIAGNOSTIC_POP_MSVC_() __pragma(warning(pop))
    #define PRAGMA_DIAGNOSTIC_IGNORED_MSVC(_x) __pragma(warning(disable : _x))
#else
    #define PRAGMA_DIAGNOSTIC_PUSH_MSVC_()
    #define PRAGMA_DIAGNOSTIC_POP_MSVC_()
    #define PRAGMA_DIAGNOSTIC_IGNORED_MSVC(_x)
#endif    // COMPILER_MSVC

#if COMPILER_CLANG
    #define PRAGMA_DIAGNOSTIC_PUSH PRAGMA_DIAGNOSTIC_PUSH_CLANG_
    #define PRAGMA_DIAGNOSTIC_POP PRAGMA_DIAGNOSTIC_POP_CLANG_
    #define PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC PRAGMA_DIAGNOSTIC_IGNORED_CLANG
#elif COMPILER_GCC
    #define PRAGMA_DIAGNOSTIC_PUSH PRAGMA_DIAGNOSTIC_PUSH_GCC_
    #define PRAGMA_DIAGNOSTIC_POP PRAGMA_DIAGNOSTIC_POP_GCC_
    #define PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC PRAGMA_DIAGNOSTIC_IGNORED_GCC
#elif COMPILER_MSVC
    #define PRAGMA_DIAGNOSTIC_PUSH PRAGMA_DIAGNOSTIC_PUSH_MSVC_
    #define PRAGMA_DIAGNOSTIC_POP PRAGMA_DIAGNOSTIC_POP_MSVC_
    #define PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC(_x)
#endif    // COMPILER_

#define PLATFORM_POSIX (0 || PLATFORM_ANDROID || PLATFORM_IOS || PLATFORM_LINUX || PLATFORM_NX || PLATFORM_OSX)
#define PLATFORM_DESKTOP (0 || PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_OSX)
#define PLATFORM_MOBILE (0 || PLATFORM_ANDROID || PLATFORM_IOS)

// Force ToolMode=0 on mobile platforms
#if CONFIG_TOOLMODE && !PLATFORM_WINDOWS
    #undef CONFIG_TOOLMODE
    #define CONFIG_TOOLMODE 0
#endif
   
#define UNUSED(_a) (void)(_a)

#define INVALID_INDEX UINT32_MAX

#ifndef API
    #define API 
#endif

// Sanitizer macros 
#define IS_ASAN_ENABLED 0
#define NO_ASAN 

#if COMPILER_MSVC
    #if defined(__SANITIZE_ADDRESS__)
        #undef IS_ASAN_ENABLED
        #define IS_ASAN_ENABLED 1
        
        #undef NO_ASAN
        #define NO_ASAN __declspec(no_sanitize_address)
    #endif
#elif COMPILER_CLANG
    #if defined(__has_feature)
        #if __has_feature(address_sanitizer)
            #undef NO_ASAN
            #define NO_ASAN __attribute__((no_sanitize("address")))

            #undef IS_ASAN_ENABLED
            #define IS_ASAN_ENABLED 1
        #endif
    #endif
#elif COMPILER_GCC
    #if defined(__SANITIZE_ADDRESS__) && __SANITIZE_ADDRESS__
        #undef NO_ASAN
        #define NO_ASAN __attribute__((__no_sanitize_address__))

        #undef IS_ASAN_ENABLED
        #define IS_ASAN_ENABLED 1
    #endif
#endif  

#if IS_ASAN_ENABLED
    // taken from asan_interface.h (llvm)
    extern "C" void __asan_poison_memory_region(void const volatile *addr, size_t size);
    extern "C" void __asan_unpoison_memory_region(void const volatile *addr, size_t size);
    
    #define ASAN_POISON_MEMORY(_addr, _size) __asan_poison_memory_region((_addr), (_size));
    #define ASAN_UNPOISON_MEMORY(_addr, _size) __asan_unpoison_memory_region((_addr), (_size))
#else
    #define ASAN_POISON_MEMORY(_addr, _size)
    #define ASAN_UNPOISON_MEMORY(_addr, _size)
#endif

#define HAS_INCLUDE(incl) __has_include(incl)

// typedef basic opaque times, make them easier to write
using uint8 = uint8_t;
using int8 = int8_t;
using uint16 = uint16_t;
using int16 = int16_t;
using uint32 = uint32_t;
using int32 = int32_t;
using uint64 = uint64_t;
using int64 = int64_t;
using fl32 = float;
using fl64 = double;
using uintptr = uintptr_t;

static inline constexpr uint32 kMaxPath = CONFIG_MAX_PATH;
static inline constexpr size_t kKB = 1024;
static inline constexpr size_t kMB = 1024*1024;
static inline constexpr size_t kGB = 1024*1024*1024;

// Minimal template min/max/clamp/Swap implementations
template <typename T> T Max(T a, T b);
template <typename T> T Min(T a, T b);
template <typename T> T Clamp(T v, T _min, T _max);
template <typename T> void Swap(T& a, T& b);
template <typename T, size_t N> constexpr uint32 CountOf(T const (&)[N]);

template <typename T> 
inline void Swap(T& a, T& b)
{
    T tmp = b;
    b = a;
    a = tmp;
}

template <typename T, size_t N> 
constexpr uint32 CountOf(T const (&)[N])
{
    return static_cast<uint32>(N);
}
    
template<typename T> inline T Max(T a, T b) { return (a > b) ? a : b; }
template<> inline int Max(int a, int b) { return (a > b) ? a : b; }
template<> inline fl32 Max(fl32 a, fl32 b) { return (a > b) ? a : b; }
template<> inline fl64 Max(fl64 a, fl64 b) { return (a > b) ? a : b; }
template<> inline int8 Max(int8 a, int8 b) { return (a > b) ? a : b; }
template<> inline uint8 Max(uint8 a, uint8 b) { return (a > b) ? a : b; }
template<> inline uint16 Max(uint16 a, uint16 b) { return (a > b) ? a : b; }
template<> inline int16 Max(int16 a, int16 b) { return (a > b) ? a : b; }
template<> inline uint32 Max(uint32 a, uint32 b) { return (a > b) ? a : b; }
template<> inline int64 Max(int64 a, int64 b) { return (a > b) ? a : b; }
template<> inline uint64 Max(uint64 a, uint64 b) { return (a > b) ? a : b; }

template<typename T> T Min(T a, T b) { return (a < b) ? a : b; }
template<> inline int Min(int a, int b) { return (a < b) ? a : b; }
template<> inline fl32 Min(fl32 a, fl32 b) { return (a < b) ? a : b; }
template<> inline fl64 Min(fl64 a, fl64 b) { return (a < b) ? a : b; }
template<> inline int8 Min(int8 a, int8 b) { return (a < b) ? a : b; }
template<> inline uint8 Min(uint8 a, uint8 b) { return (a < b) ? a : b; }
template<> inline uint16 Min(uint16 a, uint16 b) { return (a < b) ? a : b; }
template<> inline int16 Min(int16 a, int16 b) { return (a < b) ? a : b; }
template<> inline uint32 Min(uint32 a, uint32 b) { return (a < b) ? a : b; }
template<> inline int64 Min(int64 a, int64 b) { return (a < b) ? a : b; }
template<> inline uint64 Min(uint64 a, uint64 b) { return (a < b) ? a : b; }

template<typename T> inline T Clamp(T v, T _min, T _max) { return Max(Min(v, _max), _min); }
template<> inline int Clamp(int v, int _min, int _max) { return Max(Min(v, _max), _min); }
template<> inline fl32 Clamp(fl32 v, fl32 _min, fl32 _max) { return Max(Min(v, _max), _min); }
template<> inline fl64 Clamp(fl64 v, fl64 _min, fl64 _max) { return Max(Min(v, _max), _min); }
template<> inline int8 Clamp(int8 v, int8 _min, int8 _max) { return Max(Min(v, _max), _min); }
template<> inline uint8 Clamp(uint8 v, uint8 _min, uint8 _max) { return Max(Min(v, _max), _min); }
template<> inline int16 Clamp(int16 v, int16 _min, int16 _max) { return Max(Min(v, _max), _min); }
template<> inline uint16 Clamp(uint16 v, uint16 _min, uint16 _max) { return Max(Min(v, _max), _min); }
template<> inline uint32 Clamp(uint32 v, uint32 _min, uint32 _max) { return Max(Min(v, _max), _min); }
template<> inline int64 Clamp(int64 v, int64 _min, int64 _max) { return Max(Min(v, _max), _min); }
template<> inline uint64 Clamp(uint64 v, uint64 _min, uint64 _max) { return Max(Min(v, _max), _min); }

// pointer to integer conversion
template <typename T> T PtrToInt(void* ptr);

template<> inline uint16 PtrToInt(void* ptr) { return static_cast<uint16>((uintptr_t)ptr); }
template<> inline uint32 PtrToInt(void* ptr) { return static_cast<uint32>((uintptr_t)ptr); }
template<> inline int    PtrToInt(void* ptr) { return static_cast<int>((uintptr_t)ptr); }
template<> inline uint64 PtrToInt(void* ptr) { return static_cast<uint64>((uintptr_t)ptr); }
template<> inline int64  PtrToInt(void* ptr) { return static_cast<int64>((uintptr_t)ptr); }

// integer to pointer conversion
template <typename T> void* IntToPtr(T i);

template<> inline void* IntToPtr(uint16 i)  { return reinterpret_cast<void*>((uintptr_t)i); }
template<> inline void* IntToPtr(uint32 i)  { return reinterpret_cast<void*>((uintptr_t)i); }
template<> inline void* IntToPtr(int i)     { return reinterpret_cast<void*>((uintptr_t)i); }
template<> inline void* IntToPtr(uint64 i)  { return reinterpret_cast<void*>((uintptr_t)i); }
template<> inline void* IntToPtr(int64 i)   { return reinterpret_cast<void*>((uintptr_t)i); }

template <typename T> T IndexToId(T i);
template<> inline uint16 IndexToId(uint16 i)  { return i + 1; }
template<> inline uint32 IndexToId(uint32 i)  { return i + 1; }

template <typename T> T IdToIndex(T i);
template<> inline uint16 IdToIndex(uint16 i)  { return i - 1; }
template<> inline uint32 IdToIndex(uint32 i)  { return i - 1; }

// This macro is to enable bitmasks for "enum class" types
#define ENABLE_BITMASK(_EnumType) \
    FORCE_INLINE _EnumType operator|(_EnumType lhs, _EnumType rhs) {   \
        return static_cast<_EnumType>(static_cast<uint32>(lhs) | static_cast<uint32>(rhs));  \
    }   \
    FORCE_INLINE _EnumType operator&(_EnumType lhs, _EnumType rhs) {   \
        return static_cast<_EnumType>(static_cast<uint32>(lhs) & static_cast<uint32>(rhs));  \
    }   \
    FORCE_INLINE _EnumType operator^(_EnumType lhs, _EnumType rhs) {   \
        return static_cast<_EnumType>(static_cast<uint32>(lhs) ^ static_cast<uint32>(rhs));  \
    }    \
    FORCE_INLINE _EnumType operator~(_EnumType rhs) {   \
        return static_cast<_EnumType>(~static_cast<uint32>(rhs));  \
    }   \
    FORCE_INLINE _EnumType& operator|=(_EnumType& lhs, _EnumType rhs)  {   \
        lhs = static_cast<_EnumType>(static_cast<uint32>(lhs) | static_cast<uint32>(rhs));  \
        return lhs; \
    }   \
    FORCE_INLINE _EnumType& operator&=(_EnumType& lhs, _EnumType rhs)   {   \
        lhs = static_cast<_EnumType>(static_cast<uint32>(lhs) & static_cast<uint32>(rhs));  \
        return lhs; \
    }   \
    FORCE_INLINE _EnumType& operator^=(_EnumType& lhs, _EnumType rhs)   {   \
        lhs = static_cast<_EnumType>(static_cast<uint32>(lhs) ^ static_cast<uint32>(rhs));  \
        return lhs; \
    }

template <typename _A, typename _B>
struct Pair
{
    _A first;
    _B second;

    Pair() = default;
    explicit Pair(const _A& a, const _B& b) : 
        first(a), second(b) {}
};

//----------------------------------------------------------------------------------------------------------------------
// Random generation
struct RandomContext
{
    uint64 state[2];
};

API uint32        randomGenSeed();
API RandomContext randomCreateContext(uint32 seed = randomGenSeed());

API uint32        randomNewUint(RandomContext* ctx);
API float         randomNewFloat(RandomContext* ctx);
API float         randomNewFloatInRange(RandomContext* ctx, float _min, float _max);
API int           randomNewIntInRange(RandomContext* ctx, int _min, int _max);

// Context Free random functions (Uses thread_local context)
API uint32        randomNewUint();
API float         randomNewFloat();
API float         randomNewFloatInRange(float _min, float _max);
API int           randomNewIntInRange(int _min, int _max);

// Misc
INLINE constexpr uint32 MakeFourCC(uint8 _a, uint8 _b, uint8 _c, uint8 _d)
{
    return  static_cast<uint32>(_a) | 
           (static_cast<uint32>(_b) << 8) | 
           (static_cast<uint32>(_c) << 16) | 
           (static_cast<uint32>(_d) << 24);
}

template<typename _T> inline constexpr _T AlignValue(_T value, _T align);
template<> inline constexpr int AlignValue(int value, int align) { int mask = align - 1; return (value + mask) & ((~0) & (~mask)); }
template<> inline constexpr uint16 AlignValue(uint16 value, uint16 align) { uint16 mask = align - 1; return (value + mask) & ((~0) & (~mask)); }
template<> inline constexpr uint32 AlignValue(uint32 value, uint32 align) { uint32 mask = align - 1; return (value + mask) & ((~0) & (~mask)); }
template<> inline constexpr uint64 AlignValue(uint64 value, uint64 align) { uint64 mask = align - 1; return (value + mask) & ((~0) & (~mask)); }
#if PLATFORM_APPLE
template<> inline constexpr size_t AlignValue(size_t value, size_t align) { size_t mask = align - 1; return (value + mask) & ((~0) & (~mask)); }
#endif

template<typename _T> inline constexpr _T DivCeil(_T value, _T divider);
template<> inline constexpr int DivCeil(int value, int divider) { return (value + divider - 1)/divider; }
template<> inline constexpr uint16 DivCeil(uint16 value, uint16 divider) { return (value + divider - 1)/divider; }
template<> inline constexpr uint32 DivCeil(uint32 value, uint32 divider) { return (value + divider - 1)/divider; }
template<> inline constexpr uint64 DivCeil(uint64 value, uint64 divider) { return (value + divider - 1)/divider; }

// Implemented in Atomic.h
// We put this in here to avoid expensive c89atomic.h include
struct alignas(CACHE_LINE_SIZE) AtomicLock
{
    uint32 locked = 0;
    uint8 padding[CACHE_LINE_SIZE - sizeof(uint32)];
};

#if defined(Main)
    #undef Main
#endif

#if PLATFORM_ANDROID
    #define Main AndroidMain
#else
    #define Main main
#endif

//----------------------------------------------------------------------------------------------------------------------
// ASSERT implementation
#if PLATFORM_ANDROID
    #include <signal.h> // raise
#endif

using AssertFailCallback = void(*)(void* userData);
API void assertSetFailCallback(AssertFailCallback callback, void* userdata);
API void assertRunFailCallback();
API void assertDebugMessage(const char* fmt, ...);

#if PLATFORM_ANDROID
    #define DEBUG_BREAK() raise(SIGINT)
#elif COMPILER_MSVC
    #define DEBUG_BREAK() __debugbreak()
#elif COMPILER_CLANG
    #if (__has_builtin(__builtin_debugtrap))
        #define DEBUG_BREAK() __builtin_debugtrap()
    #else
        #define DEBUG_BREAK() __builtin_trap()    // This cannot be used in constexpr functions
    #endif
#elif COMPILER_GCC
    #define DEBUG_BREAK() __builtin_trap()
#endif

// Assert macros
// ASSERT: regular assert
// ASSERT_MSG: Assert with a formatted message to debug output
// ASSERT_ALWAYS: Assert even in release builds. Also with a formatted message
#ifdef ASSERT
    #undef ASSERT
#endif

#if CONFIG_ENABLE_ASSERT
    #define ASSERT(_expr) do { if (!(_expr)) { assertDebugMessage(#_expr); assertRunFailCallback(); DEBUG_BREAK(); }} while(0)
    #define ASSERT_MSG(_expr, ...) do { if (!(_expr)) { assertDebugMessage(__VA_ARGS__); assertRunFailCallback(); DEBUG_BREAK(); }} while(0)
#else
    #define ASSERT(_expr)
    #define ASSERT_MSG(_expr, ...)
#endif

#define ASSERT_ALWAYS(_expr, ...) do { if (!(_expr)) { assertDebugMessage(__VA_ARGS__); assertRunFailCallback(); DEBUG_BREAK(); }} while(0)

//----------------------------------------------------------------------------------------------------------------------
// RelativePointer: https://septag.dev/blog/posts/junkyard-relativeptr/
template <typename _T>
struct RelativePtr
{
    RelativePtr() : mOffset(0) {}
    RelativePtr(_T* ptr) { Set(ptr); }
    _T* operator->() { return Get(); }
    const _T* operator->() const { return Get(); }
    RelativePtr<_T>& operator=(_T* ptr) { Set(ptr); return *this; }
    RelativePtr<_T>& operator=(const _T* ptr) { Set(ptr); return *this; }
    _T& operator[](uint32 index) { return Get()[index]; }
    const _T& operator[](uint32 index) const { return Get()[index]; }
    bool IsNull() const { return mOffset == 0; };
    void SetNull() { mOffset = 0; }
    bool operator!() const { return IsNull(); }
    operator bool() const { return !IsNull(); }
    _T& operator*() const { return *Get(); }

    inline void Set(const _T* ptr)
    { 
        ASSERT(ptr != nullptr);
        ASSERT(uintptr_t(ptr) > uintptr_t(this));
        mOffset = uint32((uint8*)ptr - (uint8*)this);
    }

    inline _T* Get() const 
    { 
        ASSERT(mOffset);
        return (_T*)((uint8*)this + mOffset); 
    }

private:
    uint32 mOffset;
};


//----------------------------------------------------------------------------------------------------------------------
// Span: holds pointer/count pairs. useful for passing around slices of data 
template <typename _T>
struct Span
{
    Span() : mData(nullptr), mCount(0) {}
    Span(_T* data, uint32 count) : mData(data), mCount(count) {}
    Span(_T* data, _T* end) : mData(data), mCount(PtrToInt<uint32>(end - data)) { ASSERT(data); ASSERT(end); }

    _T& operator[](uint32 index)    
    { 
        #ifdef CONFIG_CHECK_OUTOFBOUNDS
            ASSERT_MSG(index < mCount, "Index out of bounds (count: %u, index: %u)", mCount, index);
        #endif
        return mData[index];
    }

    const _T& operator[](uint32 index) const
    { 
        #ifdef CONFIG_CHECK_OUTOFBOUNDS
            ASSERT_MSG(index < mCount, "Index out of bounds (count: %u, index: %u)", mCount, index);
        #endif
        return mData[index];
    }

    uint32 Count() const    { return mCount; }
    const _T* Ptr() const   { return mData; }
    _T* Ptr()   { return mData; }

    Span<_T> Slice(uint32 index, uint32 count)
    {
        #ifdef CONFIG_CHECK_OUTOFBOUNDS
            ASSERT_MSG(index < mCount, "Index out of bounds (mCount: %u, index: %u)", mCount, index);
            ASSERT_MSG((index + count) <= mCount, "Count out of bounds (mCount: %u, index: %u, count: %u)", mCount, index, count);
        #endif
        return Span(mData + index, count);
    }

    // C++ stl crap compatibility. it's mainly `for(auto t : array)` syntax sugar
    struct Iterator 
    {
        Iterator(_T* ptr) : _ptr(ptr) {}
        _T& operator*() { return *_ptr; }
        void operator++() { ++_ptr; }
        bool operator!=(Iterator it) { return _ptr != it._ptr; }
        _T* _ptr;
    };

    Iterator begin()    { return Iterator(&mData[0]); }
    Iterator end()      { return Iterator(&mData[mCount]); }

    Iterator begin() const    { return Iterator(&mData[0]); }
    Iterator end() const     { return Iterator(&mData[mCount]); }

private:
    _T* mData;
    uint32 mCount;
};

//----------------------------------------------------------------------------------------------------------------------
// Base memory types and allocator interface
enum class AllocatorType
{
    Unknown,
    Heap,       // Normal malloc/free heap allocator
    Temp,       // Stack-based temp allocator. Grows by page. Only works within a single thread context and function scopes.
    Bump,       // Bump/Linear-based allocator. Fixed capacity. Grows page by page. Can be backed by any kind of memory (VM/gpu/stack/heap/etc.)
    Tlsf        // TLSF dynamic allocator. Fixed capacity. Persists in memory and usually used for subsystems with unknown memory allocation pattern.
};

struct NO_VTABLE Allocator
{
    virtual void* Malloc(size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) = 0;
    virtual void* Realloc(void* ptr, size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) = 0;
    virtual void  Free(void* ptr, uint32 align = CONFIG_MACHINE_ALIGNMENT) = 0;
    virtual AllocatorType GetType() const = 0;
};

using MemFailCallback = void(*)(void* userData);

API void memSetFailCallback(MemFailCallback callback, void* userdata);
API void memRunFailCallback();
API void* memAlignPointer(void* ptr, size_t extra, uint32 align);
API Allocator* memDefaultAlloc();
API void memSetDefaultAlloc(Allocator* alloc);

API void memEnableMemPro(bool enable);
API bool memIsMemProEnabled();
API void memTrackMalloc(void* ptr, size_t size);
API void memTrackFree(void* ptr);
API void memTrackRealloc(void* oldPtr, void* ptr, size_t size);

#define MEMORY_FAIL() do { memRunFailCallback(); ASSERT_ALWAYS(0, "Out of memory"); } while (0)

FORCE_INLINE void* memAlloc(size_t size, Allocator* alloc = memDefaultAlloc());
FORCE_INLINE void* memAllocZero(size_t size, Allocator* alloc = memDefaultAlloc());
FORCE_INLINE void* memRealloc(void* ptr, size_t size, Allocator* alloc = memDefaultAlloc());
FORCE_INLINE void  memFree(void* ptr, Allocator* alloc = memDefaultAlloc());

FORCE_INLINE void* memAllocAligned(size_t size, uint32 align, Allocator* alloc = memDefaultAlloc());
FORCE_INLINE void* memAllocAlignedZero(size_t size, uint32 align, Allocator* alloc = memDefaultAlloc());
FORCE_INLINE void* memReallocAligned(void* ptr, size_t size, uint32 align, Allocator* alloc = memDefaultAlloc());
FORCE_INLINE void  memFreeAligned(void* ptr, uint32 align, Allocator* alloc = memDefaultAlloc());

template<typename _T> _T* memAllocTyped(uint32 count = 1, Allocator* alloc = memDefaultAlloc());
template<typename _T> _T* memAllocZeroTyped(uint32 count = 1, Allocator* alloc = memDefaultAlloc());
template<typename _T> _T* memAllocAlignedTyped(uint32 count = 1, uint32 align = CONFIG_MACHINE_ALIGNMENT, Allocator* alloc = memDefaultAlloc());
template<typename _T> _T* memAllocAlignedZeroTyped(uint32 count = 1, uint32 align = CONFIG_MACHINE_ALIGNMENT, Allocator* alloc = memDefaultAlloc());
template<typename _T> _T* memReallocTyped(void* ptr, uint32 count = 1, Allocator* alloc = memDefaultAlloc());
template<typename _T> _T* memAllocCopy(const _T* src, uint32 count = 1, Allocator* alloc = memDefaultAlloc());
template<typename _T> _T* memAllocCopyRawBytes(const _T* src, size_t sizeBytes, Allocator* alloc = memDefaultAlloc());

//----------------------------------------------------------------------------------------------------------------------
// new/delete overrides
namespace _private
{
    struct PlacementNewTag {};
}

#define PLACEMENT_NEW(_ptr, _type) ::new(_private::PlacementNewTag(), _ptr) _type
#define NEW(_alloc, _type) PLACEMENT_NEW(memAlloc(sizeof(_type), _alloc), _type)
#define ALIGNED_NEW(_alloc, _type, _align) PLACEMENT_NEW(memAllocAligned(sizeof(_type), _align, _alloc), _type)

#define PLACEMENT_NEW_ARRAY(_ptr, _type, _n) new(_private::PlacementNewTag(), _ptr) _type[_n]
#define NEW_ARRAY(_alloc, _type, _n) PLACEMENT_NEW_ARRAY(memAlloc(sizeof(_type)*_n, _alloc), _type, _n)

inline void* operator new(size_t, _private::PlacementNewTag, void* _ptr) { return _ptr; }
inline void* operator new[](size_t, _private::PlacementNewTag, void* _ptr) { return _ptr; }
inline void  operator delete(void*, _private::PlacementNewTag, void*) throw() {}

//----------------------------------------------------------------------------------------------------------------------
// Inline implementation
[[nodiscard]] FORCE_INLINE void* memAlloc(size_t size, Allocator* alloc)
{
    ASSERT(alloc);
    void* ptr = alloc->Malloc(size, CONFIG_MACHINE_ALIGNMENT);
    if (ptr == NULL) {
        MEMORY_FAIL();
    }
    return ptr;
}

[[nodiscard]] FORCE_INLINE void* memAllocZero(size_t size, Allocator* alloc)
{
    ASSERT(alloc);
    void* ptr = alloc->Malloc(size, CONFIG_MACHINE_ALIGNMENT);
    if (ptr == NULL) {
        MEMORY_FAIL();
    }
    memset(ptr, 0x0, size);
    return ptr;
}

[[nodiscard]] FORCE_INLINE void* memRealloc(void* ptr, size_t size, Allocator* alloc)
{
    ASSERT(alloc);
    ptr = alloc->Realloc(ptr, size, CONFIG_MACHINE_ALIGNMENT);
    if (ptr == NULL) {
        MEMORY_FAIL();
    }
    return ptr;
}

FORCE_INLINE void memFree(void* ptr, Allocator* alloc)
{
    ASSERT(alloc);
    alloc->Free(ptr, CONFIG_MACHINE_ALIGNMENT);
}

[[nodiscard]] FORCE_INLINE void* memAllocAligned(size_t size, uint32 align, Allocator* alloc)
{
    ASSERT(alloc);
    align = Max(align, CONFIG_MACHINE_ALIGNMENT);
    void* ptr = alloc->Malloc(AlignValue<size_t>(size, align), align);
    if (ptr == NULL) {
        MEMORY_FAIL();
    }
    return ptr;
}

[[nodiscard]] FORCE_INLINE void* memAllocAlignedZero(size_t size, uint32 align, Allocator* alloc)
{
    ASSERT(alloc);
    align = Max(align, CONFIG_MACHINE_ALIGNMENT);
    void* ptr = alloc->Malloc(AlignValue<size_t>(size, align), align);
    if (ptr == NULL) {
        MEMORY_FAIL();
        return nullptr;
    }
    memset(ptr, 0x0, size);
    return ptr;
}

[[nodiscard]] FORCE_INLINE void* memReallocAligned(void* ptr, size_t size, uint32 align, Allocator* alloc)
{
    ASSERT(alloc);
    align = Max(align, CONFIG_MACHINE_ALIGNMENT);
    ptr = alloc->Realloc(ptr, AlignValue<size_t>(size, align), align);
    if (ptr == NULL) {
        MEMORY_FAIL();
    }
    return ptr;

}

FORCE_INLINE void memFreeAligned(void* ptr, uint32 align, Allocator* alloc)
{
    ASSERT(alloc);
    alloc->Free(ptr, Max(align, CONFIG_MACHINE_ALIGNMENT));
}

template<typename _T>
[[nodiscard]] inline _T* memAllocTyped(uint32 count, Allocator* alloc)
{
    return reinterpret_cast<_T*>(memAlloc(sizeof(_T)*count, alloc));
}

template<typename _T>
[[nodiscard]] inline _T* memAllocZeroTyped(uint32 count, Allocator* alloc)
{
    return reinterpret_cast<_T*>(memAllocZero(sizeof(_T)*count, alloc));
}

template<typename _T>
[[nodiscard]] inline _T* memAllocAlignedTyped(uint32 count, uint32 align, Allocator* alloc)
{
    return reinterpret_cast<_T*>(memAllocAligned(sizeof(_T)*count, align, alloc));
}

template<typename _T>
[[nodiscard]] inline _T* memAllocAlignedZeroTyped(uint32 count, uint32 align, Allocator* alloc)
{
    return reinterpret_cast<_T*>(memAllocAlignedZero(sizeof(_T)*count, align, alloc));
}

template<typename _T>
[[nodiscard]] inline _T* memReallocTyped(void* ptr, uint32 count, Allocator* alloc)
{
    return reinterpret_cast<_T*>(memRealloc(ptr, sizeof(_T)*count, alloc));
}

template<typename _T> 
[[nodiscard]] inline _T* memAllocCopy(const _T* src, uint32 count, Allocator* alloc)
{
    if (count == 0) {
        ASSERT(0);
        return nullptr;
    }

    auto buff = memAllocTyped<_T>(count, alloc);
    if (buff) {
        memcpy(buff, src, sizeof(_T)*count);
        return buff;
    }
    else {
        return nullptr;
    }
}

template<typename _T> 
[[nodiscard]] inline _T* memAllocCopyRawBytes(const _T* src, size_t sizeBytes, Allocator* alloc)
{
    if (sizeBytes == 0) {
        ASSERT(0);
        return nullptr;
    }

    auto buff = (_T*)memAlloc(sizeBytes, alloc);
    if (buff) {
        memcpy(buff, src, sizeBytes);
        return buff;
    }
    else {
        return nullptr;
    }
}



