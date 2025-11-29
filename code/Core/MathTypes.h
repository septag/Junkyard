#pragma once

#include "Base.h"

PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4201)    // nonstandard extension used : nameless struct/union
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4204)    // nonstandard extension used: non-constant aggregate initializer

struct Float2 
{
    union {
        struct 
        {
            float x;
            float y;
        };

        float f[2];
    };

    Float2() = default;
    explicit constexpr Float2(float _x, float _y) : x(_x), y(_y) {}
    explicit constexpr Float2(float _xx) : x(_xx), y(_xx) {}
    explicit constexpr Float2(const float* _f) : x(_f[0]), y(_f[1]) {}


    static float  Dot(Float2 _a, Float2 _b);
    static Float2 Norm(Float2 _a);
    static float  Len(Float2 _a);
    static Float2 NormLen(Float2 _a, float* outlen);
    static Float2 Min(Float2 _a, Float2 _b);
    static Float2 Max(Float2 _a, Float2 _b);
    static Float2 Lerp(Float2 _a, Float2 _b, float _t);
    static Float2 Abs(Float2 _a);
    static Float2 Neg(Float2 _a);
    static Float2 Add(Float2 _a, Float2 _b);
    static Float2 Add(Float2 _a, float _b);
    static Float2 Sub(Float2 _a, Float2 _b);
    static Float2 Sub(Float2 _a, float _b);
    static Float2 Mul(Float2 _a, Float2 _b);
    static Float2 Mul(Float2 _a, float _b);
    static Float2 CalcLinearFit2D(const Float2* _points, int _num);
};

struct Float3 
{
    union {
        struct 
        {
            float x;
            float y;
            float z;
        };

        float f[3];
    };

    Float3() = default;
    explicit constexpr Float3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
    explicit constexpr Float3(float _xxx) : x(_xxx), y(_xxx), z(_xxx) {}
    explicit constexpr Float3(const float* _f) : x(_f[0]), y(_f[1]), z(_f[2]) {}
    explicit constexpr Float3(Float2 v, float _z = 0) : x(v.x), y(v.y), z(_z) {}


    static Float3 Abs(Float3 _a);
    static Float3 Neg(Float3 _a);
    static Float3 Add(Float3 _a, Float3 _b);
    static Float3 Add(Float3 _a, float _b);
    static Float3 Sub(Float3 _a, Float3 _b);
    static Float3 Sub(Float3 _a, float _b);
    static Float3 Mul(Float3 _a, Float3 _b);
    static Float3 Mul(Float3 _a, float _b);
    static float  Dot(Float3 _a, Float3 _b);
    static Float3 Cross(Float3 _a, Float3 _b);
    static Float3 Lerp(Float3 _a, Float3 _b, float _t);
    static Float3 SmoothLerp(Float3 _a, Float3 _b, float _dt, float _h);
    static float  Len(Float3 _a);
    static Float3 Norm(Float3 _a);
    static Float3 NormLen(Float3 _a, float* _outlen);
    static Float3 Min(Float3 _a, Float3 _b);
    static Float3 Max(Float3 _a, Float3 _b);
    static Float3 Rcp(Float3 _a);
    static void   Tangent(Float3* _t, Float3* _b, Float3 _n);
    static void   TangentAngle(Float3* _t, Float3* _b, Float3 _n, float _angle);
    static Float3 FromLatLong(float _u, float _v);
    static Float2 ToLatLong(Float3 _dir);
    static Float3 CalcLinearFit3D(const Float3* _points, int _num);
};

struct Float4 
{
    union {
        struct 
        {
            float x;
            float y;
            float z;
            float w;
        };

        float f[4];
    };

    Float4() = default;
    explicit constexpr Float4(float _x, float _y, float _z, float _w = 1.0f) : x(_x), y(_y), z(_z), w(_w) {}
    explicit constexpr Float4(float _xxxx) : x(_xxxx), y(_xxxx), z(_xxxx), w(_xxxx) {}
    explicit constexpr Float4(const float* _f) : x(_f[0]), y(_f[1]), z(_f[2]), w(_f[3]) {}
    explicit constexpr Float4(Float3 v, float _w = 1.0f) : x(v.x), y(v.y), z(v.z), w(_w) {}
    explicit constexpr Float4(float v[3], float _w = 1.0f) : x(v[0]), y(v[1]), z(v[2]), w(_w) {}
    explicit constexpr Float4(Float2 v, float _z = 0, float _w = 1.0f) : x(v.x), y(v.y), z(_z), w(_w) {}

    static Float4 Mul(Float4 _a, Float4 _b);
    static Float4 Mul(Float4 _a, float _b);
    static Float4 Add(Float4 _a, Float4 _b);
    static Float4 Sub(Float4 _a, Float4 _b);
};

