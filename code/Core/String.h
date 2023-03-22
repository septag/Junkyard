#pragma once

#include <string.h>
#include <stdarg.h>

#include "Base.h"

struct Allocator;

API char*   strCopy(char* RESTRICT dst, uint32 dstSize, const char* RESTRICT src);
API char*   strCopyCount(char* RESTRICT dst, uint32 dstSize, const char* RESTRICT src, uint32 count);
API char*   strConcat(char* RESTRICT dst, uint32 dstSize, const char* RESTRICT src);
API char*   strConcatCount(char* RESTRICT dst, uint32 dstSize, const char* RESTRICT src, uint32 count);
NO_ASAN API uint32  strLen(const char* str);
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
API bool    strTrim(char* dst, uint32 dstSize, const char* src);
API bool    strTrim(char* dst, uint32 dstSize, const char* src, char ch);
API void    strReplaceChar(char* dst, uint32 dstSize, char ch, char replaceWith);
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
API char* strToUpper(char* dst, uint32 dstSize, const char* src);

NO_ASAN API const char* strFindChar(const char* str, char ch);
NO_ASAN API const char* strFindCharRev(const char* str, char ch);
NO_ASAN API const char* strFindStr(const char* RESTRICT str, const char* RESTRICT find);


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
    char* Ptr();
    const char* CStr() const;
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
    char _str[_Size];
    uint32 _len;
};

using String32 = String<32>;
using String64 = String<64>;

//------------------------------------------------------------------------
//
template <uint32 _Size> 
String<_Size>::String()
{
    _str[0] = '\0';
    _len = 0;
}

template <uint32 _Size> 
inline String<_Size>::String(const char* cstr)
{
    char* end = strCopy(_str, _Size, cstr);
    _len = PtrToInt<uint32>(reinterpret_cast<void*>(end - _str));
}

template <uint32 _Size> 
inline String<_Size>::String(char ch)
{
    _str[0] = ch;
    _str[1] = '\0';
    _len = 1;
}

template <uint32 _Size> 
inline String<_Size>::String(const String<_Size>& str)
{
    memcpy(this, &str, sizeof(str));
    _len = str._len;
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
    char* end = strCopy(_str, _Size, cstr);
    _len = PtrToInt<uint32>(reinterpret_cast<void*>(end - _str));
    return *this;
}

template <uint32 _Size> 
inline bool String<_Size>::operator==(const char* str) const
{
    return strIsEqual(_str, str);
}

template <uint32 _Size> 
inline bool String<_Size>::operator==(const String<_Size>& str) const
{
    return _len == str._len && memcmp(_str, str._str, _len) == 0;
}

template <uint32 _Size> 
inline bool String<_Size>::operator!=(const String<_Size>& str) const
{
    return _len != str._len || memcmp(_str, str._str, _len) != 0;
}

template <uint32 _Size> 
inline bool String<_Size>::IsEmpty() const
{
    return _len == 0;
}

template <uint32 _Size> 
inline uint32 String<_Size>::Length() const
{
    return _len;
}

template <uint32 _Size> 
inline char* String<_Size>::Ptr()
{
    return _str;
}

template <uint32 _Size> 
inline const char* String<_Size>::CStr() const
{
    return _str;
}

template <uint32 _Size> 
inline char String<_Size>::operator[](uint32 index) const
{
    ASSERT(index < _len);
    return _str[index];
}

template <uint32 _Size> 
inline char& String<_Size>::operator[](uint32 index)
{
    ASSERT(index < _len);
    return _str[index];
}

template <uint32 _Size> 
inline String<_Size>& String<_Size>::FormatSelf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    strPrintFmtArgs(_str, _Size, fmt, args);
    va_end(args);
    return *this;
}

template <uint32 _Size> 
inline String<_Size> String<_Size>::Format(const char* fmt, ...)
{
    String<_Size> str;
    va_list args;
    va_start(args, fmt);
    strPrintFmtArgs(str._str, _Size, fmt, args);
    va_end(args);
    return str;
}

template <uint32 _Size> 
inline String<_Size>& String<_Size>::FormatArgsSelf(const char* fmt, va_list args)
{
    strPrintFmtArgs(_str, _Size, fmt, args);
    return *this;
}

template <uint32 _Size> 
inline bool String<_Size>::IsEqual(const char* cstr) const
{
    return strIsEqual(_str, cstr);
}

template <uint32 _Size>
inline bool String<_Size>::IsEqualNoCase(const char* cstr) const
{
    return strIsEqualNoCase(_str, cstr);
}

template <uint32 _Size> 
inline bool String<_Size>::IsEqual(const char* cstr, uint32 count) const
{
    return strIsEqualCount(_str, cstr, count);
}

template <uint32 _Size> 
inline uint32 String<_Size>::FindChar(char ch, uint32 startIndex) const
{
    ASSERT(startIndex <= _len);
    const char* r = strFindChar(_str + startIndex, ch);
    if (r)
        return PtrToInt<uint32>((void*)(r - _str));
    else
        return UINT32_MAX;
}

template <uint32 _Size> 
inline uint32 String<_Size>::FindCharRev(char ch) const
{
    const char* r = strFindCharRev(_str, ch);
    if (r)
        return PtrToInt<uint32>(r - _str);
    else
        return UINT32_MAX;
}

template <uint32 _Size> 
inline uint32 String<_Size>::FindString(const char* cstr) const
{
    const char* r = strFindStr(_str, cstr);
    if (r)
        return PtrToInt<uint32>(r - _str);
    else
        return UINT32_MAX;
}

template <uint32 _Size> 
inline bool String<_Size>::EndsWith(char ch) const
{
    return _len > 0 && _str[_len-1] == ch;
}

template <uint32 _Size> 
inline bool String<_Size>::EndsWith(const char* cstr) const
{
    uint32 cstrLen = strLen(cstr);
    return _len >= cstrLen && strFindStr(&_str[_len - cstrLen], cstr) != nullptr;
}

template <uint32 _Size> 
inline String<_Size>& String<_Size>::Append(const char* cstr)
{
    strCopy(_str + _len, _Size - _len, cstr);
    _len += strLen(cstr);
    return *this;
}

template <uint32 _Size> 
inline String<_Size>& String<_Size>::Append(const char* cstr, uint32 count)
{
    ASSERT(_len + count < _Size);
    strCopyCount(_str + _len, _Size - _len, cstr, count);
    _len += count;
    return *this;
}

template <uint32 _Size>
inline String<_Size>& String<_Size>::Append(const String<_Size>& str)
{
    ASSERT(_len + str._len < _Size);
    strCopyCount(_str + _len, _Size - _len, str._str, str._len);
    _len += str._len;
    return *this;
}

template <uint32 _Size> 
inline String<_Size>& String<_Size>::Trim()
{
    strTrim(_str, _Size, _str);
    _len = strLen(_str);
    return *this;
}

template <uint32 _Size> 
inline String<_Size>& String<_Size>::Trim(char ch)
{
    strTrim(_str, _Size, _str, ch);
    _len = strLen(_str);
    return *this;
}

template <uint32 _Size>
inline String<_Size> String<_Size>::SubStr(uint32 start, uint32 end)
{
    end = end == UINT32_MAX ? _len : end;

    String<_Size> r;
    strCopyCount(r._str, _Size, _str + start, end - start);
    r._len = end - start;

    return r;
}