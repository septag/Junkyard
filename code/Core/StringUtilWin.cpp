#include "StringUtil.h"

#if PLATFORM_WINDOWS

#include "IncludeWin.h"

bool strUt8ToWide(const char* src, wchar_t* dst, size_t dstNumBytes)
{
	ASSERT(src && dst && (dstNumBytes > 1));

	memset(dst, 0, dstNumBytes);
	const int dstChars = (int)(dstNumBytes / sizeof(wchar_t));
	const int dstNeeded = (int)MultiByteToWideChar(CP_UTF8, 0, src, -1, 0, 0);
	if ((dstNeeded > 0) && (dstNeeded <= dstChars)) {
		MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, dstChars);
		return true;
	}
    else {
        return false;
    }
}

bool strWideToUtf8(const wchar_t* src, char* dst, size_t dstNumBytes)
{
	ASSERT(src && dst && (dstNumBytes > 1));

	memset(dst, 0, dstNumBytes);
	const int dstChars = (int)dstNumBytes;
    const int dstNeeded = WideCharToMultiByte(CP_UTF8, 0, src, -1, 0, 0, NULL, NULL);
	if ((dstNeeded > 0) && (dstNeeded <= dstChars)) {
		WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, dstChars, NULL, NULL);
		return true;
	}
	else {
		return false;
	}
}

#endif // PLATFORM_WINDOWS
