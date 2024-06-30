#pragma once

#include "Base.h"

PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4201)    // nonstandard extension used : nameless struct/union
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4204)    // nonstandard extension used: non-constant aggregate initializer

union Float2 
{
    struct 
    {
        float x;
        float y;
    };

    float f[2];

    Float2() = default;
    explicit constexpr Float2(float _x, float _y) : x(_x), y(_y) {}
    explicit constexpr Float2(float _xx) : x(_xx), y(_xx) {}
    explicit constexpr Float2(const float* _f) : x(_f[0]), y(_f[1]) {}
};

union Float3 
{
    struct 
    {
        float x;
        float y;
        float z;
    };

    float f[3];

    Float3() = default;
    explicit constexpr Float3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
    explicit constexpr Float3(float _xxx) : x(_xxx), y(_xxx), z(_xxx) {}
    explicit constexpr Float3(const float* _f) : x(_f[0]), y(_f[1]), z(_f[2]) {}
    explicit constexpr Float3(Float2 v, float _z = 0) : x(v.x), y(v.y), z(_z) {}
};

union Float4 
{
    struct 
    {
        float x;
        float y;
        float z;
        float w;
    };

    float f[4];

    Float4() = default;
    explicit constexpr Float4(float _x, float _y, float _z, float _w = 1.0f) : x(_x), y(_y), z(_z), w(_w) {}
    explicit constexpr Float4(float _xxxx) : x(_xxxx), y(_xxxx), z(_xxxx), w(_xxxx) {}
    explicit constexpr Float4(const float* _f) : x(_f[0]), y(_f[1]), z(_f[2]), w(_f[3]) {}
    explicit constexpr Float4(Float3 v, float _w = 1.0f) : x(v.x), y(v.y), z(v.z), w(_w) {}
    explicit constexpr Float4(Float2 v, float _z = 0, float _w = 1.0f) : x(v.x), y(v.y), z(_z), w(_w) {}
};

union Color 
{
    struct 
    {
        uint8 r;
        uint8 g;
        uint8 b;
        uint8 a;
    };

    unsigned int n;

    Color() = default;

    explicit constexpr Color(uint8 _r, uint8 _g, uint8 _b, uint8 _a = 255)
        : r(_r), g(_g), b(_b), a(_a)
    {
    }

    explicit constexpr Color(float _r, float _g, float _b, float _a) :
        r((uint8)(_r * 255.0f)),
        g((uint8)(_g * 255.0f)),
        b((uint8)(_b * 255.0f)),
        a((uint8)(_a * 255.0f))
    {
    }

    explicit constexpr Color(const float* f) : Color(f[0], f[1], f[2], f[3]) {}
    constexpr Color(uint32 _n) : n(_n) {}

    Color& operator=(uint32 _n) 
    {
        n = _n;
        return *this;
    }
};

union Int2 
{
    struct 
    {
        int x;
        int y;
    };

    int n[2];

    Int2() = default;
    explicit constexpr Int2(int _x, int _y) : x(_x), y(_y) {}
    explicit constexpr Int2(const int* _i) : x(_i[0]), y(_i[1]) {}
    explicit constexpr Int2(int _xx) : x(_xx), y(_xx) {}
};

union Quat 
{
    struct 
    {
        float x;
        float y;
        float z;
        float w;
    };

    float f[4];

    Quat() = default;
    explicit constexpr Quat(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
    explicit constexpr Quat(const float* _f) : x(_f[0]), y(_f[1]), z(_f[2]), w(_f[3]) {}
};

union Mat3 
{
    struct 
    {
        float m11, m21, m31;
        float m12, m22, m32;
        float m13, m23, m33;
    };

    struct 
    {
        float fc1[3];
        float fc2[3];
        float fc3[3];
    };

    float f[9];

