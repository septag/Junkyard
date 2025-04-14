#include "StringUtil.h"

#include <string.h>
#include <stdlib.h>

#define STB_SPRINTF_IMPLEMENTATION
PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wunused-function")
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wunused-parameter")
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wsign-compare")
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wstrict-aliasing")
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wconditional-uninitialized")
#include "External/stb/stb_sprintf.h"
PRAGMA_DIAGNOSTIC_POP();
#undef STB_SPRINTF_IMPLEMENTATION

#ifdef BUILD_UNITY
    #if PLATFORM_WINDOWS
        #include "StringUtilWin.cpp"
    #endif
#endif

#include "Arrays.h"

namespace Str
{
    NO_ASAN INLINE uint32 LenCount(const char* str, uint32 _max)
    {
        const char* char_ptr;
        const uintptr* longWordPtr;
        uintptr longword, himagic, lomagic;

        for (char_ptr = str; ((uintptr)char_ptr & (sizeof(longword) - 1)) != 0; ++char_ptr) {
            if (*char_ptr == '\0') {
                uint32 _len = (uint32)(uintptr)(char_ptr - str);
                return (_len > _max) ? _max : _len;
            }
        }

        longWordPtr = (uintptr*)char_ptr;
        himagic = 0x80808080L;
        lomagic = 0x01010101L;
        #if ARCH_64BIT
        /* 64-bit version of the magic.  */
        /* Do the shift in two steps to avoid a warning if long has 32 bits.  */
        himagic = ((himagic << 16) << 16) | himagic;
        lomagic = ((lomagic << 16) << 16) | lomagic;
        #endif

        for (;;) {
            longword = *longWordPtr++;

            if (((longword - lomagic) & ~longword & himagic) != 0) {
                const char* cp = (const char*)(longWordPtr - 1);
                uint32 baseOffset = (uint32)(intptr_t)(cp - str);
                if (baseOffset > _max)
                    return _max;

                if (cp[0] == 0)
                    return Min(_max, baseOffset);
                if (cp[1] == 0)
                    return Min(_max, baseOffset + 1);
                if (cp[2] == 0)
                    return Min(_max, baseOffset + 2);
                if (cp[3] == 0)
                    return Min(_max, baseOffset + 3);
                #if ARCH_64BIT
                if (cp[4] == 0)
                    return Min(_max, baseOffset + 4);
                if (cp[5] == 0)
                    return Min(_max, baseOffset + 5);
                if (cp[6] == 0)
                    return Min(_max, baseOffset + 6);
                if (cp[7] == 0)
                    return Min(_max, baseOffset + 7);
                #endif // ARCH_64BIT
            }
        }

        #if !COMPILER_MSVC
        ASSERT_MSG(0, "Not a null-terminated string");
        return 0;
        #endif
    }
} // Str


uint32 Str::PrintFmt(char* str, uint32 size, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    uint32 r = Str::PrintFmtArgs(str, size, fmt, args);
    va_end(args);
    return r;
}

uint32 Str::PrintFmtArgs(char* str, uint32 size, const char* fmt, va_list args)
{
    return (uint32)stbsp_vsnprintf(str, (int)size, fmt, args);
}

char* Str::PrintFmtAlloc(MemAllocator* alloc, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char* str = Str::PrintFmtAllocArgs(alloc, fmt, args);
    va_end(args);
    return str;
}

char* Str::PrintFmtAllocArgs(MemAllocator* alloc, const char* fmt, va_list args)
{
    struct strPrintfContext
    {
        MemAllocator* alloc;
        char* buff;
        int len;
        char tmp[STB_SPRINTF_MIN];
    };

    auto strStbPrintfCallback = [](const char*, void* user, int len)->char*
    {
        strPrintfContext* ctx = reinterpret_cast<strPrintfContext*>(user);
        int len_ = len + 1;    // Reserve one character for null-termination
        ctx->buff = Mem::ReallocTyped<char>(ctx->buff, len_, ctx->alloc);
        memcpy(ctx->buff + ctx->len, ctx->tmp, len);
        ctx->len += len;
        return ctx->tmp;
    };
    
    strPrintfContext ctx;
    ctx.alloc = alloc;
    ctx.buff = nullptr;
    ctx.len = 0;
    stbsp_vsprintfcb(strStbPrintfCallback, &ctx, ctx.tmp, fmt, args);
    ctx.buff[ctx.len] = '\0';
    return ctx.buff;
}

