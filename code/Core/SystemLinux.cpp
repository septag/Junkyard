#include "System.h"

#if PLATFORM_LINUX

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

char* OS::GetMyPath(char* dst, size_t dstSize)
{
    ssize_t len = readlink("/proc/self/exe", dst, dstSize - 1);
    ASSERT(len != -1);
    dst[len] = '\0';
    return dst;
}

char* OS::GetCurrentDir(char* dst, size_t dstSize)
{
    return getcwd(dst, dstSize);
}

void OS::SetCurrentDir(const char* path)
{
    [[maybe_unused]] int r = chdir(path);
    ASSERT(r != -1);
}

void OS::GetSysInfo(SysInfo* sysInfo)
{
    auto cpuid = [](int func, int subfunc, int *eax, int *ebx, int *ecx, int *edx) 
    {
        int a, b, c, d;
        __asm__ volatile (
            "push %%rbx\n"      // Save rbx (callee-saved)
            "cpuid\n"
            "movl %%ebx, %1\n"   // Move 32-bit ebx to output variable
            "pop %%rbx\n"       // Restore original rbx
            : "=a"(a), "=r"(b), "=c"(c), "=d"(d)
            : "a"(func), "c"(subfunc)
            : "memory"
        );
        *eax = a;
        *ebx = b;
        *ecx = c;
        *edx = d;
    };
    
    {
        constexpr int CPUID_GET_FEATURES = 1;
        int eax, ebx, ecx, edx;
        cpuid(CPUID_GET_FEATURES, 0, &eax, &ebx, &ecx, &edx);
        sysInfo->cpuCapsSSE = edx & (1 << 25);
        sysInfo->cpuCapsSSE2 = edx & (1 << 26);
        sysInfo->cpuCapsSSE3 = (ecx & (1 << 0));
        sysInfo->cpuCapsSSE41 = (ecx & (1 << 19));
        sysInfo->cpuCapsSSE42 = (ecx & (1 << 20));
        sysInfo->cpuCapsAVX = (ecx & (1 << 28));
        sysInfo->cpuCapsAVX2 = (ebx & (1 << 5));
    }

    sysInfo->pageSize = sysconf(_SC_PAGESIZE);

    {
        struct CoreInfo {
            int physical_id;
            int core_id;
        };

        uint32 count = 0;
        int curPhysicalCore = -1, curCore = -1;
        CoreInfo cores[256];

        FILE *fp = fopen("/proc/cpuinfo", "r");
        if (fp != nullptr) {
            char line[256];
            char model[128] = "";
            char name[128] = "";
            
            while (fgets(line, sizeof(line), fp)) {
                if (!name[0] && strncmp(line, "vendor_id", 9) == 0)
                    sscanf(line, "vendor_id : %[^\n]", name);
                if (!model[0] && strncmp(line, "model name", 10) == 0)
                    sscanf(line, "model name : %[^\n]", model);
                if (strncmp(line, "physical id", 11) == 0) {
                    sscanf(line, "physical id : %d", &curPhysicalCore);
                } else if (strncmp(line, "core id", 7) == 0) {
                    sscanf(line, "core id : %d", &curCore);
                } else if (line[0] == '\n') { // blank line indicates end of one processor block
                    if (curPhysicalCore != -1 && curCore != -1) {
                        // Check if this (physical id, core id) pair is already counted.
                        bool exists = false;
                        for (uint32 i = 0; i < count; i++) {
                            if (cores[i].physical_id == curPhysicalCore && cores[i].core_id == curCore) {
                                exists = true;
                                break;
                            }
                        }
                        if (!exists) {
                            cores[count].physical_id = curPhysicalCore;
                            cores[count].core_id = curCore;
                            count++;
                        }
                    }
                    // Reset for the next processor entry
                    curPhysicalCore = -1;
                    curCore = -1;
                }                
            }

            fclose(fp);

            sysInfo->coreCount = count;
            Str::Copy(sysInfo->cpuName, sizeof(sysInfo->cpuName), name);
            Str::Copy(sysInfo->cpuModel, sizeof(sysInfo->cpuModel), model);
        }

        #if CPU_ARM && ARCH_64BIT
            sysInfo->cpuFamily = SysInfo::CpuFamily::ARM64;
        #elif CPU_X86 && ARCH_64BIT
            sysInfo->cpuFamily = SysInfo::CpuFamily::x86_64;
        #elif CPU_ARM && ARCH_32BIT
            sysInfo->cpuFamily = SysInfo::CpuFamily::ARM;
        #endif
    }

    {
        FILE *fp = fopen("/proc/meminfo", "r");
        if (fp != nullptr) {
            char line[256];
            while (fgets(line, sizeof(line), fp)) {
                if (strncmp(line, "MemTotal:", 9) == 0) {
                    uint64 memTotal;
                    sscanf(line, "MemTotal: %lu kB", &memTotal);
                    printf("Total physical memory: %lu KB\n", memTotal);
                    sysInfo->physicalMemorySize = memTotal;
                    break;
                }
            }
        
            fclose(fp);
        }
    }
}

#endif // PLATFORM_LINUX
