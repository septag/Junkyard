#pragma once

#include <string.h>
#include <stdarg.h>

#include "Base.h"

struct MemAllocator;

namespace Str
{
    API char* Copy(char* RESTRICT dst, uint32 dstSize, const char* RESTRICT src);
    API char* CopyCount(char* RESTRICT dst, uint32 dstSize, const char* RESTRICT src, uint32 count);
    API char* Concat(char* RESTRICT dst, uint32 dstSize, const char* RESTRICT src);
    API char* ConcatCount(char* RESTRICT dst, uint32 dstSize, const char* RESTRICT src, uint32 count);
    NO_ASAN API uint32 Len(const char* str);
    API uint32 PrintFmt(char* str, uint32 size, const char* fmt, ...);
    API uint32 PrintFmtArgs(char* str, uint32 size, const char* fmt, va_list args);
    API char* PrintFmtAlloc(MemAllocator* alloc, const char* fmt, ...);
    API char* PrintFmtAllocArgs(MemAllocator* alloc, const char* fmt, va_list args);
    API bool Utf8ToWide(const char* src, wchar_t* dst, size_t dstNumBytes);
    API bool WideToUtf8(const wchar_t* src, char* dst, size_t dstNumBytes);
    API bool IsEqual(const char* s1, const char* s2);
    API bool IsEqualNoCase(const char* s1, const char* s2);
    API bool IsEqualCount(const char* s1, const char* s2, uint32 count);
    API bool IsEqualNoCaseCount(const char* s1, const char* s2, uint32 count);
    API int Compare(const char* a, const char* b);
    API uint32 CountMatchingFirstChars(const char* s1, const char* s2);
    API bool StartsWith(const char* str, const char* startsWith);
    API bool EndsWith(const char* str, const char* endsWith);
    API char* Trim(char* dst, uint32 dstSize, const char* src);
    API char* Trim(char* dst, uint32 dstSize, const char* src, char ch);
    API char* RemoveWhitespace(char* dst, uint32 dstSize, const char* src);
    API char* RemoveChar(char* dst, uint32 dstSize, const char* src, char ch);
    API char* ReplaceChar(char* dst, uint32 dstSize, char ch, char replaceWith);
    API char* SubStr(char* dst, uint32 dstSize, const char* str, uint32 startIdx, uint32 endIdx = 0);
    API bool ToBool(const char* str);
    API int ToInt(const char* str);
    API uint32 ToUint(const char* str, uint32 radix = 10);
    API uint64 ToUint64(const char* str, uint32 radix = 10);
    API double ToDouble(const char* str);

    API bool IsWhitespace(char ch);
    API char ToLower(char ch);
    API char ToUpper(char ch);
    API char IsInRange(char ch, char from, char to);
    API char IsNumber(char ch);

    API const char* SkipWhitespace(const char* str);
    API const char* SkipChar(const char* str, char ch);
    API char* ToUpper(char* dst, uint32 dstSize, const char* src);
    API char* ToLower(char* dst, uint32 dstSize, const char* src);

    NO_ASAN API const char* FindChar(const char* str, char ch);
    NO_ASAN API const char* FindCharRev(const char* str, char ch);
    NO_ASAN API const char* FindStr(const char* RESTRICT str, const char* RESTRICT find);

    
    struct SplitResult
    {
        char* buffer;
        Span<char*> splits;
    };

    API SplitResult Split(const char* str, char ch, MemAllocator* alloc, bool acceptEmptySplits = false);
    API SplitResult SplitWhitespace(const char* str, MemAllocator* alloc);
    API void FreeSplitResult(SplitResult& sres, MemAllocator* alloc);
} // Str


template <uint32 _Size>
struct String 
{
    String();
    String(const char* cstr);
    String(char ch);
    String(const String<_Size>& str);

    String<_Size>& operator=(const String<_Size>& str);
    String<_Size>& operator=(const char* cstr);

    bool operator==(const char* str) const;
    bool operator==(const String<_Size>& str) const;
    bool operator!=(const char* str) const;
    bool operator!=(const String<_Size>& str) const;

    bool IsEmpty() const;
    uint32 Length() const;
    uint32 Capacity() const;
    char* Ptr();
    const char* CStr() const;
    uint32 CalcLength();
    char operator[](uint32 index) const;
    char& operator[](uint32 index);

    String<_Size>& FormatSelf(const char* fmt, ...);
    String<_Size>& FormatArgsSelf(const char* fmt, va_list args);

    static String<_Size> Format(const char* fmt, ...);