char* Str::Copy(char* RESTRICT dst, uint32 dstSize, const char* RESTRICT src)
{
    ASSERT(dst);
    ASSERT(src);		

    const uint32 len = Str::Len(src);
    const uint32 max = dstSize ? dstSize - 1 : 0;
    const uint32 num = (len < max ? len : max);
    if (num > 0) {
        memcpy(dst, src, num);
    }
    dst[num] = '\0';

    return &dst[num];
}

// https://github.com/lattera/glibc/blob/master/string/strlen.c
uint32 Str::Len(const char* str)
{
    const char* char_ptr;
    const uintptr* longWordPtr;
    uintptr longword, himagic, lomagic;

    for (char_ptr = str; ((uintptr)char_ptr & (sizeof(longword) - 1)) != 0; ++char_ptr) {
        if (*char_ptr == '\0')
            return (uint32)(intptr_t)(char_ptr - str);
    }
    longWordPtr = (uintptr*)char_ptr;
    himagic = 0x80808080L;
    lomagic = 0x01010101L;
    #if ARCH_64BIT
    /* 64-bit version of the magic.  */
    /* Do the shift in two steps to avoid a warning if long has 32 bits.  */
    himagic = ((himagic << 16) << 16) | himagic;
    lomagic = ((lomagic << 16) << 16) | lomagic;
    #endif

    for (;;) {
        longword = *longWordPtr++;

        if (((longword - lomagic) & ~longword & himagic) != 0) {
            const char* cp = (const char*)(longWordPtr - 1);

            if (cp[0] == 0)
                return (uint32)(intptr_t)(cp - str);
            if (cp[1] == 0)
                return (uint32)(intptr_t)(cp - str + 1);
            if (cp[2] == 0)
                return (uint32)(intptr_t)(cp - str + 2);
            if (cp[3] == 0)
                return (uint32)(intptr_t)(cp - str + 3);
            #if ARCH_64BIT
            if (cp[4] == 0)
                return (uint32)(intptr_t)(cp - str + 4);
            if (cp[5] == 0)
                return (uint32)(intptr_t)(cp - str + 5);
            if (cp[6] == 0)
                return (uint32)(intptr_t)(cp - str + 6);
            if (cp[7] == 0)
                return (uint32)(intptr_t)(cp - str + 7);
            #endif // ARCH_64BIT
        }
    }

    #if !COMPILER_MSVC
        ASSERT_MSG(0, "Not a null-terminated string");
        return 0;
    #endif
}

char* Str::CopyCount(char* RESTRICT dst, uint32 dstSize, const char* RESTRICT src, uint32 count)
{
    ASSERT(dst);
    ASSERT(src);

    const uint32 len = LenCount(src, count);
    const uint32 max = dstSize ? dstSize - 1 : 0;
    const uint32 num = (len < max ? len : max);
    if (num > 0) 
        memcpy(dst, src, num);
    dst[num] = '\0';

    return &dst[num];
}

char* Str::Concat(char* RESTRICT dst, uint32 dstSize, const char* RESTRICT src)
{
    ASSERT(dst);
    ASSERT(src);

    uint32 len = Str::Len(dst);
    return Str::Copy(dst + len, dstSize - len, src);
}

char* Str::ConcatCount(char* RESTRICT dst, uint32 dstSize, const char* RESTRICT src, uint32 count)
{
    ASSERT(dst);
    ASSERT(src);

    uint32 len = Str::Len(dst);
    return Str::CopyCount(dst + len, dstSize - len, src, count);
}

bool Str::IsEqual(const char* s1, const char* s2)
{
    uint32 alen = Str::Len(s1);
    uint32 blen = Str::Len(s2);
    if (alen != blen)
        return false;

    for (uint32 i = 0; i < alen; i++) {
        if (s1[i] != s2[i])
            return false;
    }
    return true;
}