    Mat3() = default;
    explicit constexpr Mat3(float _m11, float _m12, float _m13, 
                            float _m21, float _m22, float _m23, 
                            float _m31, float _m32, float _m33) :
      m11(_m11),     m21(_m21),     m31(_m31),
      m12(_m12),     m22(_m22),     m32(_m32),
      m13(_m13),     m23(_m23),     m33(_m33) 
    {
    }

    explicit constexpr Mat3(const float* _col1, const float* _col2, const float* _col3) :
      m11(_col1[0]),     m21(_col1[1]),     m31(_col1[2]),
      m12(_col2[0]),     m22(_col2[1]),     m32(_col2[2]),
      m13(_col3[0]),     m23(_col3[1]),     m33(_col3[2])
    {
    }

    explicit constexpr Mat3(Float3 _col1, Float3 _col2, Float3 _col3) :
        Mat3(_col1.f, _col2.f, _col3.f)
    {
    }    
};

union Mat4 
{
    struct 
    {
        float m11, m21, m31, m41;
        float m12, m22, m32, m42;
        float m13, m23, m33, m43;
        float m14, m24, m34, m44;
    };

    struct 
    {
        float fc1[4];
        float fc2[4];
        float fc3[4];
        float fc4[4];
    };

    float f[16];

    Mat4() = default;
    explicit constexpr Mat4(float _m11, float _m12, float _m13, float _m14, 
                            float _m21, float _m22, float _m23, float _m24, 
                            float _m31, float _m32, float _m33, float _m34, 
                            float _m41, float _m42, float _m43, float _m44)
    :   m11(_m11),     m21(_m21),     m31(_m31),     m41(_m41),
        m12(_m12),     m22(_m22),     m32(_m32),     m42(_m42),
        m13(_m13),     m23(_m23),     m33(_m33),     m43(_m43),
        m14(_m14),     m24(_m24),     m34(_m34),     m44(_m44)
    {    
    }
    explicit constexpr Mat4(const float* _col1, const float* _col2, const float* _col3, const float* _col4)
    :   m11(_col1[0]),     m21(_col1[1]),     m31(_col1[2]),     m41(_col1[3]),
        m12(_col2[0]),     m22(_col2[1]),     m32(_col2[2]),     m42(_col2[3]),
        m13(_col3[0]),     m23(_col3[1]),     m33(_col3[2]),     m43(_col3[3]),
        m14(_col4[0]),     m24(_col4[1]),     m34(_col4[2]),     m44(_col4[3])
    {
    }
    explicit constexpr Mat4(Float4 _col1, Float4 _col2, Float4 _col3, Float4 _col4) : 
        Mat4(_col1.f, _col2.f, _col3.f, _col4.f)
    {
    }
};

union Rect 
{
    struct 
    {
        float xmin, ymin;
        float xmax, ymax;
    };

    struct 
    {
        float vmin[2];
        float vmax[2];
    };

    float f[4];

    Rect() = default;
    explicit constexpr Rect(float _xmin, float _ymin, float _xmax, float _ymax) 
    : xmin(_xmin), ymin(_ymin), xmax(_xmax), ymax(_ymax)
    {
    }
    explicit constexpr Rect(const float* _vmin, const float* _vmax) 
    : vmin { _vmin[0], _vmin[1] },
      vmax { _vmax[0], _vmax[1] }
    {
    }
    explicit constexpr Rect(Float2 _vmin, Float2 _vmax) :
        Rect(_vmin.f, _vmax.f)
    {
    }
};

union Recti 
{
    struct 
    {
        int xmin, ymin;
        int xmax, ymax;
    };

    struct 
    {
        int vmin[3];
        int vmax[3];
    };

    int n[4];

