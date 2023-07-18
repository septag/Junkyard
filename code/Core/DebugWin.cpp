#include "Base.h"

#if PLATFORM_WINDOWS

#include "StringUtil.h"  // strCopy/..
#include "IncludeWin.h"

#pragma pack(push, 8)
#include <DbgHelp.h>

typedef struct _IMAGEHLP_MODULE64_V3
{
    DWORD SizeOfStruct;        // set to sizeof(IMAGEHLP_MODULE64)
    DWORD64 BaseOfImage;       // base load address of module
    DWORD ImageSize;           // virtual size of the loaded module
    DWORD TimeDateStamp;       // date/time stamp from pe header
    DWORD CheckSum;            // checksum from the pe header
    DWORD NumSyms;             // number of symbols in the symbol table
    SYM_TYPE SymType;          // type of symbols loaded
    CHAR ModuleName[32];       // module name
    CHAR ImageName[256];       // image name
    CHAR LoadedImageName[256]; // symbol file name
    // new elements: 07-Jun-2002
    CHAR LoadedPdbName[256];   // pdb file name
    DWORD CVSig;               // Signature of the CV record in the debug directories
    CHAR CVData[kMaxPath * 3]; // Contents of the CV record
    DWORD PdbSig;              // Signature of PDB
    GUID PdbSig70;             // Signature of PDB (VC 7 and up)
    DWORD PdbAge;              // DBI age of pdb
    BOOL PdbUnmatched;         // loaded an unmatched pdb
    BOOL DbgUnmatched;         // loaded an unmatched dbg
    BOOL LineNumbers;          // we have line number information
    BOOL GlobalSymbols;        // we have internal symbol information
    BOOL TypeInfo;             // we have type information
    // new elements: 17-Dec-2003
    BOOL SourceIndexed; // pdb supports source server
    BOOL Publics;       // contains public symbols
} IMAGEHLP_MODULE64_V3, *PIMAGEHLP_MODULE64_V3;

typedef struct _IMAGEHLP_MODULE64_V2
{
    DWORD SizeOfStruct;        // set to sizeof(IMAGEHLP_MODULE64)
    DWORD64 BaseOfImage;       // base load address of module
    DWORD ImageSize;           // virtual size of the loaded module
    DWORD TimeDateStamp;       // date/time stamp from pe header
    DWORD CheckSum;            // checksum from the pe header
    DWORD NumSyms;             // number of symbols in the symbol table
    SYM_TYPE SymType;          // type of symbols loaded
    CHAR ModuleName[32];       // module name
    CHAR ImageName[256];       // image name
    CHAR LoadedImageName[256]; // symbol file name
} IMAGEHLP_MODULE64_V2, *PIMAGEHLP_MODULE64_V2;
#pragma pack(pop)

using SymInitializeFn = BOOL(__stdcall*)(IN HANDLE process, IN LPCSTR UserSearchPath, IN BOOL fInvadeProcess);
using SymCleanupFn = BOOL(__stdcall*)(IN HANDLE process);
using SymGetSymFromAddr64Fn = BOOL(__stdcall*)(IN HANDLE process,
                                               IN DWORD64 dwAddr,
                                               OUT PDWORD64 pdwDisplacement,
                                               OUT PIMAGEHLP_SYMBOL64 Symbol);
using UnDecorateSymbolNameFn = DWORD(__stdcall WINAPI*)(PCSTR DecoratedName, PSTR UnDecoratedName, 
                                                        DWORD UndecoratedLength, DWORD Flags);
using SymGetLineFromAddr64Fn = BOOL(__stdcall*)(IN HANDLE process,
                                                IN DWORD64 dwAddr,
                                                OUT PDWORD pdwDisplacement,
                                                OUT PIMAGEHLP_LINE64 line);  

// DbgHelp Api functions
static SymInitializeFn _SymInitialize;
static SymCleanupFn _SymCleanup;
static SymGetSymFromAddr64Fn _SymGetSymFromAddr64;
static UnDecorateSymbolNameFn _UnDecorateSymbolName;
static SymGetLineFromAddr64Fn _SymGetLineFromAddr64;

static bool debugInitializeStacktrace();

struct DebugStacktraceContext
{
    bool initialized;
    HINSTANCE dbghelp;
    HANDLE process;
    CRITICAL_SECTION mutex;

    DebugStacktraceContext()
    {
        if constexpr (!CONFIG_FINAL_BUILD) {
            initialized = debugInitializeStacktrace();
            // TODO: I had to disable this, because in RenderDoc, this fails. Needs investigation
            // ASSERT(initialized);
        }
    }

    ~DebugStacktraceContext()
    {
        if (initialized) {
            ASSERT(dbghelp);
            ASSERT(_SymCleanup);

            EnterCriticalSection(&mutex);
            _SymCleanup(process);
            LeaveCriticalSection(&mutex);

            FreeLibrary(dbghelp);
            dbghelp = nullptr;

            // Note that we do not Delete the critical section here, because it will be used after deinitialization by Tracy
            // DeleteCriticalSection(&mutex);
        }
    }
};

static DebugStacktraceContext gStacktrace;

