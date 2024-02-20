#pragma once

#include <string.h>
#include <stdarg.h>

#include "Base.h"

struct Allocator;

API char*   strCopy(char* RESTRICT dst, uint32 dstSize, const char* RESTRICT src);
API char*   strCopyCount(char* RESTRICT dst, uint32 dstSize, const char* RESTRICT src, uint32 count);
API char*   strConcat(char* RESTRICT dst, uint32 dstSize, const char* RESTRICT src);
API char*   strConcatCount(char* RESTRICT dst, uint32 dstSize, const char* RESTRICT src, uint32 count);
NO_ASAN API uint32 strLen(const char* str);
API uint32  strPrintFmt(char* str, uint32 size, const char* fmt, ...);
API uint32  strPrintFmtArgs(char* str, uint32 size, const char* fmt, va_list args);
API char*   strPrintFmtAlloc(Allocator* alloc, const char* fmt, ...);
API char*   strPrintFmtAllocArgs(Allocator* alloc, const char* fmt, va_list args);
API bool    strUt8ToWide(const char* src, wchar_t* dst, size_t dstNumBytes);
API bool    strWideToUtf8(const wchar_t* src, char* dst, size_t dstNumBytes);
API bool    strIsEqual(const char* s1, const char* s2);
API bool    strIsEqualNoCase(const char* s1, const char* s2);
API bool    strIsEqualCount(const char* s1, const char* s2, uint32 count);
API bool    strIsEqualNoCaseCount(const char* s1, const char* s2, uint32 count);
API bool    strEndsWith(const char* str, const char* endsWith);
API char*   strTrim(char* dst, uint32 dstSize, const char* src);
API char*   strTrim(char* dst, uint32 dstSize, const char* src, char ch);
API char*   strRemoveWhitespace(char* dst, uint32 dstSize, const char* src);
API char*   strReplaceChar(char* dst, uint32 dstSize, char ch, char replaceWith);
API bool    strToBool(const char* str);
API int     strToInt(const char* str);
API uint32  strToUint(const char* str);
API uint64  strToUint64(const char* str);
API double  strToDouble(const char* str);

API bool    strIsWhitespace(char ch);
API char    strToLower(char ch);
API char    strToUpper(char ch);
API char    strIsInRange(char ch, char from, char to);
API char    strIsNumber(char ch);

API const char* strSkipWhitespace(const char* str);
API const char* strSkipChar(const char* str, char ch);
API char* strToUpper(char* dst, uint32 dstSize, const char* src);
API char* strToLower(char* dst, uint32 dstSize, const char* src);

NO_ASAN API const char* strFindChar(const char* str, char ch);
NO_ASAN API const char* strFindCharRev(const char* str, char ch);
NO_ASAN API const char* strFindStr(const char* RESTRICT str, const char* RESTRICT find);

// Returns indexes to the input string with 
API Span<char*> strSplit(const char* str, char ch, Allocator* alloc);
API Span<char*> strSplitWhitespace(const char* str, Allocator* alloc);

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
    bool EndsWith(char ch) const;
    bool EndsWith(const char* cstr) const;

    String<_Size>& Append(const char* cstr);
    String<_Size>& Append(const char* cstr, uint32 count);
    String<_Size>& Append(const String<_Size>& str);

    String<_Size>& Trim();
    String<_Size>& Trim(char ch);

    String<_Size> SubStr(uint32 start, uint32 end = UINT32_MAX);

protected:
    char mStr[_Size];
    uint32 mLen;
};

using String32 = String<32>;
using String64 = String<64>;

//------------------------------------------------------------------------
//
template <uint32 _Size>
inline String<_Size>::String() : mLen(0)
{
    memset(mStr, 0x0, sizeof(mStr));
}

template <uint32 _Size>
inline String<_Size>::String(const char* cstr)
{
    char* end = strCopy(mStr, _Size, cstr);
    mLen = PtrToInt<uint32>(reinterpret_cast<void*>(end - mStr));
}

template <uint32 _Size> 
inline String<_Size>::String(char ch)
{
    mStr[0] = ch;
    mStr[1] = '\0';
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
    char* end = strCopy(mStr, _Size, cstr);
    mLen = PtrToInt<uint32>(reinterpret_cast<void*>(end - mStr));
    return *this;
}

template <uint32 _Size> 
inline bool String<_Size>::operator==(const char* str) const
{
    return strIsEqual(mStr, str);
}

