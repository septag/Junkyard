#pragma once

#include "../Core/Base.h"
#include "CommonTypes.h"

enum class ClipboardVarType : uint32
{
    None = 0,
    Bool,
    Integer,
    Float,
    String,
    Pointer,
    Buffer
};

struct ClipboardVar
{
    ClipboardVarType type;

    union {
        uint32 bufferSize;
        uint32 stringLen;
    };

    union {
        bool valueBool;
        int64 valueInteger;
        double valueFloat;
        char* valueString;
        void* valuePointer;
    };

    void SetBool(bool value);
    void SetFloat(double value);
    void SetInt(int64 value);
    void SetString(const char* str, uint32 len = 0);
    void SetBuffer(const void* data, uint32 size);
    void SetPointer(void* ptr);

    bool GetBool() const;
    double GetFloat() const;
    int64 GetInt() const;
    const char* GetString() const;
    template <typename _T> Span<_T> GetBuffer();
    template <typename _T> _T* GetPointer();

private:
    void* GetBufferRaw(uint32* outSize);
    void* GetPointerRaw();
};

struct Allocator;

API bool clipboardInitialize(MemBumpAllocatorBase* alloc, bool debugAllocators = false);
API void clipboardRelease();

API ClipboardVarHandle clipboardAdd(const char* name, const ClipboardVar* var = nullptr);
API ClipboardVar& clipboardGet(ClipboardVarHandle handle);
API ClipboardVar& clipboardGet(const char* name);

API ClipboardVarHandle clipboardFind(const char* name);

//----------------------------------------------------------------------------------------------------------------------
//
template <typename _T> 
inline Span<_T> ClipboardVar::GetBuffer()
{
    ASSERT(type == ClipboardVarType::Buffer);
    return Span<_T>((_T*)valuePointer, bufferSize);
}

template <typename _T> 
inline _T* ClipboardVar::GetPointer()
{
    ASSERT(type == ClipboardVarType::Pointer);
    return (_T*)valuePointer;
}

inline bool ClipboardVar::GetBool() const
{
    ASSERT(type == ClipboardVarType::Bool);
    return valueBool;
}

inline double ClipboardVar::GetFloat() const
{
    ASSERT(type == ClipboardVarType::Float);
    return valueFloat;
}

inline int64 ClipboardVar::GetInt() const
{
    ASSERT(type == ClipboardVarType::Integer);
    return valueInteger;
}

inline const char* ClipboardVar::GetString() const
{
    ASSERT(type == ClipboardVarType::String);
    return valueString;
}