static bool debugInitializeStacktrace()
{
    if (gStacktrace.initialized)
        return true;

    ASSERT(gStacktrace.dbghelp == nullptr);

    // Load dbghelp.dll: this DLL should be provided and included in the repo
    gStacktrace.dbghelp = LoadLibraryA("dbghelp.dll");
    if (!gStacktrace.dbghelp) {
        debugPrint("Could not load DbgHelp.dll");
        return false;
    }

    _SymInitialize = (SymInitializeFn)GetProcAddress(gStacktrace.dbghelp, "SymInitialize");
    _SymCleanup = (SymCleanupFn)GetProcAddress(gStacktrace.dbghelp, "SymCleanup");
    _SymGetLineFromAddr64 = (SymGetLineFromAddr64Fn)GetProcAddress(gStacktrace.dbghelp, "SymGetLineFromAddr64");
    _SymGetSymFromAddr64 = (SymGetSymFromAddr64Fn)GetProcAddress(gStacktrace.dbghelp, "SymGetSymFromAddr64");
    _UnDecorateSymbolName = (UnDecorateSymbolNameFn)GetProcAddress(gStacktrace.dbghelp, "UnDecorateSymbolName");
    ASSERT(_SymInitialize && _SymCleanup && _SymGetLineFromAddr64 && _SymGetSymFromAddr64 && _UnDecorateSymbolName);

    gStacktrace.process = GetCurrentProcess();

    InitializeCriticalSectionAndSpinCount(&gStacktrace.mutex, 100);

    EnterCriticalSection(&gStacktrace.mutex);
    if (!_SymInitialize(gStacktrace.process, NULL, TRUE)) {
        LeaveCriticalSection(&gStacktrace.mutex);
        debugPrint("DbgHelp: _SymInitialize failed");
        return false;
    }
    LeaveCriticalSection(&gStacktrace.mutex);

    ASSERT(_SymInitialize && _SymCleanup && _SymGetLineFromAddr64 && _SymGetSymFromAddr64 && _UnDecorateSymbolName);

    return true;
}

NO_INLINE uint16 debugCaptureStacktrace(void** stackframes, uint16 maxStackframes, uint16 framesToSkip, uint32* pHash)
{
    static_assert(sizeof(DWORD) == sizeof(uint32));

    return (uint16)RtlCaptureStackBackTrace(framesToSkip, maxStackframes, stackframes, PDWORD(pHash));
}

void debugResolveStacktrace(uint16 numStacktrace, void* const* stackframes, DebugStacktraceEntry* entries)
{
    if (!gStacktrace.initialized) {
        gStacktrace.initialized = debugInitializeStacktrace();
        ASSERT_MSG(gStacktrace.initialized, "Failed to initialize stacktrace capture");
    }  

    IMAGEHLP_LINE64 line;
    uint8* symbolBuffer[sizeof(IMAGEHLP_SYMBOL64) + kMaxPath];
    IMAGEHLP_SYMBOL64* symbol = reinterpret_cast<IMAGEHLP_SYMBOL64*>(symbolBuffer);
    memset(symbol, 0, sizeof(IMAGEHLP_SYMBOL64) + kMaxPath);
    symbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
    symbol->MaxNameLength = kMaxPath;

    EnterCriticalSection(&gStacktrace.mutex);
    for (uint16 i = 0; i < numStacktrace; i++) {
        DebugStacktraceEntry entry = {};
        if (_SymGetSymFromAddr64(gStacktrace.process, (DWORD64)stackframes[i], &entry.offsetFromSymbol, symbol)) {
            strCopy(entry.name, sizeof(entry.name), symbol->Name);
        } 
        else {
            DWORD gle = GetLastError();
            if (gle != ERROR_INVALID_ADDRESS && gle != ERROR_MOD_NOT_FOUND) {
                debugPrint("_SymGetSymFromAddr64 failed");
                break;
            }
            strCopy(entry.name, sizeof(entry.name), "[NA]");
        }

        if (_SymGetLineFromAddr64(gStacktrace.process, (DWORD64)stackframes[i], (PDWORD)&(entry.offsetFromLine), &line)) {
            entry.line = line.LineNumber;
            strCopy(entry.filename, kMaxPath, line.FileName);
        } 
        else {
            DWORD gle = GetLastError();
            if (gle != ERROR_INVALID_ADDRESS && gle != ERROR_MOD_NOT_FOUND) {
                debugPrint("_SymGetLineFromAddr64 failed");
                break;
            }
            strCopy(entry.filename, kMaxPath, "[NA]");
        }

        memcpy(&entries[i], &entry, sizeof(DebugStacktraceEntry));
    }
    LeaveCriticalSection(&gStacktrace.mutex);
}

void debugStacktraceSaveStopPoint(void*)
{
}

#ifdef TRACY_ENABLE
void DbgHelpInit()
{
    if (!gStacktrace.initialized) {
        gStacktrace.initialized = debugInitializeStacktrace();
        ASSERT_MSG(gStacktrace.initialized, "Failed to initialize stacktrace capture");
    }  
}

void DbgHelpLock()
{
    EnterCriticalSection(&gStacktrace.mutex);
}

void DbgHelpUnlock()
{
    LeaveCriticalSection(&gStacktrace.mutex);
}
#endif


#endif // PLATFORM_WINDOWS
