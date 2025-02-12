#pragma once

#include "MathTypes.h"

#if defined(__SSE2__) || (COMPILER_MSVC && (ARCH_64BIT || _M_IX86_FP >= 2))
    #include <xmmintrin.h>    // __m128
#endif

namespace M 
{
    #if defined(__SSE2__) || (COMPILER_MSVC && (ARCH_64BIT || _M_IX86_FP >= 2))
    FORCE_INLINE float Sqrt(float _a);
    FORCE_INLINE float Rsqrt(float _a);
    #else
    API  float Sqrt(float _a);
    API  float Rsqrt(float _a);
    #endif

    API float CopySign(float _x, float _y);
    API float Floor(float _f);
    API float Cos(float _a);
    API float ACos(float _a);
    API float Sin(float _a);
    API float ASin(float _a);
    API float ATan2(float _y, float _x);
    API float Exp(float _a);
    API float Log(float _a);
    FORCE_INLINE constexpr int NearestPow2(int n);
    FORCE_INLINE constexpr bool IsPow2(int n);
    FORCE_INLINE constexpr float ToRad(float _deg);
    FORCE_INLINE constexpr float ToDeg(float _rad);
    FORCE_INLINE uint32 FloatToBits(float _a);
    FORCE_INLINE float BitsToFloat(uint32 _a);
    FORCE_INLINE uint64 DoubleToBits(fl64 _a);
    FORCE_INLINE double BitsToDouble(uint64 _a);
    FORCE_INLINE uint32 Flip(uint32 _value);
    FORCE_INLINE bool IsNAN(float _f);
    FORCE_INLINE bool IsNAN64(double _f);
    FORCE_INLINE bool IsFIN(float _f);
    FORCE_INLINE bool IsFIN64(double _f);
    FORCE_INLINE bool IsINF(float _f);
    FORCE_INLINE bool IsINF64(double _f);
    FORCE_INLINE float Round(float _f);
    FORCE_INLINE float Ceil(float _f);
    FORCE_INLINE float Lerp(float _a, float _b, float _t);
    FORCE_INLINE float SmoothLerp(float _a, float _b, float _dt, float h);
    FORCE_INLINE float Sign(float _a);
    FORCE_INLINE float Abs(float _a);
    FORCE_INLINE int Abs(int _a);
    FORCE_INLINE int64 Abs(int64 _a);
    FORCE_INLINE float Tan(float _a);
    FORCE_INLINE float SinH(float _a);
    FORCE_INLINE float CosH(float _a);
    FORCE_INLINE float TanH(float _a);
    FORCE_INLINE float ATan(float _a);
    FORCE_INLINE float Pow(float _a, float _b);
    FORCE_INLINE float Exp2(float _a);
    FORCE_INLINE float Log2(float _a);
    FORCE_INLINE float Trunc(float _a);
    FORCE_INLINE float Fract(float _a);
    FORCE_INLINE float Mod(float _a, float _b);
    FORCE_INLINE bool IsEqual(float _a, float _b, float _epsilon = 0.00001f);
    FORCE_INLINE bool IsEqualArray(const float* _a, const float* _b, int _num, float _epsilon = 0.00001f);
    FORCE_INLINE float Wrap(float _a, float _wrap);
    FORCE_INLINE float WrapRange(float x, float fmin, float fmax);
    FORCE_INLINE int WrapRange(int x, int imin, int imax);
    FORCE_INLINE float Step(float _a, float _edge);
    FORCE_INLINE float Pulse(float _a, float _start, float _end);
    FORCE_INLINE float Saturate(float _n);
    FORCE_INLINE float SmoothStep(float _a, float _min, float _max);
    FORCE_INLINE float LinearStep(float t, float _min, float _max);
    FORCE_INLINE float NormalizeTime(float t, float _max);
    FORCE_INLINE float Bias(float _time, float _bias);
    FORCE_INLINE float Gain(float _time, float _gain);
    FORCE_INLINE float AngleDiff(float _a, float _b);
    FORCE_INLINE float AngleLerp(float _a, float _b, float _t);
} // M (Math)



//    ██╗███╗   ██╗██╗     ██╗███╗   ██╗███████╗███████╗
//    ██║████╗  ██║██║     ██║████╗  ██║██╔════╝██╔════╝
//    ██║██╔██╗ ██║██║     ██║██╔██╗ ██║█████╗  ███████╗
//    ██║██║╚██╗██║██║     ██║██║╚██╗██║██╔══╝  ╚════██║
//    ██║██║ ╚████║███████╗██║██║ ╚████║███████╗███████║
//    ╚═╝╚═╝  ╚═══╝╚══════╝╚═╝╚═╝  ╚═══╝╚══════╝╚══════╝
                                                      
#if defined(__SSE2__) || (COMPILER_MSVC && ARCH_64BIT)
FORCE_INLINE float M::Sqrt(float x)
{
    float r;
    _mm_store_ss(&r, _mm_sqrt_ss( _mm_load_ss(&x)));
    return r;
}

