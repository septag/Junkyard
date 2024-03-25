#include "Clipboard.h"

#include "../Core/Hash.h"
#include "../Core/StringUtil.h"
#include "../Core/System.h"

namespace _limits
{
    static constexpr uint32 kMaxClipboardVars = 512;
    static constexpr uint32 kMaxClipboardScrapBufferSize = kMB;
}

struct ClipboardContext
{
    SpinLockMutex lock;
    MemThreadSafeAllocator scrapAlloc;
    MemTlsfAllocator tlsfAlloc;

    HashTable<ClipboardVarHandle> nameToHandle;
    HandlePool<ClipboardVarHandle, ClipboardVar> vars;
    size_t initHeapStart;
    size_t initHeapSize;
};

static ClipboardContext gClipboard;

bool clipboardInitialize(MemBumpAllocatorBase* alloc, bool debugAllocations)
{
    gClipboard.initHeapStart = alloc->GetOffset();

    {
        size_t bufferSize = HashTable<ClipboardVarHandle>::GetMemoryRequirement(_limits::kMaxClipboardVars);
        gClipboard.nameToHandle.Reserve(_limits::kMaxClipboardVars, memAlloc(bufferSize, alloc), bufferSize);
    }

    {
        size_t bufferSize = HandlePool<ClipboardVarHandle, ClipboardVar>::GetMemoryRequirement(_limits::kMaxClipboardVars);
        gClipboard.vars.Reserve(_limits::kMaxClipboardVars, memAlloc(bufferSize, alloc), bufferSize);
    }

    {
        size_t bufferSize = MemTlsfAllocator::GetMemoryRequirement(_limits::kMaxClipboardScrapBufferSize);
        gClipboard.tlsfAlloc.Initialize(_limits::kMaxClipboardScrapBufferSize, memAlloc(bufferSize, alloc), bufferSize, debugAllocations);
        gClipboard.scrapAlloc.SetAllocator(&gClipboard.tlsfAlloc);
    }

    gClipboard.initHeapSize = alloc->GetOffset() - gClipboard.initHeapSize;

    return true;
}

void clipboardRelease()
{
}

ClipboardVar& clipboardGet(ClipboardVarHandle handle)
{
    ASSERT(handle.IsValid());
    SpinLockMutexScope lock(gClipboard.lock);
    ASSERT(gClipboard.vars.IsValid(handle));
    return gClipboard.vars.Data(handle);
}

ClipboardVar& clipboardGet(const char* name)
{
    SpinLockMutexScope lock(gClipboard.lock);
    
    uint32 index = gClipboard.nameToHandle.Find(hashFnv32Str(name));
    if (index != INVALID_INDEX) {
        ClipboardVarHandle handle = gClipboard.nameToHandle.Get(index);
        ASSERT(gClipboard.vars.IsValid(handle));
        return gClipboard.vars.Data(handle);
    }
    else {
        ASSERT_MSG(0, "Could not find clipboard item '%s'. You have to add it first.", name);
        static ClipboardVar DummyVar {};
        return DummyVar;
    }
}

ClipboardVarHandle clipboardAdd(const char* name, const ClipboardVar* var)
{
    SpinLockMutexScope lock(gClipboard.lock);

    ClipboardVar prevValue;
    static ClipboardVar DummyVar {};
    ClipboardVarHandle handle = gClipboard.vars.Add(var ? *var : DummyVar, &prevValue);
    if (prevValue.type == ClipboardVarType::Buffer || prevValue.type == ClipboardVarType::String)
        memFree(prevValue.valuePointer, &gClipboard.scrapAlloc);
    gClipboard.nameToHandle.Add(hashFnv32Str(name), handle);
    return handle;
}

ClipboardVarHandle clipboardFind(const char* name)
{
    SpinLockMutexScope lock(gClipboard.lock);
    return gClipboard.nameToHandle.FindAndFetch(hashFnv32Str(name), ClipboardVarHandle());
}

void ClipboardVar::SetBool(bool value)
{
    ASSERT(type == ClipboardVarType::Bool || type == ClipboardVarType::None);
    type = ClipboardVarType::Bool;
    valueBool = value;
}

void ClipboardVar::SetFloat(double value)
{
    ASSERT(type == ClipboardVarType::Bool || type == ClipboardVarType::None);
    type = ClipboardVarType::Float;
    valueFloat = value;
}

void ClipboardVar::SetInt(int64 value)
{
    ASSERT(type == ClipboardVarType::Integer || type == ClipboardVarType::None);
    type = ClipboardVarType::Integer;
    valueInteger = value;
}

void ClipboardVar::SetString(const char* str, uint32 len)
{
    ASSERT(type == ClipboardVarType::String || type == ClipboardVarType::None);
    ASSERT(str);
    type = ClipboardVarType::String;
    if (len == 0)
        len = strLen(str);
    char* oldString = valueString;
    valueString = memAllocCopy<char>(str, len + 1, &gClipboard.scrapAlloc);
    memFree(oldString, &gClipboard.scrapAlloc);
}

void ClipboardVar::SetBuffer(const void* data, uint32 size)
{
    ASSERT(type == ClipboardVarType::Buffer || type == ClipboardVarType::None);
    ASSERT(size);
    ASSERT(data);
    type = ClipboardVarType::Buffer;
    void* oldBuffer = valuePointer;
    valuePointer = memAllocCopyRawBytes(data, size, &gClipboard.scrapAlloc);
    memFree(oldBuffer, &gClipboard.scrapAlloc);
}

void ClipboardVar::SetPointer(void* ptr)
{
    ASSERT(type == ClipboardVarType::Pointer || type == ClipboardVarType::None);
    type = ClipboardVarType::Pointer;
    valuePointer = ptr;
}

