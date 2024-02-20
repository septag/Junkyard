// Other System implmentations reside in SystemPosix.cpp
#include "System.h"

#if PLATFORM_ANDROID

#include <android/log.h>
#include <android/native_activity.h>
#include <jni.h>

#include "External/cpufeatures/cpu-features.h"
PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wunused-function")
#include "External/cpufeatures/cpu-features.c"
PRAGMA_DIAGNOSTIC_POP()

#include "Atomic.h"
#include "Blobs.h"

static thread_local JNIEnv* gJniEnv = nullptr;
static atomicUint32 gJniAttachedThreadCount;

#if CONFIG_ENABLE_ASSERT
static constexpr uint32 kJniMaxAttachedThreadCount = 5;
#endif

// https://en.wikipedia.org/wiki/CPUID
// https://docs.microsoft.com/en-us/cpp/intrinsics/cpuid-cpuidex?redirectedfrom=MSDN&view=msvc-170
void sysGetSysInfo(SysInfo* info)
{
    info->coreCount = android_getCpuCount();
    info->pageSize = sysGetPageSize();

    switch (android_getCpuFamily()) {
    case ANDROID_CPU_FAMILY_ARM:        info->cpuFamily = SysCpuFamily::ARM;    break;
    case ANDROID_CPU_FAMILY_ARM64:      info->cpuFamily = SysCpuFamily::ARM64;  break;
    case ANDROID_CPU_FAMILY_X86:        
    case ANDROID_CPU_FAMILY_X86_64:     info->cpuFamily = SysCpuFamily::x86_64; break;
    default:                            
        ASSERT_MSG(0, "Hardware not supported"); info->cpuFamily = SysCpuFamily::Unknown; break;
    }

    uint64 features  = android_getCpuFeatures();
    if (info->cpuFamily == SysCpuFamily::ARM || info->cpuFamily == SysCpuFamily::ARM64) {
        if (features & ANDROID_CPU_ARM_FEATURE_NEON)
            info->cpuCapsNeon = true;
    }
    else if (info->cpuFamily == SysCpuFamily::x86_64) {
        if (features & ANDROID_CPU_X86_FEATURE_SSSE3)
            info->cpuCapsSSE3 = true;
        if (features & ANDROID_CPU_X86_FEATURE_SSE4_1)
            info->cpuCapsSSE41 = true;
        if (features & ANDROID_CPU_X86_FEATURE_SSE4_2)
            info->cpuCapsSSE42 = true;
        if (features & ANDROID_CPU_X86_FEATURE_AVX)
            info->cpuCapsAVX = true;
        if (features & ANDROID_CPU_X86_FEATURE_AVX2)
            info->cpuCapsAVX2 = true;
    }

    // For memory, read /proc/meminfo
    {
        File f;
        if (f.Open("/proc/meminfo", FileOpenFlags::Read|FileOpenFlags::SeqScan)) {
            Blob data;
            data.SetGrowPolicy(Blob::GrowPolicy::Linear);
            size_t numChars;
            char bulk[512];

            while ((numChars = f.Read<char>(bulk, sizeof(bulk))) > 0) {
                data.Write(bulk, numChars);
            }
            data.Write<char>('\0');
            f.Close();

            char* text;
            data.Detach((void**)&text, &numChars);
            
            const char* memTotalLine = strFindStr(text, "MemTotal:");
            if (memTotalLine) {
               memTotalLine += 9;
               char memText[32];
               uint32 memTextSize = 0;

               while (strIsWhitespace(*memTotalLine))
                   memTotalLine++;
               
               while (strIsNumber(*memTotalLine)) {
                   memText[memTextSize++] = *memTotalLine;
                   memTotalLine++;
               }
               memText[memTextSize] = '\0';
               info->physicalMemorySize = strToUint64(memText)*kKB;
            }

            memFree(text);
        }
    }

    // For processor model, read /proc/cpuinfo
    {
        File f;
        if (f.Open("/proc/cpuinfo", FileOpenFlags::Read|FileOpenFlags::SeqScan)) {
            Blob data;
            data.SetGrowPolicy(Blob::GrowPolicy::Linear);
            size_t numChars;
            char bulk[512];

            while ((numChars = f.Read<char>(bulk, sizeof(bulk))) > 0) {
                data.Write(bulk, numChars);
            }
            data.Write<char>('\0');
            f.Close();

            char* text;
            data.Detach((void**)&text, &numChars);
            strTrim(text, numChars, text, '\n');
            
            const char* lastNewline = strFindCharRev(text, '\n');
            if (lastNewline) {
                const char* lastLine = lastNewline + 1;
                if (strIsEqualCount(lastLine, "Hardware", 8)) {
                    const char* colon = strFindChar(lastLine, ':');
                    if (colon) {
                        strCopy(info->cpuModel, sizeof(info->cpuModel), colon + 1);
                        strTrim(info->cpuModel, sizeof(info->cpuModel), info->cpuModel, ' ');
                    }
                }
            }

            memFree(text);

        }
    }
}