FORCE_INLINE float M::Rsqrt(float x)
{
    float r;
    _mm_store_ss(&r, _mm_rsqrt_ss(_mm_load_ss(&x)));
    return r;
}
#endif   // __SSE2__

// https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
FORCE_INLINE constexpr int M::NearestPow2(int n)
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

FORCE_INLINE constexpr bool M::IsPow2(int n)
{
    return (n & (n - 1)) == 0;
}

FORCE_INLINE constexpr float M::ToRad(float _deg)
{
    return _deg * M_PI / 180.0f;
}

FORCE_INLINE constexpr float M::ToDeg(float _rad)
{
    return _rad * 180.0f / M_PI;
}

// Packs float to uint32
FORCE_INLINE uint32 M::FloatToBits(float _a)
{
    union {
        float f;
        uint32 ui;
    } u = { _a };
    return u.ui;
}

// Unpacks float from uint32
FORCE_INLINE float M::BitsToFloat(uint32 _a)
{
    union {
        uint32 ui;
        float f;
    } u = { _a };
    return u.f;
}

// Packs fl64 to uint64
FORCE_INLINE uint64 M::DoubleToBits(fl64 _a)
{
    union {
        fl64 f;
        uint64 ui;
    } u = { _a };
    return u.ui;
}

// Unpacks uint64 to fl64
FORCE_INLINE fl64 M::BitsToDouble(uint64 _a)
{
    union {
        uint64 ui;
        fl64 f;
    } u = { _a };
    return u.f;
}

// Returns sortable bit packed float value
// http://archive.fo/2012.12.08-212402/http://stereopsis.com/radix.html
FORCE_INLINE uint32 M::Flip(uint32 _value)
{
    uint32 mask = -int32(_value >> 31) | 0x80000000;
    return _value ^ mask;
}

FORCE_INLINE bool M::IsNAN(float _f)
{
    const uint32 tmp = M::FloatToBits(_f) & INT32_MAX;
    return tmp > UINT32_C(0x7f800000);
}

FORCE_INLINE bool M::IsNAN64(fl64 _f)
{
    const uint64 tmp = M::DoubleToBits(_f) & INT64_MAX;
    return tmp > UINT64_C(0x7ff0000000000000);
}

FORCE_INLINE bool M::IsFIN(float _f)
{
    const uint32 tmp = M::FloatToBits(_f) & INT32_MAX;
    return tmp < UINT32_C(0x7f800000);
}

FORCE_INLINE bool M::IsFIN64(fl64 _f)
{
    const uint64 tmp = M::DoubleToBits(_f) & INT64_MAX;
    return tmp < UINT64_C(0x7ff0000000000000);
}

FORCE_INLINE bool M::IsINF(float _f)
{
    const uint32 tmp = M::FloatToBits(_f) & INT32_MAX;
    return tmp == UINT32_C(0x7f800000);
}

FORCE_INLINE bool M::IsINF64(fl64 _f)
{
    const uint64 tmp = M::DoubleToBits(_f) & INT64_MAX;
    return tmp == UINT64_C(0x7ff0000000000000);
}

FORCE_INLINE float M::Round(float _f)
{
    return M::Floor(_f + 0.5f);
}

FORCE_INLINE float M::Ceil(float _f)
{
    return -M::Floor(-_f);
}

FORCE_INLINE float M::Lerp(float _a, float _b, float _t)
{
    // this version is more precise than: _a + (_b - _a) * _t
    return (1.0f - _t) * _a + _t * _b;
}

// SmoothLerp by Freya: https://x.com/FreyaHolmer/status/1757836988495847568?s=20
// '_h' or half-life can be calculated like this: h = -t/M::Log2(p)
// where 'p' is the normalized distance traveled to the target after 't' seconds
// Useful for lerping moving targets
FORCE_INLINE float M::SmoothLerp(float _a, float _b, float _dt, float _h)
{
    return _b + (_a - _b)*M::Exp2(-_dt/_h);
}

FORCE_INLINE float M::Sign(float _a)
{
    return _a < 0.0f ? -1.0f : 1.0f;
}

FORCE_INLINE float M::Abs(float _a)
{
    union {
        float f;
        uint32 ui;
    } u = { _a };
    u.ui &= 0x7FFFFFFF;
    return u.f;
}

FORCE_INLINE int M::Abs(int _a)
{
    int mask = _a >> 31;
    return (mask^_a) - mask;
}

FORCE_INLINE int64 M::Abs(int64 _a)
{
    int64 mask = _a >> 63;
    return (mask^_a) - mask;
}

FORCE_INLINE float M::Tan(float _a)
{
    return M::Sin(_a) / M::Cos(_a);
}

FORCE_INLINE float M::SinH(float _a)
{
    return 0.5f * (M::Exp(_a) - M::Exp(-_a));
}

FORCE_INLINE float M::CosH(float _a)
{
    return 0.5f * (M::Exp(_a) + M::Exp(-_a));
}

FORCE_INLINE float M::TanH(float _a)
{
    const float tmp0 = M::Exp(2.0f * _a);
    const float tmp1 = tmp0 - 1.0f;
    const float tmp2 = tmp0 + 1.0f;
    const float result = tmp1 / tmp2;

    return result;
}