bool Str::IsEqualNoCase(const char* s1, const char* s2)
{
    uint32 alen = Str::Len(s1);
    uint32 blen = Str::Len(s2);
    if (alen != blen)
        return false;

    for (uint32 i = 0; i < alen; i++) {
        if (Str::ToLower(s1[i]) != Str::ToLower(s2[i]))
            return false;
    }
    return true;    
}

bool Str::IsEqualCount(const char* a, const char* b, uint32 count)
{
    uint32 _alen = Str::Len(a);
    uint32 _blen = Str::Len(b);
    uint32 alen = Min(count, _alen);
    uint32 blen = Min(count, _blen);
    if (alen != blen)
        return false;

    for (uint32 i = 0; i < alen; i++) {
        if (a[i] != b[i])
            return false;
    }
    return true;
}

bool Str::IsEqualNoCaseCount(const char* a, const char* b, uint32 count)
{
    uint32 _alen = Str::Len(a);
    uint32 _blen = Str::Len(b);
    uint32 alen = Min(count, _alen);
    uint32 blen = Min(count, _blen);
    if (alen != blen)
        return false;

    for (uint32 i = 0; i < alen; i++) {
        if (Str::ToLower(a[i]) != Str::ToLower(b[i]))
            return false;
    }
    return true;
}

int Str::Compare(const char* a, const char* b)
{
    return strcmp(a, b);
}

uint32 Str::CountMatchingFirstChars(const char* s1, const char* s2)
{
    uint32 count = 0;
    while (true) {
        if (s1[count] == 0 || s2[count] == 0 || s1[count] != s2[count])
            return count;
        count++;
    }
    
    return count;
}

bool Str::StartsWith(const char* str, const char* startsWith)
{
    uint32 len = Str::Len(str);
    uint32 startsWithLen = Str::Len(startsWith);
    if (startsWithLen > len)
        return false;

    return Str::IsEqualCount(str, startsWith, startsWithLen);
}

bool Str::EndsWith(const char* str, const char* endsWith)
{
    uint32 len = Str::Len(str);
    uint32 endsWithLen = Str::Len(endsWith);
    if (endsWithLen > len)
        return false;
    return Str::IsEqual(str + len - endsWithLen, endsWith);   
}

bool Str::IsWhitespace(char ch)
{
    return static_cast<uint32>(ch - 1) < 32 && ((0x80001F00 >> static_cast<uint32>(ch - 1)) & 1) == 1;
}

char Str::ToLower(char ch)
{
    return ch + (Str::IsInRange(ch, 'A', 'Z') ? 0x20 : 0);
}

char* Str::ToLower(char* dst, uint32 dstSize, const char* src)
{
    uint32 offset = 0;
    uint32 dstMax = dstSize - 1;
    while (*src && offset < dstMax) {
        dst[offset++] = Str::ToLower(*src);
        ++src;
    }
    dst[offset] = '\0';
    return dst;
}

char Str::ToUpper(char ch)
{
    return ch - (Str::IsInRange(ch, 'a', 'z') ? 0x20 : 0);
}

char* Str::ToUpper(char* dst, uint32 dstSize, const char* src)
{
    uint32 offset = 0;
    uint32 dstMax = dstSize - 1;
    while (*src && offset < dstMax) {
        dst[offset++] = Str::ToUpper(*src);
        ++src;
    }
    dst[offset] = '\0';
    return dst;
}

char Str::IsInRange(char ch, char from, char to)
{
    return static_cast<uint8>(ch - from) <= static_cast<uint8>(to - from);
}

char Str::IsNumber(char ch)
{
    return Str::IsInRange(ch, '0', '9');
}

char* Str::Trim(char* dst, uint32 dstSize, const char* src)
{
    uint32 len = Min(Str::Len(src), dstSize - 1);
    uint32 startOffset = 0;
    uint32 endOffset = len;
    
    // trim from start
    {
        for (uint32 i = 0; i < len; i++) {
            if (Str::IsWhitespace(src[i]))
                startOffset++;
            else
                break;
        }
    }

    // trim from the end
    if (len) {
        for (uint32 i = len; --i > 0; ) {
            if (!Str::IsWhitespace(src[i]))   {
                endOffset = i + 1;  
                break;
            }
        }
    }

    // copy the rest
    for (uint32 i = startOffset; i < endOffset; i++) 
        dst[i - startOffset] = src[i];

    dst[endOffset - startOffset] = '\0';
    return dst;
}