    bool IsEqual(const char* cstr) const;
    bool IsEqualNoCase(const char* cstr) const;
    bool IsEqual(const char* cstr, uint32 count) const;

    uint32 FindChar(char ch, uint32 startIndex = 0) const;
    uint32 FindCharRev(char ch) const;
    uint32 FindString(const char* cstr) const;
    bool StartsWith(char ch) const;
    bool StartsWith(const char* cstr) const;
    bool EndsWith(char ch) const;
    bool EndsWith(const char* cstr) const;

    String<_Size>& Append(const char* cstr);
    String<_Size>& Append(const char* cstr, uint32 count);
    String<_Size>& Append(const String<_Size>& str);

    String<_Size>& Trim();
    String<_Size>& Trim(char ch);

    String<_Size> SubStr(uint32 start, uint32 end = 0);

protected:
    char mStr[_Size];
    uint32 mLen;
};

using String32 = String<32>;
using String64 = String<64>;

//    ██╗███╗   ██╗██╗     ██╗███╗   ██╗███████╗███████╗
//    ██║████╗  ██║██║     ██║████╗  ██║██╔════╝██╔════╝
//    ██║██╔██╗ ██║██║     ██║██╔██╗ ██║█████╗  ███████╗
//    ██║██║╚██╗██║██║     ██║██║╚██╗██║██╔══╝  ╚════██║
//    ██║██║ ╚████║███████╗██║██║ ╚████║███████╗███████║
//    ╚═╝╚═╝  ╚═══╝╚══════╝╚═╝╚═╝  ╚═══╝╚══════╝╚══════╝

template <uint32 _Size>
inline String<_Size>::String() : mLen(0)
{
    memset(mStr, 0x0, sizeof(mStr));
}

template <uint32 _Size>
inline String<_Size>::String(const char* cstr)
{
    char* end = Str::Copy(mStr, _Size, cstr);
    mLen = PtrToInt<uint32>(reinterpret_cast<void*>(end - mStr));
    memset(end, 0x0, _Size - mLen);
}

template <uint32 _Size> 
inline String<_Size>::String(char ch)
{
    memset(mStr, 0x0, sizeof(mStr));
    mStr[0] = ch;
    mLen = 1;
}

template <uint32 _Size> 
inline String<_Size>::String(const String<_Size>& str)
{
    memcpy(this, &str, sizeof(str));
    mLen = str.mLen;
}

template <uint32 _Size> 
inline String<_Size>& String<_Size>::operator=(const String<_Size>& str)
{
    memcpy(this, &str, sizeof(str));
    return *this;
}

template <uint32 _Size> 
inline String<_Size>& String<_Size>::operator=(const char* cstr)
{
    char* end = Str::Copy(mStr, _Size, cstr);
    mLen = PtrToInt<uint32>(reinterpret_cast<void*>(end - mStr));
    return *this;
}

template <uint32 _Size> 
inline bool String<_Size>::operator==(const char* str) const
{
    return Str::IsEqual(mStr, str);
}

template <uint32 _Size> 
inline bool String<_Size>::operator==(const String<_Size>& str) const
{
    return mLen == str.mLen && memcmp(mStr, str.mStr, mLen) == 0;
}

template <uint32 _Size> 
inline bool String<_Size>::operator!=(const char* str) const
{
    return !Str::IsEqual(mStr, str);
}

template <uint32 _Size> 
inline bool String<_Size>::operator!=(const String<_Size>& str) const
{
    return mLen != str.mLen || memcmp(mStr, str.mStr, mLen) != 0;
}

template <uint32 _Size> 
inline bool String<_Size>::IsEmpty() const
{
    return mLen == 0;
}

template <uint32 _Size> 
inline uint32 String<_Size>::Length() const
{
    return mLen;
}

template <uint32 _Size> 
inline uint32 String<_Size>::Capacity() const
{
    return _Size;
}

template <uint32 _Size> 
inline char* String<_Size>::Ptr()
{
    return mStr;
}

template <uint32 _Size> 
inline const char* String<_Size>::CStr() const
{
    return mStr;
}

template <uint32 _Size> 
inline char String<_Size>::operator[](uint32 index) const
{
    ASSERT(index < mLen);
    return mStr[index];
}

template <uint32 _Size> 
inline char& String<_Size>::operator[](uint32 index)
{
    ASSERT(index < mLen);
    return mStr[index];
}

template <uint32 _Size> 
inline String<_Size>& String<_Size>::FormatSelf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    Str::PrintFmtArgs(mStr, _Size, fmt, args);
    va_end(args);
    mLen = Str::Len(mStr);
    return *this;
}