struct Color4u 
{
    union {
        struct 
        {
            uint8 r;
            uint8 g;
            uint8 b;
            uint8 a;
        };

        unsigned int n;
    };

    Color4u() = default;

    explicit constexpr Color4u(uint8 _r, uint8 _g, uint8 _b, uint8 _a = 255)
        : r(_r), g(_g), b(_b), a(_a)
    {
    }

    explicit Color4u(const float* f) { *this = Color4u::FromFloat4(f[0], f[1], f[2], f[3]); }
    constexpr Color4u(uint32 _n) : n(_n) {}

    Color4u& operator=(uint32 _n) 
    {
        n = _n;
        return *this;
    }

    static float ValueToLinear(float _a);
    static float ValueToGamma(float _a);
    static Color4u FromFloat4(float _r, float _g, float _b, float _a = 1.0f);
    static Float4 ToFloat4(Color4u c);
    static Float4 ToFloat4(uint8 _r, uint8 _g, uint8 _b, uint8 _a = 255);
    static Color4u Blend(Color4u _a, Color4u _b, float _t);
    static Float4 ToFloat4SRGB(Float4 cf);
    static Float4 ToFloat4Linear(Float4 c);
    static Float3 RGBtoHSV(Float3 rgb);
    static Float3 HSVtoRGB(Float3 hsv);
};

struct Int2 
{
    union {
        struct 
        {
            int x;
            int y;
        };

        int n[2];
    };

    Int2() = default;
    explicit constexpr Int2(int _x, int _y) : x(_x), y(_y) {}
    explicit constexpr Int2(const int* _i) : x(_i[0]), y(_i[1]) {}
    explicit constexpr Int2(int _xx) : x(_xx), y(_xx) {}

    static Int2 Add(Int2 _a, Int2 _b);
    static Int2 Sub(Int2 _a, Int2 _b);
    static Int2 Min(Int2 _a, Int2 _b);
    static Int2 Max(Int2 _a, Int2 _b);
};

struct Quat 
{
    union {
        struct 
        {
            float x;
            float y;
            float z;
            float w;
        };

        float f[4];
    };

    Quat() = default;
    explicit constexpr Quat(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
    explicit constexpr Quat(const float* _f) : x(_f[0]), y(_f[1]), z(_f[2]), w(_f[3]) {}

    static Float3 MulXYZ(Quat _qa, Quat _qb);
    static Quat   Mul(Quat p, Quat q);
    static Quat   Inverse(Quat _q);
    static float  Dot(Quat _a, Quat _b);
    static float  Angle(Quat _a, Quat _b);
    static Quat   Norm(Quat _q);
    static Quat   RotateAxis(Float3 _axis, float _angle);
    static Quat   RotateX(float _ax);
    static Quat   RotateY(float _ay);
    static Quat   RotateZ(float _az);
    static Quat   Lerp(Quat _a, Quat _b, float t);
    static Quat   Slerp(Quat _a, Quat _b, float t);
    static Float3 ToEuler(Quat _q);
    static Quat   FromEuler(Float3 _float3);
    static Float3 TransformFloat3(Float3 v, Quat q);
};

struct Mat3 
{
    union {
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
    };


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

    Float3 Row1() const;
    Float3 Row2() const;
    Float3 Row3() const;
    
    static Mat3   Transpose(const Mat3& _a);
    static Float3 MulFloat3(const Mat3& _mat, Float3 _vec);
    static Mat3   MulInverse(const Mat3& _a, const Mat3& _b);
    static Float3 MulFloat3Inverse(const Mat3& mat, Float3 v);
    static Float2 MulFloat2(const Mat3& _mat, Float2 _vec);
    static Mat3   Translate(float x, float y);
    static Mat3   TranslateFloat2(Float2 p);
    static Mat3   Rotate(float theta);
    static Mat3   Scale(float sx, float sy);
    static Mat3   ScaleRotateTranslate(float sx, float sy, float angle, float tx, float ty);
    static Mat3   Inverse(const Mat3& _a);
    static Mat3   Mul(const Mat3& _a, const Mat3& _b);
    static Mat3   Abs(const Mat3& m);
    static Mat3   FromQuat(Quat q);
};

struct Mat4 
{
    union {
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
    };

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

    Float4 Row1() const;
    Float4 Row2() const;
    Float4 Row3() const;
    Float4 Row4() const;