char* Str::Trim(char* dst, uint32 dstSize, const char* src, char ch)
{
    uint32 len = Min(Str::Len(src), dstSize - 1);
    uint32 startOffset = 0;
    uint32 endOffset = len;
    
    // trim from start
    {
        for (uint32 i = 0; i < len; i++) {
            if (src[i] == ch)
                startOffset++;
            else
                break;
        }
    }

    // trim from the end
    {
        for (uint32 i = len; --i > 0; ) {
            if (src[i] != ch)   {
                endOffset = i + 1;  
                break;
            }
        }
    }

    // copy the rest
    for (uint32 i = startOffset; i < endOffset; i++) 
        dst[i - startOffset] = src[i];

    dst[endOffset - startOffset] = '\0';
    return dst;
}

char* Str::RemoveWhitespace(char* dst, uint32 dstSize, const char* src)
{
    uint32 c = 0;
    while (*src) {
        if (!Str::IsWhitespace(*src)) {
            if (c < (dstSize - 1))
                dst[c++] = *src;
            else
                break;
        }
        src++;
    }
    dst[c] = '\0';
    return dst;
}

char* Str::RemoveChar(char* dst, uint32 dstSize, const char* src, char ch)
{
    uint32 c = 0;
    while (*src) {
        if (*src != ch) {
            if (c < (dstSize - 1))
                dst[c++] = *src;
            else
                break;
        }
        src++;
    }
    dst[c] = '\0';
    return dst;
}

// https://github.com/lattera/glibc/blob/master/string/strchr.c
const char* Str::FindChar(const char* str, char ch)
{
    const uint8* charPtr;
    uintptr* longwordPtr;
    uintptr longword, magicBits, charmask;
    uint8 c = (uint8)ch;

    // Handle the first few characters by reading one character at a time.
    // Do this until CHAR_PTR is aligned on a longword boundary.
    for (charPtr = (const uint8*)str;
         ((uintptr)charPtr & (sizeof(longword) - 1)) != 0; ++charPtr) {
        if (*charPtr == c)
            return (const char*)charPtr;
        else if (*charPtr == '\0')
            return nullptr;
    }

    longwordPtr = (uintptr*)charPtr;
    magicBits = (uintptr)-1;
    magicBits = magicBits / 0xff * 0xfe << 1 >> 1 | 1;
    charmask = c | (c << 8);
    charmask |= charmask << 16;
    #if ARCH_64BIT
        charmask |= (charmask << 16) << 16;
    #endif

    for (;;) {
        longword = *longwordPtr++;

        if ((((longword + magicBits) ^ ~longword) & ~magicBits) != 0 ||
            ((((longword ^ charmask) + magicBits) ^ ~(longword ^ charmask)) &
             ~magicBits) != 0) {
            const uint8* cp = (const uint8*)(longwordPtr - 1);

            if (*cp == c)
                return (const char*)cp;
            else if (*cp == '\0')
                break;
            if (*++cp == c)
                return (const char*)cp;
            else if (*cp == '\0')
                break;
            if (*++cp == c)
                return (const char*)cp;
            else if (*cp == '\0')
                break;
            if (*++cp == c)
                return (const char*)cp;
            else if (*cp == '\0')
                break;
            #if ARCH_64BIT
                if (*++cp == c)
                    return (const char*)cp;
                else if (*cp == '\0')
                    break;
                if (*++cp == c)
                    return (const char*)cp;
                else if (*cp == '\0')
                    break;
                if (*++cp == c)
                    return (const char*)cp;
                else if (*cp == '\0')
                    break;
                if (*++cp == c)
                    return (const char*)cp;
                else if (*cp == '\0')
                    break;
            #endif
        }
    }

    return nullptr;
}

const char* Str::FindCharRev(const char* str, char ch)
{
    const char *found = nullptr, *p;
    ch = (uint8)ch;
    
    if (ch == '\0')
        return Str::FindChar(str, '\0');
    while ((p = Str::FindChar(str, ch)) != NULL) {
        found = p;
        str = p + 1;
    }
    return (const char*)found;
}