    Recti() = default;
    explicit constexpr Recti(int _xmin, int _ymin, int _xmax, int _ymax) 
        : xmin(_xmin),   ymin(_ymin),   xmax(_xmax),   ymax(_ymax)
    {
    }
    explicit constexpr Recti(const int* _vmin, const int* _vmax) 
    : vmin { _vmin[0], _vmin[1] },
      vmax { _vmax[0], _vmax[1] }
    {
    }
    explicit constexpr Recti(Int2 _vmin, Int2 _vmax) :
        Recti(_vmin.n, _vmax.n)
    { 
    }
};

union AABB 
{
    struct 
    {
        float xmin, ymin, zmin;
        float xmax, ymax, zmax;
    };

    struct 
    {
        float vmin[3];
        float vmax[3];
    };

    float f[6];

    AABB() = default;
    explicit constexpr AABB(float _xmin, float _ymin, float _zmin, float _xmax, float _ymax, float _zmax) 
    :   xmin(_xmin), ymin(_ymin), zmin(_zmin),
        xmax(_xmax), ymax(_ymax), zmax(_zmax)
    { 
    }
    explicit constexpr AABB(const float* _vmin, const float* _vmax) 
    : vmin { _vmin[0],    _vmin[1],     _vmin[2] },
      vmax { _vmax[0],    _vmax[1],     _vmax[2] }
    {   
    }
    explicit constexpr AABB(Float3 _vmin, Float3 _vmax) :
        AABB(_vmin.f, _vmax.f)
    {
    }
};

// 3d plane: a*nx + b*ny + c*nz + d = 0
union Plane 
{
    Float4 p;
    struct {
        float normal[3];
        float dist;
    };
    float f[4];

    Plane() = default;
    explicit constexpr Plane(float _nx, float _ny, float _nz, float _d) : p(_nx, _ny, _nz, _d) {}
    explicit constexpr Plane(Float3 _normal, float _d) :
        normal { _normal.x,     _normal.y,  _normal.z}, dist(_d)
    {   
    }
};

// Simplified 3D transform. by position and rotation
struct Transform3D 
{
    Float3 pos;
    Mat3 rot;

    Transform3D() = default;
    explicit constexpr Transform3D(Float3 _pos, const Mat3& _rot) : pos(_pos), rot(_rot) {}
};

// Box is a 3d primitive (cube), that extents in each X,Y,Z direction and has arbitary transform
// This is different from AABB (AABB) where it is axis-aligned and defined by min/max points
struct Box 
{
    Transform3D tx;   // transform (pos = origin of the box, rot = rotation of the box)
    Float3 e;         // half-extent from the origin (0.5*width, 0.5*height, 0.5f*depth)