    static Mat4   Translate(float _tx, float _ty, float _tz);
    static Mat4   Scale(float _sx, float _sy, float _sz);
    static Mat4   Scale(float _scale);
    static Mat4   RotateX(float _ax);
    static Mat4   RotateY(float _ay);
    static Mat4   RotateZ(float _az);
    static Mat4   RotateXY(float _ax, float _ay);
    static Mat4   RotateXYZ(float _ax, float _ay, float _az);
    static Mat4   RotateZYX(float _ax, float _ay, float _az);
    static Mat4   ToQuatTranslate(Quat _quat, Float3 _translation);
    static Mat4   ToQuatTranslateHMD(Quat _quat, Float3 _translation);
    static Float3 MulFloat3(const Mat4& _mat, Float3 _vec);
    static Float3 MulFloat3_xyz0(const Mat4& _mat, Float3 _vec);
    static Float3 MulFloat3H(const Mat4& _mat, Float3 _vec);
    static Float4 MulFloat4(const Mat4& _mat, Float4 _vec);
    static Mat4   Transpose(const Mat4& _a);
    static void   ProjFlipHandedness(Mat4* _dst, const Mat4& _src);
    static void   ViewFlipHandedness(Mat4* _dst, const Mat4& _src);
    static Mat4   FromNormal(Float3 _normal, float _scale, Float3 _pos);
    static Mat4   FromNormalAngle(Float3 _normal, float _scale, Float3 _pos, float _angle);
    static Mat4   ViewLookAt(Float3 eye, Float3 target, Float3 up);
    static Mat4   ViewLookAtLH(Float3 eye, Float3 target, Float3 up);
    static Mat4   ViewFPS(Float3 eye, float pitch, float yaw);
    static Mat4   ViewArcBall(Float3 move, Quat rot, Float3 target_pos);
    static Mat4   Perspective(float width, float height, float zn, float zf, bool d3dNdc = false);
    static Mat4   PerspectiveLH(float width, float height, float zn, float zf, bool d3dNdc = false);
    static Mat4   PerspectiveOffCenter(float xmin, float ymin, float xmax, float ymax,
                                       float zn, float zf, bool d3dNdc = false);
    static Mat4   PerspectiveOffCenterLH(float xmin, float ymin, float xmax, float ymax,
                                         float zn, float zf, bool d3dNdc = false);
    static Mat4   PerspectiveFOV(float fov_y, float aspect, float zn, float zf, bool d3dNdc = false);
    static Mat4   PerspectiveFOVLH(float fov_y, float aspect, float zn, float zf, bool d3dNdc = false);
    static Mat4   Ortho(float width, float height, float zn, float zf, float offset = 0, bool d3dNdc = false);
    static Mat4   OrthoLH(float width, float height, float zn, float zf, float offset = 0, bool d3dNdc = false);
    static Mat4   OrthoOffCenter(float xmin, float ymin, float xmax, float ymax, float zn,
                                 float zf, float offset = 0, bool d3dNdc = false);
    static Mat4   OrthoOffCenterLH(float xmin, float ymin, float xmax, float ymax, float zn,
                                   float zf, float offset = 0, bool d3dNdc = false);
    static Mat4   TransformMat(float _tx, float _ty, float _tz, 
                               float _ax, float _ay, float _az,
                               float _sx, float _sy, float _sz);
    static Mat4   TransformMat(Float3 translation, Quat rotation, Float3 scale);
    static Mat4   Mul(const Mat4& _a, const Mat4& _b);
    static Mat4   Inverse(const Mat4& _a);
    static Mat4   InverseTransformMat(const Mat4& _a);
    static Quat   ToQuat(const Mat4& _mat);
    static Mat4   FromQuat(Quat q);
    static Mat4   ProjectPlane(Float3 planeNormal);
};

struct RectFloat 
{
    union {
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
    };

    RectFloat() = default;

    explicit constexpr RectFloat(float _xmin, float _ymin, float _xmax, float _ymax) 
    : xmin(_xmin), ymin(_ymin), xmax(_xmax), ymax(_ymax)
    {
    }

    explicit constexpr RectFloat(const float* _vmin, const float* _vmax) 
    : vmin { _vmin[0], _vmin[1] },
      vmax { _vmax[0], _vmax[1] }
    {
    }

    explicit constexpr RectFloat(Float2 _vmin, Float2 _vmax) :
        RectFloat(_vmin.f, _vmax.f)
    {
    }

    bool   IsEmpty() const;
    float  Width() const;
    float  Height() const;

    static RectFloat   CenterExtents(Float2 center, Float2 extents);
    static RectFloat   Expand(const RectFloat rc, Float2 expand);
    static bool   TestPoint(const RectFloat rc, Float2 pt);
    static bool   Test(const RectFloat rc1, const RectFloat rc2);
    static void   AddPoint(RectFloat* rc, Float2 pt);
    static Float2 GetCorner(const RectFloat* rc, int index);
    static void   GetCorners(Float2 corners[4], const RectFloat* rc);
    static Float2 Extents(const RectFloat rc);
    static Float2 Center(const RectFloat rc);
    static RectFloat   Translate(const RectFloat rc, Float2 pos);
};

struct RectInt 
{
    union {
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
    };


