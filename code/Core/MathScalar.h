#pragma once

#include "MathTypes.h"

#if defined(__SSE2__) || (COMPILER_MSVC && (ARCH_64BIT || _M_IX86_FP >= 2))
    #include <xmmintrin.h>    // __m128
#endif    //

#if defined(__SSE2__) || (COMPILER_MSVC && (ARCH_64BIT || _M_IX86_FP >= 2))
    FORCE_INLINE float mathSqrt(float _a);
    FORCE_INLINE float mathRsqrt(float _a);
#else
    API  float       mathSqrt(float _a);
    API  float       mathRsqrt(float _a);
#endif

API  float mathCopySign(float _x, float _y);
API  float mathFloor(float _f);
API  float mathCos(float _a);
API  float mathACos(float _a);
API  float mathSin(float _a);
API  float mathASin(float _a);
API  float mathATan2(float _y, float _x);
API  float mathExp(float _a);
API  float mathLog(float _a);
FORCE_INLINE constexpr int   mathNearestPow2(int n);
FORCE_INLINE constexpr bool  mathIsPow2(int n);
FORCE_INLINE constexpr float mathToRad(float _deg);
FORCE_INLINE constexpr float mathToDeg(float _rad);
FORCE_INLINE uint32 mathFloatToBits(float _a);
FORCE_INLINE float  mathBitsToFloat(uint32 _a);
FORCE_INLINE uint64 mathDoubleToBits(fl64 _a);
FORCE_INLINE fl64   mathBitsToDouble(uint64 _a);
FORCE_INLINE uint32 mathFlip(uint32 _value);
FORCE_INLINE bool   mathIsNAN(float _f);
FORCE_INLINE bool   mathIsNAN64(fl64 _f);
FORCE_INLINE bool   mathIsFIN(float _f);
FORCE_INLINE bool   mathIsFIN64(fl64 _f);
FORCE_INLINE bool   mathIsINF(float _f);
FORCE_INLINE bool   mathIsINF64(fl64 _f);
FORCE_INLINE float  mathIsRound(float _f);
FORCE_INLINE float  mathCeil(float _f);
FORCE_INLINE float  mathLerp(float _a, float _b, float _t);
FORCE_INLINE float  mathSmoothLerp(float _a, float _b, float _dt, float h);
FORCE_INLINE float  mathSign(float _a);
FORCE_INLINE float  mathAbs(float _a);
FORCE_INLINE float  mathTan(float _a);
FORCE_INLINE float  mathSinH(float _a);
FORCE_INLINE float  mathCosH(float _a);
FORCE_INLINE float  mathTanH(float _a);
FORCE_INLINE float  mathATan(float _a);
FORCE_INLINE float  mathPow(float _a, float _b);
FORCE_INLINE float  mathExp2(float _a);
FORCE_INLINE float  mathLog2(float _a);
FORCE_INLINE float  mathTrunc(float _a);
FORCE_INLINE float  mathFract(float _a);
FORCE_INLINE float  mathMod(float _a, float _b);
FORCE_INLINE bool   mathIsEqual(float _a, float _b, float _epsilon = 0.00001f);
FORCE_INLINE bool   mathIsEqualArray(const float* _a, const float* _b, int _num, float _epsilon = 0.00001f);
FORCE_INLINE float  mathWrap(float _a, float _wrap);
FORCE_INLINE float  mathWrapRange(float x, float fmin, float fmax);
FORCE_INLINE int    mathWrapInt(int x, int imin, int imax);
FORCE_INLINE float  mathStep(float _a, float _edge);
FORCE_INLINE float  mathPulse(float _a, float _start, float _end);
FORCE_INLINE float  mathSaturate(float _n);
FORCE_INLINE float  mathSmoothStep(float _a, float _min, float _max);
FORCE_INLINE float  mathLinearStep(float t, float _min, float _max);
FORCE_INLINE float  mathNormalizeTime(float t, float _max);
FORCE_INLINE float  mathBias(float _time, float _bias);
FORCE_INLINE float  mathGain(float _time, float _gain);
FORCE_INLINE float  mathAngleDiff(float _a, float _b);
FORCE_INLINE float  mathAngleLerp(float _a, float _b, float _t);

////////////////////////////////////////////////////////////////////////////////////////////////////
#if defined(__SSE2__) || (COMPILER_MSVC && ARCH_64BIT)
FORCE_INLINE float mathSqrt(float x)
{
    float r;
    _mm_store_ss(&r, _mm_sqrt_ss( _mm_load_ss(&x)));
    return r;
}

FORCE_INLINE float mathRsqrt(float x)
{
    float r;
    _mm_store_ss(&r, _mm_rsqrt_ss(_mm_load_ss(&x)));
    return r;
}
#endif   // __SSE2__