char* pathGetMyPath(char*, size_t)
{
    ASSERT_MSG(0, "Exe path is not implemented on android");
    return nullptr;
}

void pathSetCurrentDir(const char*)
{
    ASSERT_MSG(0, "SetCurrentDir is not implemented on android");
}

char* pathGetCurrentDir(char*, size_t)
{
    ASSERT_MSG(0, "GetCurrentDir is not implemented on android");
    return nullptr;
}

void sysAndroidPrintToLog(SysAndroidLogType logType, const char* tag, const char* text)
{
    static_assert(SysAndroidLogType::Unknown == static_cast<SysAndroidLogType>(ANDROID_LOG_UNKNOWN));
    static_assert(SysAndroidLogType::Default == static_cast<SysAndroidLogType>(ANDROID_LOG_DEFAULT));
    static_assert(SysAndroidLogType::Verbose == static_cast<SysAndroidLogType>(ANDROID_LOG_VERBOSE));
    static_assert(SysAndroidLogType::Debug == static_cast<SysAndroidLogType>(ANDROID_LOG_DEBUG));
    static_assert(SysAndroidLogType::Info == static_cast<SysAndroidLogType>(ANDROID_LOG_INFO));
    static_assert(SysAndroidLogType::Warn == static_cast<SysAndroidLogType>(ANDROID_LOG_WARN));
    static_assert(SysAndroidLogType::Error == static_cast<SysAndroidLogType>(ANDROID_LOG_ERROR));
    static_assert(SysAndroidLogType::Fatal == static_cast<SysAndroidLogType>(ANDROID_LOG_FATAL));
    static_assert(SysAndroidLogType::Silent == static_cast<SysAndroidLogType>(ANDROID_LOG_SILENT));

    __android_log_write(static_cast<int>(logType), tag, text);
}

JNIEnv* sysAndroidAcquireJniEnv(ANativeActivity* activity)
{
    if (gJniEnv)
        return gJniEnv;
    ASSERT(activity);

    [[maybe_unused]] jint ret = activity->vm->AttachCurrentThread(&gJniEnv, nullptr);	// required to call JNIEnv functions on this thread
    ASSERT(ret == JNI_OK);
    [[maybe_unused]] uint32 activeThreadCount = atomicFetchAdd32(&gJniAttachedThreadCount, 1);
    ASSERT_MSG(activeThreadCount <= kJniMaxAttachedThreadCount, "Too many AcquireJniEnv in several threads");
    return gJniEnv;
}

void sysAndroidReleaseJniEnv(ANativeActivity* activity)
{
    ASSERT(activity);
    activity->vm->DetachCurrentThread();		// jni cleanup
    atomicFetchSub32(&gJniAttachedThreadCount, 1);
}

JNIEnv* sysAndroidGetJniEnv()
{
    // If this assert fires, the current thread doesn't have access to the JniEnvironment.
    // This can be achieved via AcquirerJni / ReleaseJni
    // Notes:
    //    - Jni-Enabled threads consume additional stack space and resources
    //    - Keep the amount of Jni-Enabled threads as low as possible. Ideally 1.
    //    - Do not Acquirer / Release frequently. OK for Long-Tasks, NoGo for Short-Tasks
    ASSERT_MSG(gJniEnv != nullptr, "JNI not attached. Call sysAndroidAcquireJniEnv/sysAndroidReleaseJniEnv on the calling thread");
    return gJniEnv;
}

// https://developer.android.com/reference/android/os/Debug
bool sysIsDebuggerPresent()
{
    JNIEnv* jniEnv = sysAndroidGetJniEnv();
    jclass clz = jniEnv->FindClass("android/os/Debug");
    ASSERT(clz);
    jmethodID funcId = jniEnv->GetStaticMethodID(clz, "isDebuggerConnected", "()Z");
    ASSERT(funcId);
    jboolean isConnected = jniEnv->CallStaticBooleanMethod(clz, funcId);
    jniEnv->DeleteLocalRef(clz);
    
    return isConnected;
}

Path sysAndroidGetCacheDirectory(ANativeActivity* activity)
{
    JNIEnv* jniEnv = sysAndroidGetJniEnv();

    jobject context = activity->clazz;
    jclass contextClass = jniEnv->GetObjectClass(context);

    // jclass contextClass = jniEnv->FindClass("android/content/Context");
    jmethodID getCacheDirMethod = jniEnv->GetMethodID(contextClass, "getCacheDir", "()Ljava/io/File;");
    jobject cacheDir = jniEnv->CallObjectMethod(context, getCacheDirMethod);

    jclass fileClass = jniEnv->GetObjectClass(cacheDir);
    jmethodID getPathMethod = jniEnv->GetMethodID(fileClass, "getPath", "()Ljava/lang/String;");
    jstring pathString = (jstring) jniEnv->CallObjectMethod(cacheDir, getPathMethod);

    const char* path = jniEnv->GetStringUTFChars(pathString, nullptr);
    Path r(path);
    jniEnv->ReleaseStringUTFChars(pathString, path);

    return r;
}

#endif // PLATFORM_ANDROID