    RectInt() = default;

    explicit constexpr RectInt(int _xmin, int _ymin, int _xmax, int _ymax) 
        : xmin(_xmin),   ymin(_ymin),   xmax(_xmax),   ymax(_ymax)
    {
    }

    explicit constexpr RectInt(const int* _vmin, const int* _vmax) 
    : vmin { _vmin[0], _vmin[1] },
      vmax { _vmax[0], _vmax[1] }
    {
    }

    explicit constexpr RectInt(Int2 _vmin, Int2 _vmax) :
        RectInt(_vmin.n, _vmax.n)
    { 
    }

    bool IsEmpty() const;
    int Width() const;
    int Height() const;
    void SetWidth(int width);
    void SetHeight(int height);

    static RectInt  Expand(const RectInt rc, Int2 expand);
    static bool   TestPoint(const RectInt rc, Int2 pt);
    static bool   Test(const RectInt rc1, const RectInt rc2);
    static void   AddPoint(RectInt* rc, Int2 pt);
    static Int2   GetCorner(const RectInt* rc, int index);
    static void   GetCorners(Int2 corners[4], const RectInt* rc);
};

struct AABB 
{
    union {
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
    };

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

    bool IsEmpty() const;
    Float3 Extents() const;
    Float3 Center() const;
    Float3 Dimensions() const;

    static void   AddPoint(AABB* aabb, Float3 pt);
    static AABB   Unify(const AABB& aabb1, const AABB& aabb2);
    static bool   TestPoint(const AABB& aabb, Float3 pt);
    static bool   Test(const AABB& aabb1, const AABB& aabb2);
    static Float3 GetCorner(const AABB& aabb, int index);
    static void   GetCorners(Float3 corners[8], const AABB& aabb);
    static AABB   Translate(const AABB& aabb, Float3 offset);
    static AABB   SetPos(const AABB& aabb, Float3 pos);
    static AABB   Expand(const AABB& aabb, Float3 expand);
    static AABB   Scale(const AABB& aabb, Float3 scale);
    static AABB   Transform(const AABB& aabb, const Mat4& mat);
};

// 3d plane: a*nx + b*ny + c*nz + d = 0
struct Plane 
{
    union {
        Float4 p;

        struct {
            float normal[3];
            float dist;
        };

        float f[4];
    };

    Plane() = default;
    explicit constexpr Plane(float _nx, float _ny, float _nz, float _d) : p(_nx, _ny, _nz, _d) {}
    explicit constexpr Plane(Float3 _normal, float _d) :
        normal { _normal.x,     _normal.y,  _normal.z}, dist(_d)
    {   
    }

    static Float3 CalcNormal(Float3 _va, Float3 _vb, Float3 _vc);
    static Plane  From3Points(Float3 _va, Float3 _vb, Float3 _vc);
    static Plane  FromNormalPoint(Float3 _normal, Float3 _p);
    static float  Distance(Plane _plane, Float3 _p);
    static Float3 ProjectPoint(Plane _plane, Float3 _p);
    static Float3 Origin(Plane _plane);
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
inline constexpr float M_FLOAT32_MIN  = 1.175494e-38f;
inline constexpr float M_FLOAT32_MAX  = 3.402823e+38f;
inline constexpr float M_FLOAT32_EPSILON = 1.192093e-07f;

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

inline constexpr Color4u COLOR4U_WHITE  { uint8(255), uint8(255), uint8(255), uint8(255) };
inline constexpr Color4u COLOR4U_BLACK  { uint8(0), uint8(0), uint8(0), uint8(255) };
inline constexpr Color4u COLOR4U_RED    { uint8(255), uint8(0), uint8(0), uint8(255) };
inline constexpr Color4u COLOR4U_YELLOW { uint8(255), uint8(255), uint8(0), uint8(255) };
inline constexpr Color4u COLOR4U_GREEN  { uint8(0), uint8(255), uint8(0), uint8(255) };
inline constexpr Color4u COLOR4U_BLUE   { uint8(0), uint8(0), uint8(255), uint8(255) };
inline constexpr Color4u COLOR4U_PURPLE { uint8(255), uint8(0), uint8(255), uint8(255) };

inline constexpr RectFloat RECTFLOAT_EMPTY { M_FLOAT32_MAX, M_FLOAT32_MAX, -M_FLOAT32_MAX, -M_FLOAT32_MAX };
inline constexpr RectInt RECTINT_EMPTY { INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN };
inline constexpr AABB AABB_EMPTY { M_FLOAT32_MAX, M_FLOAT32_MAX, M_FLOAT32_MAX, -M_FLOAT32_MAX, -M_FLOAT32_MAX, -M_FLOAT32_MAX };

PRAGMA_DIAGNOSTIC_POP()    // ignore msvc warnings