    Box() = default;
    explicit constexpr Box(const Transform3D& _tx, Float3 _e) : tx(_tx), e(_e) {}
};


//
//     ██████╗ ██████╗ ███╗   ██╗███████╗████████╗ █████╗ ███╗   ██╗████████╗███████╗
//    ██╔════╝██╔═══██╗████╗  ██║██╔════╝╚══██╔══╝██╔══██╗████╗  ██║╚══██╔══╝██╔════╝
//    ██║     ██║   ██║██╔██╗ ██║███████╗   ██║   ███████║██╔██╗ ██║   ██║   ███████╗
//    ██║     ██║   ██║██║╚██╗██║╚════██║   ██║   ██╔══██║██║╚██╗██║   ██║   ╚════██║
//    ╚██████╗╚██████╔╝██║ ╚████║███████║   ██║   ██║  ██║██║ ╚████║   ██║   ███████║
//     ╚═════╝ ╚═════╝ ╚═╝  ╚═══╝╚══════╝   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═══╝   ╚═╝   ╚══════╝

inline constexpr float M_PI           = 3.1415926535897932384626433832795f;
inline constexpr float M_PI2          = 6.2831853071795864769252867665590f;
inline constexpr float M_INVPI        = (1.0f / 3.1415926535897932384626433832795f);
inline constexpr float M_HALFPI       = 1.5707963267948966192313216916398f;
inline constexpr float M_QUARTERPI    = 0.7853981633974483096156608458199f;
inline constexpr float M_SQRT2        = 1.4142135623730950488016887242097f;
inline constexpr float M_LOGNAT10     = 2.3025850929940456840179914546844f;
inline constexpr float M_INVLOGNAT10   = 1.4426950408889634073599246810019f;
inline constexpr float M_LOGNAT2H     = 0.6931471805599453094172321214582f;
inline constexpr float M_LOGNAT2L     = 1.90821492927058770002e-10f;
inline constexpr float M_E            = 2.7182818284590452353602874713527f;
inline constexpr float M_NEARZERO     = (1.0f / (float)(1 << 28));
inline constexpr float M_FLOAT32_MIN  = (1.175494e-38f);
inline constexpr float M_FLOAT32_MAX  = (3.402823e+38f);

inline constexpr Float2 FLOAT2_ZERO  { 0.0f, 0.0f };
inline constexpr Float2 FLOAT2_UNITX { 1.0f, 0.0f };
inline constexpr Float2 FLOAT2_UNITY { 0.0f, 1.0f };

inline constexpr Float3 FLOAT3_ZERO  { 0.0f, 0.0f, 0.0f };
inline constexpr Float3 FLOAT3_UNIX { 1.0f, 0.0f, 0.0f };
inline constexpr Float3 FLOAT3_UNITY { 0.0f, 1.0f, 0.0f };
inline constexpr Float3 FLOAT3_UNITZ { 0.0f, 0.0f, 1.0f };

inline constexpr Float4 FLOAT4_ZERO  { 0.0f, 0.0f, 0.0f, 1.0f };
inline constexpr Float4 FLOAT4_UNITX { 1.0f, 0.0f, 0.0f, 1.0f };
inline constexpr Float4 FLOAT4_UNITY { 0.0f, 1.0f, 0.0f, 1.0f };
inline constexpr Float4 FLOAT4_UNITZ { 0.0f, 0.0f, 1.0f, 1.0f };

inline constexpr Int2 INT2_ZERO {0, 0};
inline constexpr Int2 INT2_ONE {1, 1};

inline constexpr Mat3 MAT3_IDENT {
    1.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 1.0f
};

inline constexpr Mat4 MAT4_IDENT {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f 
};

inline constexpr Quat QUAT_INDENT {0, 0, 0, 1.0f};

inline constexpr Transform3D TRANSFORM3D_IDENT { FLOAT3_ZERO, MAT3_IDENT };

inline constexpr Color COLOR_WHITE  { uint8(255), uint8(255), uint8(255), uint8(255) };
inline constexpr Color COLOR_BLACK  { uint8(0), uint8(0), uint8(0), uint8(255) };
inline constexpr Color COLOR_RED    { uint8(255), uint8(0), uint8(0), uint8(255) };
inline constexpr Color COLOR_YELLOW { uint8(255), uint8(255), uint8(0), uint8(255) };
inline constexpr Color COLOR_GREEN  { uint8(0), uint8(255), uint8(0), uint8(255) };
inline constexpr Color COLOR_BLUE   { uint8(0), uint8(0), uint8(255), uint8(255) };
inline constexpr Color COLOR_PURPLE { uint8(255), uint8(0), uint8(255), uint8(255) };

inline constexpr Rect RECT_EMPTY { M_FLOAT32_MAX, M_FLOAT32_MAX, -M_FLOAT32_MAX, -M_FLOAT32_MAX };
inline constexpr Recti RECTI_EMPTY { INT32_MAX, INT32_MAX, -INT32_MIN, -INT32_MIN };
inline constexpr AABB AABB_EMPTY { M_FLOAT32_MAX, M_FLOAT32_MAX, M_FLOAT32_MAX, -M_FLOAT32_MAX, -M_FLOAT32_MAX, -M_FLOAT32_MAX };

PRAGMA_DIAGNOSTIC_POP()    // ignore msvc warnings
