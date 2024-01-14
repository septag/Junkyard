#pragma once

#if defined(_WIN32) || defined(_WIN64)

#define WIN32_MEAN_AND_LEAN
#define VC_EXTRALEAN
#define NOMINMAX          // Macros min(a,b) and max(a,b)

#include <winsock2.h>
#include <windows.h>

#undef WIN32_MEAN_AND_LEAN
#undef VC_EXTRALEAN
#undef NOMINMAX

#endif // _WIN32 / _WIN64