const char* Str::FindStr(const char* RESTRICT str, const char* RESTRICT find)
{
    ASSERT(str);
    ASSERT(find);
    
    char ch = find[0];
    const char* _start = Str::FindChar(str, ch);
    uint32 find_len = Str::Len(find);
    uint32 len = Str::Len(str);
    
    while (_start) {
        // We have the first character, check the rest
        len -= (uint32)(intptr_t)(_start - str);
        if (len < find_len)
            return NULL;
        str = _start;
        
        if (memcmp(_start, find, find_len) == 0)
            return str;
        
        _start = Str::FindChar(_start + 1, ch);
    }
    
    return nullptr;
}

bool Str::ToBool(const char* str)
{
    if (!str || str[0] == '\0')
        return false;

    if (Str::IsEqualNoCase(str, "true") || Str::IsEqualNoCase(str, "on") || str[0] == '1')
        return true;

    return false;    
}

int Str::ToInt(const char* str)
{
    return atoi(str);
}

uint32 Str::ToUint(const char* str, uint32 radix)
{
    return static_cast<uint32>(strtoul(str, nullptr, radix));
}

uint64 Str::ToUint64(const char* str, uint32 radix)
{
    return static_cast<uint64>(strtoull(str, nullptr, radix));
}

double Str::ToDouble(const char* str)
{
    return strtod(str, nullptr);
}

char* Str::ReplaceChar(char* dst, uint32 dstSize, char ch, char replaceWith)
{
    char* s = dst;
    uint32 count = 0; 
    while (*s != 0 && count++ < dstSize) {
        if (*s == ch)
            *s = replaceWith;
        ++s;
    }
    return dst;
}

char* Str::SubStr(char* dst, uint32 dstSize, const char* str, uint32 startIdx, uint32 endIdx)
{
    if (endIdx == 0)
        endIdx = Str::Len(str);

    ASSERT(startIdx < endIdx);

    uint32 srcCopyLen = Min(endIdx - startIdx, dstSize - 1);
    memcpy(dst, str + startIdx, srcCopyLen);
    dst[srcCopyLen] = '\0';

    return dst;
}

const char* Str::SkipWhitespace(const char* str)
{
    while (*str) {
        if (Str::IsWhitespace(*str))
            ++str;
        else
            break;
    }
    return str;
}

const char* Str::SkipChar(const char* str, char ch)
{
    while (*str) {
        if (*str == ch)
            ++str;
        else
            break;
    }
    return str;
}

Str::SplitResult Str::Split(const char* str, char ch, MemAllocator* alloc)
{
    Array<char*> splits(alloc);
    char* strCopy = Mem::AllocCopy<char>(str, Str::Len(str) + 1, alloc);

    char* s = strCopy;
    char* start = s;
    while (*s) {
        if (*s == ch) {
            if (start != s) {
                *(s++) = 0;
                splits.Push(start);
            }

            s = const_cast<char*>(Str::SkipChar(s, ch));
            start = s;
        }
        else {
            ++s;
        }
    }

    if (start < s) 
        splits.Push(start);

    Str::SplitResult r {
        .buffer = strCopy,
        .splits = splits.Detach()
    };

    return r;
}

Str::SplitResult Str::SplitWhitespace(const char* str, MemAllocator* alloc)
{
    Array<char*> splits(alloc);
    char* strCopy = Mem::AllocCopy<char>(str, Str::Len(str) + 1, alloc);

    char* s = strCopy;
    char* start = s;
    while (*s) {
        if (Str::IsWhitespace(*s)) {
            if (start != s) {
                *(s++) = 0;
                splits.Push(start);
            }

            s = const_cast<char*>(Str::SkipWhitespace(s));
            start = s;
        }
        else {
            ++s;
        }
    }

    if (start != s) 
        splits.Push(start);

    Str::SplitResult r {
        .buffer = strCopy,
        .splits = splits.Detach()
    };

    return r;
}

void Str::FreeSplitResult(SplitResult& sres, MemAllocator* alloc)
{
    Mem::Free(sres.buffer, alloc);
    Mem::Free(sres.splits.Ptr(), alloc);
}