// https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
FORCE_INLINE constexpr int mathNearestPow2(int n)
{
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n++;
    return n;
}

FORCE_INLINE constexpr bool mathIsPow2(int n)
{
    return (n & (n - 1)) == 0;
}

FORCE_INLINE constexpr float mathToRad(float _deg)
{
    return _deg * kPI / 180.0f;
}

FORCE_INLINE constexpr float mathToDeg(float _rad)
{
    return _rad * 180.0f / kPI;
}

// Packs float to uint32
FORCE_INLINE uint32 mathFloatToBits(float _a)
{
    union {
        float f;
        uint32 ui;
    } u = { _a };
    return u.ui;
}

// Unpacks float from uint32
FORCE_INLINE float mathBitsToFloat(uint32 _a)
{
    union {
        uint32 ui;
        float f;
    } u = { _a };
    return u.f;
}

// Packs fl64 to uint64
FORCE_INLINE uint64 mathDoubleToBits(fl64 _a)
{
    union {
        fl64 f;
        uint64 ui;
    } u = { _a };
    return u.ui;
}

// Unpacks uint64 to fl64
FORCE_INLINE fl64 mathBitsToDouble(uint64 _a)
{
    union {
        uint64 ui;
        fl64 f;
    } u = { _a };
    return u.f;
}

// Returns sortable bit packed float value
// http://archive.fo/2012.12.08-212402/http://stereopsis.com/radix.html
FORCE_INLINE uint32 mathFlip(uint32 _value)
{
    uint32 mask = -((int32)(_value >> 31)) | 0x80000000;
    return _value ^ mask;
}

FORCE_INLINE bool mathIsNAN(float _f)
{
    const uint32 tmp = mathFloatToBits(_f) & INT32_MAX;
    return tmp > UINT32_C(0x7f800000);
}

FORCE_INLINE bool mathIsNAN64(fl64 _f)
{
    const uint64 tmp = mathDoubleToBits(_f) & INT64_MAX;
    return tmp > UINT64_C(0x7ff0000000000000);
}

FORCE_INLINE bool mathIsFIN(float _f)
{
    const uint32 tmp = mathFloatToBits(_f) & INT32_MAX;
    return tmp < UINT32_C(0x7f800000);
}

FORCE_INLINE bool mathIsFIN64(fl64 _f)
{
    const uint64 tmp = mathDoubleToBits(_f) & INT64_MAX;
    return tmp < UINT64_C(0x7ff0000000000000);
}

FORCE_INLINE bool mathIsINF(float _f)
{
    const uint32 tmp = mathFloatToBits(_f) & INT32_MAX;
    return tmp == UINT32_C(0x7f800000);
}

FORCE_INLINE bool mathIsINF64(fl64 _f)
{
    const uint64 tmp = mathDoubleToBits(_f) & INT64_MAX;
    return tmp == UINT64_C(0x7ff0000000000000);
}

FORCE_INLINE float mathIsRound(float _f)
{
    return mathFloor(_f + 0.5f);
}

FORCE_INLINE float mathCeil(float _f)
{
    return -mathFloor(-_f);
}

FORCE_INLINE float mathLerp(float _a, float _b, float _t)
{
    // this version is more precise than: _a + (_b - _a) * _t
    return (1.0f - _t) * _a + _t * _b;
}

// SmoothLerp by Freya: https://x.com/FreyaHolmer/status/1757836988495847568?s=20
// '_h' or half-life can be calculated like this: h = -t/mathLog2(p)
// where 'p' is the normalized distance traveled to the target after 't' seconds
// Useful for lerping moving targets
FORCE_INLINE float mathSmoothLerp(float _a, float _b, float _dt, float _h)
{
    return _b + (_a - _b)*mathExp2(-_dt/_h);
}

FORCE_INLINE float mathSign(float _a)
{
    return _a < 0.0f ? -1.0f : 1.0f;
}

FORCE_INLINE float mathAbs(float _a)
{
    union {
        float f;
        uint32 ui;
    } u = { _a };
    u.ui &= 0x7FFFFFFF;
    return u.f;
}

FORCE_INLINE float mathTan(float _a)
{
    return mathSin(_a) / mathCos(_a);
}

FORCE_INLINE float mathSinH(float _a)
{
    return 0.5f * (mathExp(_a) - mathExp(-_a));
}

FORCE_INLINE float mathCosH(float _a)
{
    return 0.5f * (mathExp(_a) + mathExp(-_a));
}

FORCE_INLINE float mathTanH(float _a)
{
    const float tmp0 = mathExp(2.0f * _a);
    const float tmp1 = tmp0 - 1.0f;
    const float tmp2 = tmp0 + 1.0f;
    const float result = tmp1 / tmp2;

    return result;
}

FORCE_INLINE float mathATan(float _a)
{
    return mathATan2(_a, 1.0f);
}