template <uint32 _Size> 
inline String<_Size> String<_Size>::Format(const char* fmt, ...)
{
    String<_Size> str;
    va_list args;
    va_start(args, fmt);
    Str::PrintFmtArgs(str.mStr, _Size, fmt, args);
    va_end(args);
    str.mLen = Str::Len(str.mStr);
    return str;
}

template <uint32 _Size> 
inline String<_Size>& String<_Size>::FormatArgsSelf(const char* fmt, va_list args)
{
    Str::PrintFmtArgs(mStr, _Size, fmt, args);
    return *this;
}

template <uint32 _Size> 
inline bool String<_Size>::IsEqual(const char* cstr) const
{
    return Str::IsEqual(mStr, cstr);
}

template <uint32 _Size>
inline bool String<_Size>::IsEqualNoCase(const char* cstr) const
{
    return Str::IsEqualNoCase(mStr, cstr);
}

template <uint32 _Size> 
inline bool String<_Size>::IsEqual(const char* cstr, uint32 count) const
{
    return Str::IsEqualCount(mStr, cstr, count);
}

template <uint32 _Size> 
inline uint32 String<_Size>::FindChar(char ch, uint32 startIndex) const
{
    ASSERT(startIndex <= mLen);
    const char* r = Str::FindChar(mStr + startIndex, ch);
    if (r)
        return PtrToInt<uint32>((void*)(r - mStr));
    else
        return UINT32_MAX;
}

template <uint32 _Size> 
inline uint32 String<_Size>::FindCharRev(char ch) const
{
    const char* r = Str::FindCharRev(mStr, ch);
    if (r)
        return PtrToInt<uint32>(r - mStr);
    else
        return UINT32_MAX;
}

template <uint32 _Size> 
inline uint32 String<_Size>::FindString(const char* cstr) const
{
    const char* r = Str::FindStr(mStr, cstr);
    if (r)
        return PtrToInt<uint32>(r - mStr);
    else
        return UINT32_MAX;
}

template <uint32 _Size> 
bool String<_Size>::StartsWith(char ch) const
{
    return mLen > 0 && mStr[0] == ch;
}

template <uint32 _Size> 
bool String<_Size>::StartsWith(const char* cstr) const
{
    uint32 cstrLen = Str::Len(cstr);
    return mLen >= cstrLen && Str::IsEqualCount(mStr, cstr, cstrLen);
}

template <uint32 _Size> 
inline bool String<_Size>::EndsWith(char ch) const
{
    return mLen > 0 && mStr[mLen-1] == ch;
}

template <uint32 _Size> 
inline bool String<_Size>::EndsWith(const char* cstr) const
{
    uint32 cstrLen = Str::Len(cstr);
    return mLen >= cstrLen && Str::FindStr(&mStr[mLen - cstrLen], cstr) != nullptr;
}

template <uint32 _Size> 
inline String<_Size>& String<_Size>::Append(const char* cstr)
{
    Str::Copy(mStr + mLen, _Size - mLen, cstr);
    mLen += Str::Len(cstr);
    return *this;
}

template <uint32 _Size> 
inline String<_Size>& String<_Size>::Append(const char* cstr, uint32 count)
{
    ASSERT(mLen + count < _Size);
    Str::CopyCount(mStr + mLen, _Size - mLen, cstr, count);
    mLen += count;
    return *this;
}

template <uint32 _Size>
inline String<_Size>& String<_Size>::Append(const String<_Size>& str)
{
    ASSERT(mLen + str.mLen < _Size);
    Str::CopyCount(mStr + mLen, _Size - mLen, str.mStr, str.mLen);
    mLen += str.mLen;
    return *this;
}

template <uint32 _Size> 
inline String<_Size>& String<_Size>::Trim()
{
    Str::Trim(mStr, _Size, mStr);
    mLen = Str::Len(mStr);
    return *this;
}

template <uint32 _Size> 
inline String<_Size>& String<_Size>::Trim(char ch)
{
    Str::Trim(mStr, _Size, mStr, ch);
    mLen = Str::Len(mStr);
    return *this;
}

template <uint32 _Size>
inline String<_Size> String<_Size>::SubStr(uint32 start, uint32 end)
{
    end = (end == 0) ? mLen : end;

    String<_Size> r;
    Str::CopyCount(r.mStr, _Size, mStr + start, end - start);
    r.mLen = end - start;

    return r;
}

template <uint32 _Size>
inline uint32 String<_Size>::CalcLength()
{
    mLen = Str::Len(mStr);
    return mLen;
}