FORCE_INLINE float M::ATan(float _a)
{
    return M::ATan2(_a, 1.0f);
}

FORCE_INLINE float M::Pow(float _a, float _b)
{
    return M::Exp(_b * M::Log(_a));
}

FORCE_INLINE float M::Exp2(float _a)
{
    return M::Pow(2.0f, _a);
}

FORCE_INLINE float M::Log2(float _a)
{
    return M::Log(_a) * M_INVLOGNAT10;
}

// Returns the nearest integer not greater in magnitude than _a.
FORCE_INLINE float M::Trunc(float _a)
{
    return (float)((int)_a);
}

// Returns the fractional (or decimal) part of _a, which is 0~1
FORCE_INLINE float M::Fract(float _a)
{
    return _a - M::Trunc(_a);
}

// Returns the floating-point remainder of the division operation _a/_b.
FORCE_INLINE float M::Mod(float _a, float _b)
{
    return _a - _b * M::Floor(_a / _b);
}

// http://realtimecollisiondetection.net/blog/?t=89
PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wshadow")
FORCE_INLINE bool M::IsEqual(float _a, float _b, float _epsilon)
{
    const float lhs = M::Abs(_a - _b);
    float aa = M::Abs(_a);
    float ab = M::Abs(_b);
    float _max = aa > ab ? aa : ab;
    const float rhs = _epsilon * ((1.0f > _max ? 1.0f : _max));
    return lhs <= rhs;
}
PRAGMA_DIAGNOSTIC_POP()

FORCE_INLINE bool M::IsEqualArray(const float* _a, const float* _b, int _num,
                                             float _epsilon)
{
    bool result = M::IsEqual(_a[0], _b[0], _epsilon);
    for (int ii = 1; result && ii < _num; ++ii) {
        result = M::IsEqual(_a[ii], _b[ii], _epsilon);
    }
    return result;
}

FORCE_INLINE float M::Wrap(float _a, float _wrap)
{
    const float tmp0 = M::Mod(_a, _wrap);
    const float result = tmp0 < 0.0f ? _wrap + tmp0 : tmp0;
    return result;
}

FORCE_INLINE float M::WrapRange(float x, float fmin, float fmax)
{
    return M::Mod(x, fmax - fmin) + fmin;
}

FORCE_INLINE int M::WrapRange(int x, int imin, int imax)
{
    int range = imax - imin + 1;
    if (x < imin)
        x += range * ((imin - x) / range + 1);
    return imin + (x - imin) % range;
}

// Returns 0 if _a < _edge, else 1
FORCE_INLINE float M::Step(float _a, float _edge)
{
    return _a < _edge ? 0.0f : 1.0f;
}

FORCE_INLINE float M::Pulse(float _a, float _start, float _end)
{
    return M::Step(_a, _start) - M::Step(_a, _end);
}

FORCE_INLINE float M::Saturate(float _n)
{
    if (_n < 0) 
        _n = 0;
    else if (_n > 1.0f)
        _n = 1.0f;
    return _n;
}

// Smooth Inverse Lerp: Hermite interpolation (result = 0..1) when edge0 < x < edge1
FORCE_INLINE float M::SmoothStep(float _a, float _min, float _max)
{
    ASSERT(_min < _max);
    float a = M::Saturate((_a - _min) / (_max - _min));
    return a * a * (3.0f - 2.0f * a);
}

// Inverse Lerp 
// result is 0..1 when in _min.._max range, 0 if less than _min, 1 if more than _max
FORCE_INLINE float M::LinearStep(float t, float _min, float _max)
{
    ASSERT(_min < _max);
    return M::Saturate((t - _min) / (_max - _min));
}

// used for normalizing time values to 0..1
// based on the assumption that time 't' starts from 0.._max or more than that
FORCE_INLINE float M::NormalizeTime(float t, float _max)
{
    ASSERT(_max > 0);
    float nt = t / _max;
    return nt < 1.0f ? nt : 1.0f;
}


// References:
//  - Bias And Gain Are Your Friend
//    http://blog.demofox.org/2012/09/24/M::Bias-and-M::Gain-are-your-friend/
//  - http://demofox.org/biasgain.html
FORCE_INLINE float M::Bias(float _time, float _bias)
{
    return _time / (((1.0f / _bias - 2.0f) * (1.0f - _time)) + 1.0f);
}

FORCE_INLINE float M::Gain(float _time, float _gain)
{
    if (_time < 0.5f)
        return M::Bias(_time * 2.0f, _gain) * 0.5f;

    return M::Bias(_time * 2.0f - 1.0f, 1.0f - _gain) * 0.5f + 0.5f;
}

FORCE_INLINE float M::AngleDiff(float _a, float _b)
{
    const float dist = M::Wrap(_b - _a, M_PI2);
    return M::Wrap(dist * 2.0f, M_PI2) - dist;
}

FORCE_INLINE float M::AngleLerp(float _a, float _b, float _t)
{
    return _a + M::AngleDiff(_a, _b) * _t;
}