template <uint32 _Size> 
inline bool String<_Size>::operator==(const String<_Size>& str) const
{
    return mLen == str.mLen && memcmp(mStr, str.mStr, mLen) == 0;
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
    strPrintFmtArgs(mStr, _Size, fmt, args);
    va_end(args);
    return *this;
}

template <uint32 _Size> 
inline String<_Size> String<_Size>::Format(const char* fmt, ...)
{
    String<_Size> str;
    va_list args;
    va_start(args, fmt);
    strPrintFmtArgs(str.mStr, _Size, fmt, args);
    va_end(args);
    return str;
}

template <uint32 _Size> 
inline String<_Size>& String<_Size>::FormatArgsSelf(const char* fmt, va_list args)
{
    strPrintFmtArgs(mStr, _Size, fmt, args);
    return *this;
}

template <uint32 _Size> 
inline bool String<_Size>::IsEqual(const char* cstr) const
{
    return strIsEqual(mStr, cstr);
}

template <uint32 _Size>
inline bool String<_Size>::IsEqualNoCase(const char* cstr) const
{
    return strIsEqualNoCase(mStr, cstr);
}

template <uint32 _Size> 
inline bool String<_Size>::IsEqual(const char* cstr, uint32 count) const
{
    return strIsEqualCount(mStr, cstr, count);
}

template <uint32 _Size> 
inline uint32 String<_Size>::FindChar(char ch, uint32 startIndex) const
{
    ASSERT(startIndex <= mLen);
    const char* r = strFindChar(mStr + startIndex, ch);
    if (r)
        return PtrToInt<uint32>((void*)(r - mStr));
    else
        return UINT32_MAX;
}

template <uint32 _Size> 
inline uint32 String<_Size>::FindCharRev(char ch) const
{
    const char* r = strFindCharRev(mStr, ch);
    if (r)
        return PtrToInt<uint32>(r - mStr);
    else
        return UINT32_MAX;
}

template <uint32 _Size> 
inline uint32 String<_Size>::FindString(const char* cstr) const
{
    const char* r = strFindStr(mStr, cstr);
    if (r)
        return PtrToInt<uint32>(r - mStr);
    else
        return UINT32_MAX;
}

template <uint32 _Size> 
inline bool String<_Size>::EndsWith(char ch) const
{
    return mLen > 0 && mStr[mLen-1] == ch;
}

template <uint32 _Size> 
inline bool String<_Size>::EndsWith(const char* cstr) const
{
    uint32 cstrLen = strLen(cstr);
    return mLen >= cstrLen && strFindStr(&mStr[mLen - cstrLen], cstr) != nullptr;
}

template <uint32 _Size> 
inline String<_Size>& String<_Size>::Append(const char* cstr)
{
    strCopy(mStr + mLen, _Size - mLen, cstr);
    mLen += strLen(cstr);
    return *this;
}

template <uint32 _Size> 
inline String<_Size>& String<_Size>::Append(const char* cstr, uint32 count)
{
    ASSERT(mLen + count < _Size);
    strCopyCount(mStr + mLen, _Size - mLen, cstr, count);
    mLen += count;
    return *this;
}

template <uint32 _Size>
inline String<_Size>& String<_Size>::Append(const String<_Size>& str)
{
    ASSERT(mLen + str.mLen < _Size);
    strCopyCount(mStr + mLen, _Size - mLen, str.mStr, str.mLen);
    mLen += str.mLen;
    return *this;
}

template <uint32 _Size> 
inline String<_Size>& String<_Size>::Trim()
{
    strTrim(mStr, _Size, mStr);
    mLen = strLen(mStr);
    return *this;
}

template <uint32 _Size> 
inline String<_Size>& String<_Size>::Trim(char ch)
{
    strTrim(mStr, _Size, mStr, ch);
    mLen = strLen(mStr);
    return *this;
}

template <uint32 _Size>
inline String<_Size> String<_Size>::SubStr(uint32 start, uint32 end)
{
    end = end == UINT32_MAX ? mLen : end;

    String<_Size> r;
    strCopyCount(r.mStr, _Size, mStr + start, end - start);
    r.mLen = end - start;

    return r;
}

template <uint32 _Size>
inline uint32 String<_Size>::CalcLength()
{
    mLen = strLen(mStr);
    return mLen;
}