FORCE_INLINE float mathPow(float _a, float _b)
{
    return mathExp(_b * mathLog(_a));
}

FORCE_INLINE float mathExp2(float _a)
{
    return mathPow(2.0f, _a);
}

FORCE_INLINE float mathLog2(float _a)
{
    return mathLog(_a) * kInvLogNat2;
}

// Returns the nearest integer not greater in magnitude than _a.
FORCE_INLINE float mathTrunc(float _a)
{
    return (float)((int)_a);
}

// Returns the fractional (or decimal) part of _a, which is 0~1
FORCE_INLINE float mathFract(float _a)
{
    return _a - mathTrunc(_a);
}

// Returns the floating-point remainder of the division operation _a/_b.
FORCE_INLINE float mathMod(float _a, float _b)
{
    return _a - _b * mathFloor(_a / _b);
}

// http://realtimecollisiondetection.net/blog/?t=89
PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wshadow")
FORCE_INLINE bool mathIsEqual(float _a, float _b, float _epsilon)
{
    const float lhs = mathAbs(_a - _b);
    float aa = mathAbs(_a);
    float ab = mathAbs(_b);
    float _max = aa > ab ? aa : ab;
    const float rhs = _epsilon * ((1.0f > _max ? 1.0f : _max));
    return lhs <= rhs;
}
PRAGMA_DIAGNOSTIC_POP()

FORCE_INLINE bool mathIsEqualArray(const float* _a, const float* _b, int _num,
                                             float _epsilon)
{
    bool result = mathIsEqual(_a[0], _b[0], _epsilon);
    for (int ii = 1; result && ii < _num; ++ii) {
        result = mathIsEqual(_a[ii], _b[ii], _epsilon);
    }
    return result;
}

FORCE_INLINE float mathWrap(float _a, float _wrap)
{
    const float tmp0 = mathMod(_a, _wrap);
    const float result = tmp0 < 0.0f ? _wrap + tmp0 : tmp0;
    return result;
}

FORCE_INLINE float mathWrapRange(float x, float fmin, float fmax)
{
    return mathMod(x, fmax - fmin) + fmin;
}

FORCE_INLINE int mathWrapInt(int x, int imin, int imax)
{
    int range = imax - imin + 1;
    if (x < imin)
        x += range * ((imin - x) / range + 1);
    return imin + (x - imin) % range;
}

// Returns 0 if _a < _edge, else 1
FORCE_INLINE float mathStep(float _a, float _edge)
{
    return _a < _edge ? 0.0f : 1.0f;
}

FORCE_INLINE float mathPulse(float _a, float _start, float _end)
{
    return mathStep(_a, _start) - mathStep(_a, _end);
}

FORCE_INLINE float mathSaturate(float _n)
{
    if (_n < 0) 
        _n = 0;
    else if (_n > 1.0f)
        _n = 1.0f;
    return _n;
}

// Smooth Inverse Lerp: Hermite interpolation (result = 0..1) when edge0 < x < edge1
FORCE_INLINE float mathSmoothStep(float _a, float _min, float _max)
{
    ASSERT(_min < _max);
    float a = mathSaturate((_a - _min) / (_max - _min));
    return a * a * (3.0f - 2.0f * a);
}

// Inverse Lerp 
// result is 0..1 when in _min.._max range, 0 if less than _min, 1 if more than _max
FORCE_INLINE float mathLinearStep(float t, float _min, float _max)
{
    ASSERT(_min < _max);
    return mathSaturate((t - _min) / (_max - _min));
}

// used for normalizing time values to 0..1
// based on the assumption that time 't' starts from 0.._max or more than that
FORCE_INLINE float mathNormalizeTime(float t, float _max)
{
    ASSERT(_max > 0);
    float nt = t / _max;
    return nt < 1.0f ? nt : 1.0f;
}


// References:
//  - Bias And Gain Are Your Friend
//    http://blog.demofox.org/2012/09/24/mathBias-and-mathGain-are-your-friend/
//  - http://demofox.org/biasgain.html
FORCE_INLINE float mathBias(float _time, float _bias)
{
    return _time / (((1.0f / _bias - 2.0f) * (1.0f - _time)) + 1.0f);
}

FORCE_INLINE float mathGain(float _time, float _gain)
{
    if (_time < 0.5f)
        return mathBias(_time * 2.0f, _gain) * 0.5f;

    return mathBias(_time * 2.0f - 1.0f, 1.0f - _gain) * 0.5f + 0.5f;
}

FORCE_INLINE float mathAngleDiff(float _a, float _b)
{
    const float dist = mathWrap(_b - _a, kPI2);
    return mathWrap(dist * 2.0f, kPI2) - dist;
}

FORCE_INLINE float mathAngleLerp(float _a, float _b, float _t)
{
    return _a + mathAngleDiff(_a, _b) * _t;
}
