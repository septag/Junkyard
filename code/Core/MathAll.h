#pragma once

//
// Scalar and Vector math functions
// Contains vector primitives and vector/fpu math functions,
// Individual files:
//      MathTypes: Basic declarations for math primitives. Include this mainly in other headers
//      MathScalar: Scalar math functions: sqrt/sin/cos/Lerp/etc.
//      MathVector: Functions and operators for math primitives: Vector/Matrix/Quaternion/RectFloat
//
// Easings:
//      Reference: https://easings.net/
//                 https://github.com/r-lyeh-archived/tween
// Conventions:
//      - The lib prefers Right-Handed system (default API), although there are functions for
//        both LH or RH system for calulating view/projection matrices 
//      - Rotations are CCW (right thumb for the rotation axis, then fold your fingers around your thumb)
//      - Matrices are Column-Major, but the representation is row-major.
//          which means:
//              mat->mRC -> which R is the row, and C is column index
//              transform vector (v) by matrix (M) = M.v
//              matrix transforms are multiplied in reverse:
//              Vector transform: Vector x Scale->Rotation->Translate = TxRxSxv
//
// 3D coordinate system: Prefered is the Right-handed - Z UP
// Example: pass FLOAT3_UNITZ to Mat4::ViewLookAt's up vector
//      
//            +z
//            ^   ^ +y
//            |  /
//            | /
//            |/      
//            ■-----> +x
//
// 2D coordinate system: Prefered is the Y UP
//
//            +y
//            ^ 
//            |
//            |      
//            ■-----> +x
//
// Vulkan NDC vs D3D NDC (also referenced in Mat4Perspective/Mat4Ortho functions): 
// +Z goes into the screen for both. Normalized between [0, 1]
// 
// Vulkan:                          
//  (-1, -1)                   
//         +-----+-----+       
//         |     |     |       
//         |     |     |       
//         +-----+-----> +x    
//         |     |     |       
//         |     |     |       
//         +-----v-----+       
//              +y      (1, 1) 
//  
// D3D:
//  (-1, 1)     +y             
//         +-----^-----+       
//         |     |     |       
//         |     |     |       
//         +-----+-----> +x    
//         |     |     |       
//         |     |     |       
//         +-----+-----+       
//                     (1, -1) 
//                    
// C++ operators:
//     Some useful operators for basic vector and matrix arithmatic are
//
// Function Aliases: 
//      All member static functions of the types, also include a function in M namespace for convenience
//      See the end of the file for the listing of these functions
//      Those are useful when you put "using namespace M" in your functions 
//
// TODO: convert static member functions to member functions if make sense
//

#include "MathScalar.h"
#include "MathTypes.h"

//    ██╗███╗   ██╗██╗     ██╗███╗   ██╗███████╗███████╗
//    ██║████╗  ██║██║     ██║████╗  ██║██╔════╝██╔════╝
//    ██║██╔██╗ ██║██║     ██║██╔██╗ ██║█████╗  ███████╗
//    ██║██║╚██╗██║██║     ██║██║╚██╗██║██╔══╝  ╚════██║
//    ██║██║ ╚████║███████╗██║██║ ╚████║███████╗███████║
//    ╚═╝╚═╝  ╚═══╝╚══════╝╚═╝╚═╝  ╚═══╝╚══════╝╚══════╝


//     ██████╗ ██╗   ██╗ █████╗ ████████╗
//    ██╔═══██╗██║   ██║██╔══██╗╚══██╔══╝
//    ██║   ██║██║   ██║███████║   ██║   
//    ██║▄▄ ██║██║   ██║██╔══██║   ██║   
//    ╚██████╔╝╚██████╔╝██║  ██║   ██║   
//     ╚══▀▀═╝  ╚═════╝ ╚═╝  ╚═╝   ╚═╝   

FORCE_INLINE Float3 Quat::MulXYZ(Quat qa, Quat qb)
{
    const float ax = qa.x;
    const float ay = qa.y;
    const float az = qa.z;
    const float aw = qa.w;

    const float bx = qb.x;
    const float by = qb.y;
    const float bz = qb.z;
    const float bw = qb.w;

    return Float3(aw * bx + ax * bw + ay * bz - az * by, 
                       aw * by - ax * bz + ay * bw + az * bx,
                       aw * bz + ax * by - ay * bx + az * bw);
}

FORCE_INLINE Float3 Quat::TransformFloat3(Float3 v, Quat q)
{
    Quat tmp0 = Quat::Inverse(q);
    Quat qv = Quat(v.x, v.y, v.z, 0.0f);
    Quat tmp1 = Quat::Mul(qv, tmp0);
    return Quat::MulXYZ(q, tmp1);
}


// The product of two rotation quaternions will be equivalent to the rotation q followed by
// the rotation p
FORCE_INLINE Quat Quat::Mul(Quat p, Quat q)
{
    return Quat(
        p.f[3] * q.f[0] + p.f[0] * q.f[3] + p.f[1] * q.f[2] - p.f[2] * q.f[1],
        p.f[3] * q.f[1] - p.f[0] * q.f[2] + p.f[1] * q.f[3] + p.f[2] * q.f[0],
        p.f[3] * q.f[2] + p.f[0] * q.f[1] - p.f[1] * q.f[0] + p.f[2] * q.f[3],
        p.f[3] * q.f[3] - p.f[0] * q.f[0] - p.f[1] * q.f[1] - p.f[2] * q.f[2]
    );
}

FORCE_INLINE Quat Quat::Inverse(Quat q)
{
    return Quat(-q.x, -q.y, -q.z, q.w);
}

FORCE_INLINE float Quat::Dot(Quat _a, Quat _b)
{
    return _a.x * _b.x + _a.y * _b.y + _a.z * _b.z + _a.w * _b.w;
}

FORCE_INLINE float Quat::Angle(Quat qa, Quat qb)
{
    float a = M::Abs(Quat::Dot(qa, qb));
    return M::ACos((a < 1.0f ? a : 1.0f) * 2.0f);
}

FORCE_INLINE Quat Quat::Norm(Quat q)
{
    const float inv_norm = M::Rsqrt(Quat::Dot(q, q));
    return Quat(q.x*inv_norm, q.y*inv_norm, q.z*inv_norm, q.w*inv_norm);
}

FORCE_INLINE Quat Quat::RotateAxis(Float3 _axis, float _angle)
{
    const float ha = _angle * 0.5f;
    const float ca = M::Cos(ha);
    const float sa = M::Sin(ha);
    return Quat(_axis.x * sa, _axis.y * sa, _axis.z * sa, ca);
}

FORCE_INLINE Quat Quat::RotateX(float ax)
{
    const float hx = ax * 0.5f;
    const float cx = M::Cos(hx);
    const float sx = M::Sin(hx);
    return Quat(sx, 0.0f, 0.0f, cx);
}

FORCE_INLINE Quat Quat::RotateY(float ay)
{
    const float hy = ay * 0.5f;
    const float cy = M::Cos(hy);
    const float sy = M::Sin(hy);
    return Quat(0.0f, sy, 0.0f, cy);
}

FORCE_INLINE Quat Quat::RotateZ(float az)
{
    const float hz = az * 0.5f;
    const float cz = M::Cos(hz);
    const float sz = M::Sin(hz);
    return Quat(0.0f, 0.0f, sz, cz);
}

//    ███████╗██╗      ██████╗  █████╗ ████████╗██████╗ 
//    ██╔════╝██║     ██╔═══██╗██╔══██╗╚══██╔══╝╚════██╗
//    █████╗  ██║     ██║   ██║███████║   ██║    █████╔╝
//    ██╔══╝  ██║     ██║   ██║██╔══██║   ██║    ╚═══██╗
//    ██║     ███████╗╚██████╔╝██║  ██║   ██║   ██████╔╝
//    ╚═╝     ╚══════╝ ╚═════╝ ╚═╝  ╚═╝   ╚═╝   ╚═════╝ 

FORCE_INLINE Float3 Float3::Abs(Float3 _a)
{
    return Float3(M::Abs(_a.x), M::Abs(_a.y), M::Abs(_a.z));
}

FORCE_INLINE Float3 Float3::Neg(Float3 _a)
{
    return Float3(-_a.x, -_a.y, -_a.z);
}

FORCE_INLINE Float3 Float3::Add(Float3 _a, Float3 _b)
{
    return Float3(_a.x + _b.x, _a.y + _b.y, _a.z + _b.z);
}

FORCE_INLINE Float3 Float3::Add(Float3 _a, float _b)
{
    return Float3(_a.x + _b, _a.y + _b, _a.z + _b);
}

FORCE_INLINE Float3 Float3::Sub(Float3 _a, Float3 _b)
{
    return Float3(_a.x - _b.x, _a.y - _b.y, _a.z - _b.z);
}

FORCE_INLINE Float3 Float3::Sub(Float3 _a, float _b)
{
    return Float3(_a.x - _b, _a.y - _b, _a.z - _b);
}

FORCE_INLINE Float3 Float3::Mul(Float3 _a, Float3 _b)
{
    return Float3(_a.x * _b.x, _a.y * _b.y, _a.z * _b.z);
}

FORCE_INLINE Float3 Float3::Mul(Float3 _a, float _b)
{
    return Float3(_a.x * _b, _a.y * _b, _a.z * _b);
}

FORCE_INLINE float Float3::Dot(Float3 _a, Float3 _b)
{
    return _a.x * _b.x + _a.y * _b.y + _a.z * _b.z;
}

FORCE_INLINE Float3 Float3::Cross(Float3 _a, Float3 _b)
{
    return Float3(_a.y * _b.z - _a.z * _b.y, 
                  _a.z * _b.x - _a.x * _b.z,
                  _a.x * _b.y - _a.y * _b.x);
}

FORCE_INLINE float Float3::Len(Float3 _a)
{
    return M::Sqrt(Float3::Dot(_a, _a));
}

FORCE_INLINE Float3 Float3::Lerp(Float3 _a, Float3 _b, float _t)
{
    return Float3(M::Lerp(_a.x, _b.x, _t), M::Lerp(_a.y, _b.y, _t), M::Lerp(_a.z, _b.z, _t));
}

FORCE_INLINE Float3 Float3::SmoothLerp(Float3 _a, Float3 _b, float _dt, float _h)
{
    float f = M::Exp2(-_dt/_h);
    return Float3(_b.x + (_a.x - _b.x)*f,
                  _b.y + (_a.y - _b.y)*f,
                  _b.z + (_a.z - _b.z)*f);
}

FORCE_INLINE Float3 Float3::Norm(Float3 _a)
{
    return Float3::Mul(_a, M::Rsqrt(Float3::Dot(_a, _a)));
}

FORCE_INLINE Float3 Float3::NormLen(Float3 _a, float* _outlen)
{
    ASSERT(_outlen);
    const float len = Float3::Len(_a);
    if (len > 0.0f) {
        const float invlen = 1.0f / len;
        *_outlen = len;
        return Float3(_a.x * invlen, _a.y * invlen, _a.z * invlen);
    } else {
        ASSERT_MSG(0, "Divide by zero");
        return Float3(0.0f, 0.0f, 0.0f);
    }
}

FORCE_INLINE Float3 Float3::Min(Float3 _a, Float3 _b)
{
    float xmin = _a.x < _b.x ? _a.x : _b.x;
    float ymin = _a.y < _b.y ? _a.y : _b.y;
    float zmin = _a.z < _b.z ? _a.z : _b.z;
    return Float3(xmin, ymin, zmin);
}

FORCE_INLINE Float3 Float3::Max(Float3 _a, Float3 _b)
{
    float xmax = _a.x > _b.x ? _a.x : _b.x;
    float ymax = _a.y > _b.y ? _a.y : _b.y;
    float zmax = _a.z > _b.z ? _a.z : _b.z;
    return Float3(xmax, ymax, zmax);
}

FORCE_INLINE Float3 Float3::Rcp(Float3 _a)
{
    return Float3(1.0f / _a.x, 1.0f / _a.y, 1.0f / _a.z);
}

FORCE_INLINE void Float3::Tangent(Float3* _t, Float3* _b, Float3 _n)
{
    const float nx = _n.x;
    const float ny = _n.y;
    const float nz = _n.z;

    if (M::Abs(nx) > M::Abs(nz)) {
        float inv_len = 1.0f / M::Sqrt(nx * nx + nz * nz);
        *_t = Float3(-nz * inv_len, 0.0f, nx * inv_len);
    } else {
        float inv_len = 1.0f / M::Sqrt(ny * ny + nz * nz);
        *_t = Float3(0.0f, nz * inv_len, -ny * inv_len);
    }

    *_b = Float3::Cross(_n, *_t);
}

FORCE_INLINE void Float3::TangentAngle(Float3* _t, Float3* _b, Float3 _n, float _angle)
{
    Float3::Tangent(_t, _b, _n);

    const float sa = M::Sin(_angle);
    const float ca = M::Cos(_angle);

    *_t = Float3(-sa * _b->x + ca * _t->x, -sa * _b->y + ca * _t->y, -sa * _b->z + ca * _t->z);

    *_b = Float3::Cross(_n, *_t);
}

FORCE_INLINE Float3 Float3::FromLatLong(float _u, float _v)
{
    const float phi = _u * M_PI2;
    const float theta = _v * M_PI;

    const float st = M::Sin(theta);
    const float sp = M::Sin(phi);
    const float ct = M::Cos(theta);
    const float cp = M::Cos(phi);

    return Float3(-st * sp, -st * cp, ct);
}

FORCE_INLINE Float2 Float3::ToLatLong(Float3 pos)
{
    const float phi = M::ATan2(pos.x, pos.y);
    const float theta = M::ACos(pos.z);

    return Float2((M_PI + phi) / M_PI2, theta * M_INVPI);
}

//    ███╗   ███╗ █████╗ ████████╗██╗  ██╗
//    ████╗ ████║██╔══██╗╚══██╔══╝██║  ██║
//    ██╔████╔██║███████║   ██║   ███████║
//    ██║╚██╔╝██║██╔══██║   ██║   ╚════██║
//    ██║ ╚═╝ ██║██║  ██║   ██║        ██║
//    ╚═╝     ╚═╝╚═╝  ╚═╝   ╚═╝        ╚═╝

FORCE_INLINE Float4 Mat4::Row1() const 
{
    return Float4(m11, m12, m13, m14);
}

FORCE_INLINE Float4 Mat4::Row2() const
{
    return Float4(m21, m22, m23, m24);
}

FORCE_INLINE Float4 Mat4::Row3() const
{
    return Float4(m31, m32, m33, m34);
}

FORCE_INLINE Float4 Mat4::Row4() const
{
    return Float4(m41, m42, m43, m44);
}


FORCE_INLINE Mat4 Mat4::Translate(float _tx, float _ty, float _tz)
{
    return Mat4(1.0f, 0.0f, 0.0f, _tx, 
                0.0f, 1.0f, 0.0f, _ty, 
                0.0f, 0.0f, 1.0f, _tz, 
                0.0f, 0.0f, 0.0f, 1.0f);
}

FORCE_INLINE Mat4 Mat4::Scale(float _sx, float _sy, float _sz)
{
    return Mat4(_sx, 0.0f, 0.0f, 0.0f, 
                0.0f, _sy, 0.0f, 0.0f, 
                0.0f, 0.0f, _sz, 0.0f, 
                0.0f, 0.0f,0.0f, 1.0f);
}

FORCE_INLINE Mat4 Mat4::Scale(float _scale)
{
    return Mat4::Scale(_scale, _scale, _scale);
}

FORCE_INLINE Mat4 Mat4::RotateX(float _ax)
{
    const float sx = M::Sin(_ax);
    const float cx = M::Cos(_ax);

    return Mat4(1.0f, 0.0f, 0.0f, 0.0f, 
                0.0f, cx, -sx, 0.0f, 
                0.0f, sx, cx, 0.0f, 
                0.0f, 0.0f, 0.0f, 1.0f);
}

FORCE_INLINE Mat4 Mat4::RotateY(float _ay)
{
    const float sy = M::Sin(_ay);
    const float cy = M::Cos(_ay);

    return Mat4(cy, 0.0f, sy, 0.0f, 
                0.0f, 1.0f, 0.0f, 0.0f, 
                -sy, 0.0f, cy, 0.0f, 
                0.0f, 0.0f, 0.0f, 1.0f);
}

FORCE_INLINE Mat4 Mat4::RotateZ(float _az)
{
    const float sz = M::Sin(_az);
    const float cz = M::Cos(_az);

    return Mat4(cz, -sz, 0.0f, 0.0f, 
                sz, cz, 0.0f, 0.0f, 
                0.0f, 0.0f, 1.0f, 0.0f, 
                0.0f, 0.0f, 0.0f, 1.0f);
}

FORCE_INLINE Mat4 Mat4::RotateXY(float _ax, float _ay)
{
    const float sx = M::Sin(_ax);
    const float cx = M::Cos(_ax);
    const float sy = M::Sin(_ay);
    const float cy = M::Cos(_ay);

    return Mat4(cy, 0.0f, sy, 0.0f, 
                sx * sy, cx, -sx * cy, 0.0f, 
                -cx * sy, sx, cx * cy, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f);
}

FORCE_INLINE Mat4 Mat4::RotateXYZ(float _ax, float _ay, float _az)
{
    const float sx = M::Sin(_ax);
    const float cx = M::Cos(_ax);
    const float sy = M::Sin(_ay);
    const float cy = M::Cos(_ay);
    const float sz = M::Sin(_az);
    const float cz = M::Cos(_az);

    return Mat4(cy * cz, -cy * sz, sy, 0.0f, 
                cz * sx * sy + cx * sz, cx * cz - sx * sy * sz, -cy * sx, 0.0f, 
                -cx * cz * sy + sx * sz, cz * sx + cx * sy * sz, cx * cy, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f);
}

FORCE_INLINE Mat4 Mat4::RotateZYX(float _ax, float _ay, float _az)
{
    const float sx = M::Sin(_ax);
    const float cx = M::Cos(_ax);
    const float sy = M::Sin(_ay);
    const float cy = M::Cos(_ay);
    const float sz = M::Sin(_az);
    const float cz = M::Cos(_az);

    return Mat4(cy * cz, cz * sx * sy - cx * sz, cx * cz * sy + sx * sz, 0.0f, 
                cy * sz, cx * cz + sx * sy * sz, -cz * sx + cx * sy * sz, 0.0f, 
                -sy, cy * sx, cx * cy, 0.0f, 
                0.0f, 0.0f, 0.0f, 1.0f);
};

FORCE_INLINE Mat4 Mat4::ToQuatTranslate(Quat q, Float3 translate)
{
    Mat4 mat = Mat4::FromQuat(q);
    mat.m14 = -(mat.m11 * translate.x + mat.m12 * translate.y + mat.m13 * translate.z);
    mat.m24 = -(mat.m21 * translate.x + mat.m22 * translate.y + mat.m23 * translate.z);
    mat.m34 = -(mat.m31 * translate.x + mat.m32 * translate.y + mat.m33 * translate.z);
    return mat;
}

FORCE_INLINE Mat4 Mat4::ToQuatTranslateHMD(Quat q, Float3 translate)
{
    return Mat4::ToQuatTranslate(Quat(-q.x, -q.y, q.z, q.w), translate);
}

/// multiply vector3 into 4x4 matrix without considering 4th column, which is not used in transform
/// matrices
FORCE_INLINE Float3 Mat4::MulFloat3(const Mat4& _mat, Float3 _vec)
{
    return Float3(_vec.x * _mat.m11 + _vec.y * _mat.m12 + _vec.z * _mat.m13 + _mat.m14,
                  _vec.x * _mat.m21 + _vec.y * _mat.m22 + _vec.z * _mat.m23 + _mat.m24,
                  _vec.x * _mat.m31 + _vec.y * _mat.m32 + _vec.z * _mat.m33 + _mat.m34);
}

/// multiply vector3 into rotation part of the matrix only (used for normal vectors, etc...)
FORCE_INLINE Float3 Mat4::MulFloat3_xyz0(const Mat4& _mat, Float3 _vec)
{
    return Float3(_vec.x * _mat.m11 + _vec.y * _mat.m12 + _vec.z * _mat.m13,
                  _vec.x * _mat.m21 + _vec.y * _mat.m22 + _vec.z * _mat.m23,
                  _vec.x * _mat.m31 + _vec.y * _mat.m32 + _vec.z * _mat.m33);
}

FORCE_INLINE Float3 Mat4::MulFloat3H(const Mat4& _mat, Float3 _vec)
{
    float xx = _vec.x * _mat.m11 + _vec.y * _mat.m12 + _vec.z * _mat.m13 + _mat.m14;
    float yy = _vec.x * _mat.m21 + _vec.y * _mat.m22 + _vec.z * _mat.m23 + _mat.m24;
    float zz = _vec.x * _mat.m31 + _vec.y * _mat.m32 + _vec.z * _mat.m33 + _mat.m34;
    float ww = _vec.x * _mat.m41 + _vec.y * _mat.m42 + _vec.z * _mat.m43 + _mat.m44;
    float iw = M::Sign(ww) / ww;
    return Float3(xx * iw, yy * iw, zz * iw);
}

FORCE_INLINE Float4 Mat4::MulFloat4(const Mat4& _mat, Float4 _vec)
{
    return Float4(
        _vec.x * _mat.m11 + _vec.y * _mat.m12 + _vec.z * _mat.m13 + _vec.w * _mat.m14,
        _vec.x * _mat.m21 + _vec.y * _mat.m22 + _vec.z * _mat.m23 + _vec.w * _mat.m24,
        _vec.x * _mat.m31 + _vec.y * _mat.m32 + _vec.z * _mat.m33 + _vec.w * _mat.m34,
        _vec.x * _mat.m41 + _vec.y * _mat.m42 + _vec.z * _mat.m43 + _vec.w * _mat.m44);
}

/// Convert LH to RH projection matrix and vice versa.
FORCE_INLINE void Mat4::ProjFlipHandedness(Mat4* _dst, const Mat4& _src)
{
    _dst->m11 = -_src.m11;
    _dst->m12 = -_src.m12;
    _dst->m13 = -_src.m13;
    _dst->m14 = -_src.m14;
    _dst->m21 = _src.m21;
    _dst->m22 = _src.m22;
    _dst->m23 = _src.m23;
    _dst->m24 = _src.m24;
    _dst->m31 = -_src.m31;
    _dst->m32 = -_src.m32;
    _dst->m33 = -_src.m33;
    _dst->m34 = -_src.m34;
    _dst->m41 = _src.m41;
    _dst->m42 = _src.m42;
    _dst->m43 = _src.m43;
    _dst->m44 = _src.m44;
}

/// Convert LH to RH view matrix and vice versa.
FORCE_INLINE void Mat4::ViewFlipHandedness(Mat4* _dst, const Mat4& _src)
{
    _dst->m11 = -_src.m11;
    _dst->m12 = _src.m12;
    _dst->m13 = -_src.m13;
    _dst->m14 = _src.m14;
    _dst->m21 = -_src.m21;
    _dst->m22 = _src.m22;
    _dst->m23 = -_src.m23;
    _dst->m24 = _src.m24;
    _dst->m31 = -_src.m31;
    _dst->m32 = _src.m32;
    _dst->m33 = -_src.m33;
    _dst->m34 = _src.m34;
    _dst->m41 = -_src.m41;
    _dst->m42 = _src.m42;
    _dst->m43 = -_src.m43;
    _dst->m44 = _src.m44;
}

FORCE_INLINE Mat4 Mat4::Transpose(const Mat4& _a)
{
    return Mat4(_a.m11, _a.m21, _a.m31, _a.m41,
                _a.m12, _a.m22, _a.m32, _a.m42, 
                _a.m13, _a.m23, _a.m33, _a.m43, 
                _a.m14, _a.m24, _a.m34, _a.m44);
}

//    ███████╗██╗      ██████╗  █████╗ ████████╗██╗  ██╗
//    ██╔════╝██║     ██╔═══██╗██╔══██╗╚══██╔══╝██║  ██║
//    █████╗  ██║     ██║   ██║███████║   ██║   ███████║
//    ██╔══╝  ██║     ██║   ██║██╔══██║   ██║   ╚════██║
//    ██║     ███████╗╚██████╔╝██║  ██║   ██║        ██║
//    ╚═╝     ╚══════╝ ╚═════╝ ╚═╝  ╚═╝   ╚═╝        ╚═╝

FORCE_INLINE Float4 Float4::Mul(Float4 _a, Float4 _b)
{
    return Float4(_a.x * _b.x, _a.y * _b.y, _a.z * _b.z, _a.w * _b.w);
}

FORCE_INLINE Float4 Float4::Mul(Float4 _a, float _b)
{
    return Float4(_a.x * _b, _a.y * _b, _a.z * _b, _a.w * _b);
}

FORCE_INLINE Float4 Float4::Add(Float4 _a, Float4 _b)
{
    return Float4(_a.x + _b.x, _a.y + _b.y, _a.z + _b.z, _a.w + _b.w);
}

FORCE_INLINE Float4 Float4::Sub(Float4 _a, Float4 _b)
{
    return Float4(_a.x - _b.x, _a.y - _b.y, _a.z - _b.z, _a.w - _b.w);
}

//     ██████╗ ██████╗ ██╗      ██████╗ ██████╗ 
//    ██╔════╝██╔═══██╗██║     ██╔═══██╗██╔══██╗
//    ██║     ██║   ██║██║     ██║   ██║██████╔╝
//    ██║     ██║   ██║██║     ██║   ██║██╔══██╗
//    ╚██████╗╚██████╔╝███████╗╚██████╔╝██║  ██║
//     ╚═════╝ ╚═════╝ ╚══════╝ ╚═════╝ ╚═╝  ╚═╝
FORCE_INLINE float Color4u::ValueToLinear(float _a)
{
    const float lo = _a / 12.92f;
    const float hi = M::Pow((_a + 0.055f) / 1.055f, 2.4f);
    const float result = M::Lerp(hi, lo, _a <= 0.04045f ? 1.0f : 0.0f);
    return result;
}

FORCE_INLINE float Color4u::ValueToGamma(float _a)
{
    const float lo = _a * 12.92f;
    const float hi = M::Pow(M::Abs(_a), 1.0f / 2.4f) * 1.055f - 0.055f;
    const float result = M::Lerp(hi, lo, _a <= 0.0031308f ? 1.0f : 0.0f);
    return result;
}

FORCE_INLINE Color4u Color4u::FromFloat4(float r, float g, float b, float a)
{
    return Color4u(
        ((uint8)(r * 255.0f)),
        ((uint8)(g * 255.0f)),
        ((uint8)(b * 255.0f)),
        ((uint8)(a * 255.0f)));
}

FORCE_INLINE Float4 Color4u::ToFloat4(Color4u c)
{
    float rcp = 1.0f / 255.0f;
    return Float4((float)c.r * rcp, (float)c.g * rcp, (float)c.b * rcp, (float)c.a * rcp);
}

FORCE_INLINE Float4 Color4u::ToFloat4(uint8 _r, uint8 _g, uint8 _b, uint8 _a)
{
    return ToFloat4(Color4u(_r, _g, _b, _a));
}

//    ███╗   ███╗ █████╗ ████████╗██████╗ 
//    ████╗ ████║██╔══██╗╚══██╔══╝╚════██╗
//    ██╔████╔██║███████║   ██║    █████╔╝
//    ██║╚██╔╝██║██╔══██║   ██║    ╚═══██╗
//    ██║ ╚═╝ ██║██║  ██║   ██║   ██████╔╝
//    ╚═╝     ╚═╝╚═╝  ╚═╝   ╚═╝   ╚═════╝ 

FORCE_INLINE Float3 Mat3::Row1() const
{
    return Float3(m11, m12, m13);
}

FORCE_INLINE Float3 Mat3::Row2() const
{
    return Float3(m21, m22, m23);
}

FORCE_INLINE Float3 Mat3::Row3() const
{
    return Float3(m31, m32, m33);
}

FORCE_INLINE Mat3 Mat3::Transpose(const Mat3& _a)
{
    return Mat3(_a.m11, _a.m21, _a.m31, 
                _a.m12, _a.m22, _a.m32, 
                _a.m13, _a.m23, _a.m33);
}

FORCE_INLINE Float3 Mat3::MulFloat3(const Mat3& _mat, Float3 _vec)
{
    return Float3(_vec.x * _mat.m11 + _vec.y * _mat.m12 + _vec.z * _mat.m13,
                  _vec.x * _mat.m21 + _vec.y * _mat.m22 + _vec.z * _mat.m23,
                  _vec.x * _mat.m31 + _vec.y * _mat.m32 + _vec.z * _mat.m33);
}

FORCE_INLINE Mat3 Mat3::MulInverse(const Mat3& _a, const Mat3& _b)
{
    Mat3 _atrans = Mat3::Transpose(_a);
    return Mat3::Mul(_atrans, _b);
}

FORCE_INLINE Float3 Mat3::MulFloat3Inverse(const Mat3& mat, Float3 v)
{
    Mat3 rmat = Mat3::Transpose(mat);
    return Mat3::MulFloat3(rmat, v);
}

FORCE_INLINE Float2 Mat3::MulFloat2(const Mat3& _mat, Float2 _vec)
{
    return Float2(_vec.x * _mat.m11 + _vec.y * _mat.m12 + _mat.m13,
                  _vec.x * _mat.m21 + _vec.y * _mat.m22 + _mat.m23);
}

FORCE_INLINE Mat3 Mat3::Translate(float x, float y)
{
    return Mat3(1.0f, 0.0f, x, 
                0.0f, 1.0f, y, 
                0.0f, 0.0f, 1.0f);
}

FORCE_INLINE Mat3 Mat3::TranslateFloat2(Float2 p)
{
    return Mat3::Translate(p.x, p.y);
}

FORCE_INLINE Mat3 Mat3::Rotate(float theta)
{
    float c = M::Cos(theta);
    float s = M::Sin(theta);
    return Mat3(c, -s, 0.0f, 
                       s, c, 0.0f, 
                       0.0f, 0.0f, 1.0f);
}

FORCE_INLINE Mat3 Mat3::Scale(float sx, float sy)
{
    return Mat3(sx, 0.0f, 0.0f, 
                0.0f, sy, 0.0f, 
                0.0f, 0.0f, 1.0f);
}

FORCE_INLINE Mat3 Mat3::ScaleRotateTranslate(float sx, float sy, float angle, float tx, float ty)
{
    // scale -> rotate -> translate
    // result of T(translate) * R(rotate) * S(scale)
    float c = M::Cos(angle);
    float s = M::Sin(angle);
    return Mat3(sx*c,  -sy*s,  tx, 
                sx*s,   sy*c,  ty, 
                0.0f,   0.0f,  1.0f);
}

//    ███████╗██╗      ██████╗  █████╗ ████████╗██████╗ 
//    ██╔════╝██║     ██╔═══██╗██╔══██╗╚══██╔══╝╚════██╗
//    █████╗  ██║     ██║   ██║███████║   ██║    █████╔╝
//    ██╔══╝  ██║     ██║   ██║██╔══██║   ██║   ██╔═══╝ 
//    ██║     ███████╗╚██████╔╝██║  ██║   ██║   ███████╗
//    ╚═╝     ╚══════╝ ╚═════╝ ╚═╝  ╚═╝   ╚═╝   ╚══════╝
FORCE_INLINE float Float2::Dot(Float2 _a, Float2 _b)
{
    return _a.x * _b.x + _a.y * _b.y;
}

FORCE_INLINE float Float2::Len(Float2 _a)
{
    return M::Sqrt(Float2::Dot(_a, _a));
}

FORCE_INLINE Float2 Float2::Norm(Float2 _a)
{
    return Float2::Mul(_a, M::Rsqrt(Float2::Dot(_a, _a)));
}

FORCE_INLINE Float2 Float2::NormLen(Float2 _a, float* outlen)
{
    const float len = Float2::Len(_a);
    if (len > 0.0f) {
        *outlen = len;
        return Float2(_a.x / len, _a.y / len);
    } else {
        ASSERT_MSG(0, "Divide by zero");
        return _a;
    }
}

FORCE_INLINE Float2 Float2::Min(Float2 _a, Float2 _b)
{
    return Float2(_a.x < _b.x ? _a.x : _b.x, _a.y < _b.y ? _a.y : _b.y);
}

FORCE_INLINE Float2 Float2::Max(Float2 _a, Float2 _b)
{
    return Float2(_a.x > _b.x ? _a.x : _b.x, _a.y > _b.y ? _a.y : _b.y );
}

FORCE_INLINE Float2 Float2::Lerp(Float2 _a, Float2 _b, float _t)
{
    return Float2(M::Lerp(_a.x, _b.x, _t), M::Lerp(_a.y, _b.y, _t));
}

FORCE_INLINE Float2 Float2::Abs(Float2 _a)
{
    return Float2(M::Abs(_a.x), M::Abs(_a.y));
}

FORCE_INLINE Float2 Float2::Neg(Float2 _a)
{
    return Float2(-_a.x, -_a.y);
}

FORCE_INLINE Float2 Float2::Add(Float2 _a, Float2 _b)
{
    return Float2(_a.x + _b.x, _a.y + _b.y);
}

FORCE_INLINE Float2 Float2::Add(Float2 _a, float _b)
{
    return Float2(_a.x + _b, _a.y + _b);
}

FORCE_INLINE Float2 Float2::Sub(Float2 _a, Float2 _b)
{
    return Float2(_a.x - _b.x, _a.y - _b.y);
}

FORCE_INLINE Float2 Float2::Sub(Float2 _a, float _b)
{
    return Float2(_a.x - _b, _a.y - _b);
}

FORCE_INLINE Float2 Float2::Mul(Float2 _a, Float2 _b)
{
    return Float2(_a.x * _b.x, _a.y * _b.y);
}

FORCE_INLINE Float2 Float2::Mul(Float2 _a, float _b)
{
    return Float2(_a.x * _b, _a.y * _b);
}

//    ██╗███╗   ██╗████████╗██████╗ 
//    ██║████╗  ██║╚══██╔══╝╚════██╗
//    ██║██╔██╗ ██║   ██║    █████╔╝
//    ██║██║╚██╗██║   ██║   ██╔═══╝ 
//    ██║██║ ╚████║   ██║   ███████╗
//    ╚═╝╚═╝  ╚═══╝   ╚═╝   ╚══════╝
FORCE_INLINE Int2 Int2::Add(Int2 _a, Int2 _b)
{
    return Int2(_a.x + _b.x, _a.y + _b.y);
}

FORCE_INLINE Int2 Int2::Sub(Int2 _a, Int2 _b)
{
    return Int2(_a.x - _b.x, _a.y - _b.y);
}

FORCE_INLINE Int2 Int2::Min(Int2 _a, Int2 _b)
{
    return Int2(_a.x < _b.x ? _a.x : _b.x, _a.y < _b.y ? _a.y : _b.y);
}

FORCE_INLINE Int2 Int2::Max(Int2 _a, Int2 _b)
{
    return Int2(_a.x > _b.x ? _a.x : _b.x, _a.y > _b.y ? _a.y : _b.y);
}

//    ██████╗ ███████╗ ██████╗████████╗
//    ██╔══██╗██╔════╝██╔════╝╚══██╔══╝
//    ██████╔╝█████╗  ██║        ██║   
//    ██╔══██╗██╔══╝  ██║        ██║   
//    ██║  ██║███████╗╚██████╗   ██║   
//    ╚═╝  ╚═╝╚══════╝ ╚═════╝   ╚═╝   

FORCE_INLINE RectFloat RectFloat::CenterExtents(Float2 center, Float2 extents)
{
    return RectFloat(Float2::Sub(center, extents), Float2::Add(center, extents));
}

FORCE_INLINE RectFloat RectFloat::Expand(const RectFloat rc, Float2 expand)
{
    return RectFloat(rc.xmin - expand.x, rc.ymin - expand.y, rc.xmax + expand.x, rc.ymax + expand.y);
}

FORCE_INLINE bool RectFloat::TestPoint(const RectFloat rc, Float2 pt)
{
    if (pt.x < rc.xmin || pt.y < rc.ymin || pt.x > rc.xmax || pt.y > rc.ymax)
        return false;
    return true;
}

FORCE_INLINE bool RectFloat::Test(const RectFloat rc1, const RectFloat rc2)
{
    if (rc1.xmax < rc2.xmin || rc1.xmin > rc2.xmax)
        return false;
    if (rc1.ymax < rc2.ymin || rc1.ymin > rc2.ymax)
        return false;
    return true;
}

FORCE_INLINE void RectFloat::AddPoint(RectFloat* rc, Float2 pt)
{
    *rc = RectFloat(Float2::Min(Float2(rc->vmin), pt), Float2::Max(Float2(rc->vmax), pt));
}

FORCE_INLINE bool RectFloat::IsEmpty() const
{
    return xmin >= xmax || ymin >= ymax;
}

/*
*   2               3
*   -----------------
*   |               |
*   |               |
*   |               |
*   |               |
*   |               |
*   -----------------
*   0               1
*/
FORCE_INLINE Float2 RectFloat::GetCorner(const RectFloat* rc, int index)
{
    return Float2((index & 1) ? rc->xmax : rc->xmin, (index & 2) ? rc->ymax : rc->ymin);
}

FORCE_INLINE void RectFloat::GetCorners(Float2 corners[4], const RectFloat* rc)
{
    for (int i = 0; i < 4; i++)
        corners[0] = RectFloat::GetCorner(rc, i);
}

FORCE_INLINE float RectFloat::Width() const
{
    return xmax - xmin;
}

FORCE_INLINE float RectFloat::Height() const
{
    return ymax - ymin;
}

FORCE_INLINE Float2 RectFloat::Extents(const RectFloat rc)
{
    return Float2::Mul(Float2::Sub(Float2(rc.vmax), Float2(rc.vmin)), 0.5f);
}

FORCE_INLINE Float2 RectFloat::Center(const RectFloat rc)
{
    return Float2::Mul(Float2::Add(Float2(rc.vmin), Float2(rc.vmax)), 0.5f);
}

FORCE_INLINE RectFloat RectFloat::Translate(const RectFloat rc, Float2 pos) 
{
    return RectFloat(Float2::Add(pos, Float2(rc.vmin)), Float2::Add(pos, Float2(rc.vmax)));
}

FORCE_INLINE RectInt RectInt::Expand(const RectInt rc, Int2 expand)
{
    return RectInt(rc.xmin - expand.x, rc.ymin - expand.y, rc.xmax + expand.x, rc.ymax + expand.y);
}

FORCE_INLINE bool RectInt::TestPoint(const RectInt rc, Int2 pt)
{
    if (pt.x < rc.xmin || pt.y < rc.ymin || pt.x > rc.xmax || pt.y > rc.ymax)
        return false;
    return true;
}

FORCE_INLINE bool RectInt::Test(const RectInt rc1, const RectInt rc2)
{
    if (rc1.xmax < rc2.xmin || rc1.xmin > rc2.xmax)
        return false;
    if (rc1.ymax < rc2.ymin || rc1.ymin > rc2.ymax)
        return false;
    return true;
}

FORCE_INLINE void RectInt::AddPoint(RectInt* rc, Int2 pt)
{
    *rc = RectInt(Int2::Min(Int2(rc->vmin), pt), Int2::Max(Int2(rc->vmax), pt));
}

FORCE_INLINE bool RectInt::IsEmpty() const
{
    return xmin >= xmax || ymin >= ymax;
}    

FORCE_INLINE int RectInt::Width() const
{
    return xmax - xmin;
}

FORCE_INLINE int RectInt::Height() const
{
    return ymax - ymin;
}

FORCE_INLINE void RectInt::SetWidth(int width)
{
    xmax = xmin + width;
}

FORCE_INLINE void RectInt::SetHeight(int height)
{
    ymax = ymin + height;
}


/*
*   2               3 (max)
*   -----------------
*   |               |
*   |               |
*   |               |
*   |               |
*   |               |
*   -----------------
*   0 (min)         1
*/
FORCE_INLINE Int2 RectInt::GetCorner(const RectInt* rc, int index)
{
    return Int2((index & 1) ? rc->xmax : rc->xmin, (index & 2) ? rc->ymax : rc->ymin);
}

FORCE_INLINE void RectInt::GetCorners(Int2 corners[4], const RectInt* rc)
{
    for (int i = 0; i < 4; i++)
        corners[0] = GetCorner(rc, i);
}

//     █████╗  █████╗ ██████╗ ██████╗ 
//    ██╔══██╗██╔══██╗██╔══██╗██╔══██╗
//    ███████║███████║██████╔╝██████╔╝
//    ██╔══██║██╔══██║██╔══██╗██╔══██╗
//    ██║  ██║██║  ██║██████╔╝██████╔╝
//    ╚═╝  ╚═╝╚═╝  ╚═╝╚═════╝ ╚═════╝ 

FORCE_INLINE bool AABB::IsEmpty() const
{
    return xmin >= xmax || ymin >= ymax || zmin >= zmax;
}

FORCE_INLINE void AABB::AddPoint(AABB* aabb, Float3 pt)
{
    *aabb = AABB(Float3::Min(Float3(aabb->vmin), pt), Float3::Max(Float3(aabb->vmax), pt));
}

FORCE_INLINE AABB AABB::Unify(const AABB& aabb1, const AABB& aabb2)
{
    AABB r = aabb1;
    AABB::AddPoint(&r, Float3(aabb2.vmin));
    AABB::AddPoint(&r, Float3(aabb2.vmax));
    return r;
}

FORCE_INLINE bool AABB::TestPoint(const AABB& aabb, Float3 pt)
{
    if (aabb.xmax < pt.x || aabb.xmin > pt.x)
        return false;
    if (aabb.ymax < pt.y || aabb.ymin > pt.y)
        return false;
    if (aabb.zmax < pt.z || aabb.zmin > pt.z)
        return false;
    return true;
}

FORCE_INLINE bool AABB::Test(const AABB& aabb1, const AABB& aabb2)
{
    if (aabb1.xmax < aabb2.xmin || aabb1.xmin > aabb2.xmax)
        return false;
    if (aabb1.ymax < aabb2.ymin || aabb1.ymin > aabb2.ymax)
        return false;
    if (aabb1.zmax < aabb2.zmin || aabb1.zmin > aabb2.zmax)
        return false;
    return true;    
}

/*
 *        6                 7
 *        ------------------
 *       /|               /|
 *      / |              / |
 *     /  |             /  |
 *  2 /   |          3 /   |
 *   /----------------/    |
 *   |    |           |    |
 *   |    |           |    |      +Z
 *   |    |           |    |
 *   |    |-----------|----|     |
 *   |   / 4          |   / 5    |  / +Y
 *   |  /             |  /       | /
 *   | /              | /        |/
 *   |/               |/         --------- +X
 *   ------------------
 *  0                 1
 */
FORCE_INLINE Float3 AABB::GetCorner(const AABB& aabb, int index)
{
    ASSERT(index < 8);
    return Float3((index & 1) ? aabb.xmax : aabb.xmin,
                  (index & 4) ? aabb.ymax : aabb.ymin,
                  (index & 2) ? aabb.zmax : aabb.zmin);
}

FORCE_INLINE void AABB::GetCorners(Float3 corners[8], const AABB& aabb)
{
    for (int i = 0; i < 8; i++)
        corners[i] = AABB::GetCorner(aabb, i);
}

FORCE_INLINE Float3 AABB::Extents() const
{
    return Float3::Mul(Float3(xmax - xmin,  ymax - ymin, zmax - zmin), 0.5f);
}

FORCE_INLINE Float3 AABB::Center() const
{
    return Float3::Mul(Float3::Add(Float3(vmin), Float3(vmax)), 0.5f);
}

FORCE_INLINE AABB AABB::Translate(const AABB& aabb, Float3 offset)
{
    return AABB(Float3::Add(Float3(aabb.vmin), offset), Float3::Add(Float3(aabb.vmax), offset));
}

FORCE_INLINE AABB AABB::SetPos(const AABB& aabb, Float3 pos)
{
    Float3 e = aabb.Extents();
    return AABB(pos.x - e.x, pos.y - e.y, pos.z - e.z, 
                pos.x + e.x, pos.y + e.y, pos.z + e.z);
}

FORCE_INLINE AABB AABB::Expand(const AABB& aabb, Float3 expand)
{
    Float3 p = aabb.Center();
    Float3 e = Float3::Add(aabb.Extents(), expand);
    return AABB(p.x - e.x, p.y - e.y, p.z - e.z, 
                p.x + e.x, p.y + e.y, p.z + e.z);
}

FORCE_INLINE AABB AABB::Scale(const AABB& aabb, Float3 scale)
{
    Float3 p = aabb.Center();
    Float3 e = Float3::Mul(aabb.Extents(), scale);
    return AABB(p.x - e.x, p.y - e.y, p.z - e.z, 
                p.x + e.x, p.y + e.y, p.z + e.z);
}

//    ████████╗██████╗  █████╗ ███╗   ██╗███████╗███████╗ ██████╗ ██████╗ ███╗   ███╗██████╗ ██████╗ 
//    ╚══██╔══╝██╔══██╗██╔══██╗████╗  ██║██╔════╝██╔════╝██╔═══██╗██╔══██╗████╗ ████║╚════██╗██╔══██╗
//       ██║   ██████╔╝███████║██╔██╗ ██║███████╗█████╗  ██║   ██║██████╔╝██╔████╔██║ █████╔╝██║  ██║
//       ██║   ██╔══██╗██╔══██║██║╚██╗██║╚════██║██╔══╝  ██║   ██║██╔══██╗██║╚██╔╝██║ ╚═══██╗██║  ██║
//       ██║   ██║  ██║██║  ██║██║ ╚████║███████║██║     ╚██████╔╝██║  ██║██║ ╚═╝ ██║██████╔╝██████╔╝
//       ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═══╝╚══════╝╚═╝      ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝╚═════╝ ╚═════╝ 
FORCE_INLINE Transform3D Transform3D::Mul(const Transform3D& txa, const Transform3D& txb)
{
    return Transform3D(Float3::Add(Mat3::MulFloat3(txa.rot, txb.pos), txa.pos), Mat3::Mul(txa.rot, txb.rot));
}

FORCE_INLINE Float3 Transform3D::MulFloat3(const Transform3D& tx, Float3 v)
{
    return Float3::Add(Mat3::MulFloat3(tx.rot, v), tx.pos);
}   

FORCE_INLINE Float3 Transform3D::MulFloat3Scale(const Transform3D& tx, Float3 scale, Float3 v)
{
    return Float3::Add(Mat3::MulFloat3(tx.rot, Float3::Mul(v, scale)), tx.pos);
}

FORCE_INLINE Transform3D Transform3D::Inverse(const Transform3D& tx)
{   
    Mat3 rotInv = Mat3::Transpose(tx.rot);
    return Transform3D(Mat3::MulFloat3(rotInv, Float3::Mul(tx.pos, -1.0f)), rotInv);
}

FORCE_INLINE Float3 Transform3D::MulFloat3Inverse(const Transform3D& tx, Float3 v)
{   
    Mat3 rmat = Mat3::Transpose(tx.rot);
    return Mat3::MulFloat3(rmat, Float3::Sub(v, tx.pos));
}

FORCE_INLINE Transform3D Transform3D::MulInverse(const Transform3D& txa, const Transform3D& txb)
{
    return Transform3D(Mat3::MulFloat3Inverse(txa.rot, Float3::Sub(txb.pos, txa.pos)), Mat3::MulInverse(txa.rot, txb.rot));
}

FORCE_INLINE Mat4 Transform3D::ToMat4(const Transform3D& tx)
{
    return Mat4(Float4(Float3(tx.rot.fc1), 0.0f),
                Float4(Float3(tx.rot.fc2), 0.0f),
                Float4(Float3(tx.rot.fc3), 0.0f),
                Float4(tx.pos,             1.0f));
}

FORCE_INLINE Transform3D Transform3D::Make(float x, float y, float z, float rx, float ry, float rz)
{
    Mat4 rot = Mat4::RotateXYZ(rx, ry, rz);
    return Transform3D(Float3(x, y, z), Mat3(rot.fc1, rot.fc2, rot.fc3));
}

FORCE_INLINE Transform3D Transform3D::FromMat4(const Mat4& mat)
{
    return Transform3D(Float3(mat.fc4),  Mat3(mat.fc1, mat.fc2, mat.fc3));
}


//
//     ██████╗██████╗ ██████╗      ██████╗ ██████╗ ███████╗██████╗  █████╗ ████████╗ ██████╗ ██████╗ ███████╗
//    ██╔════╝██╔══██╗██╔══██╗    ██╔═══██╗██╔══██╗██╔════╝██╔══██╗██╔══██╗╚══██╔══╝██╔═══██╗██╔══██╗██╔════╝
//    ██║     ██████╔╝██████╔╝    ██║   ██║██████╔╝█████╗  ██████╔╝███████║   ██║   ██║   ██║██████╔╝███████╗
//    ██║     ██╔═══╝ ██╔═══╝     ██║   ██║██╔═══╝ ██╔══╝  ██╔══██╗██╔══██║   ██║   ██║   ██║██╔══██╗╚════██║
//    ╚██████╗██║     ██║         ╚██████╔╝██║     ███████╗██║  ██║██║  ██║   ██║   ╚██████╔╝██║  ██║███████║
//     ╚═════╝╚═╝     ╚═╝          ╚═════╝ ╚═╝     ╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝   ╚═╝    ╚═════╝ ╚═╝  ╚═╝╚══════╝
                                                                                                           
FORCE_INLINE Float2 operator+(Float2 a, Float2 b)
{
    return Float2::Add(a, b);
}

FORCE_INLINE Float2 operator-(Float2 a, Float2 b)
{
    return Float2::Sub(a, b);
}

FORCE_INLINE Float2 operator*(Float2 v, float k)
{
    return Float2::Mul(v, k);
}

FORCE_INLINE Float2 operator*(float k, Float2 v)
{
    return Float2::Mul(v, k);
}

FORCE_INLINE Float2 operator*(Float2 v0, Float2 v1)
{
    return Float2::Mul(v0, v1);
}

FORCE_INLINE Int2 operator+(Int2 a, Int2 b)
{
    return Int2::Add(a, b);
}

FORCE_INLINE Int2 operator-(Int2 a, Int2 b)
{
    return Int2::Sub(a, b);
}

FORCE_INLINE Float3 operator+(Float3 v1, Float3 v2)
{
    return Float3::Add(v1, v2);
}

FORCE_INLINE Float3 operator-(Float3 v1, Float3 v2)
{
    return Float3::Sub(v1, v2);
}

FORCE_INLINE Float3 operator*(Float3 v, float k)
{
    return Float3::Mul(v, k);
}

FORCE_INLINE Float3 operator*(float k, Float3 v)
{
    return Float3::Mul(v, k);
}

FORCE_INLINE Mat4 operator*(const Mat4& a, const Mat4& b)
{
    return Mat4::Mul(a, b);
}

FORCE_INLINE Mat3 operator*(const Mat3& a, const Mat3& b)
{
    return Mat3::Mul(a, b);
}

FORCE_INLINE Quat operator*(const Quat& a, const Quat& b)
{
    return Quat::Mul(a, b);
}

//    ███████╗██╗   ██╗███╗   ██╗ ██████╗     █████╗ ██╗     ██╗ █████╗ ███████╗
//    ██╔════╝██║   ██║████╗  ██║██╔════╝    ██╔══██╗██║     ██║██╔══██╗██╔════╝
//    █████╗  ██║   ██║██╔██╗ ██║██║         ███████║██║     ██║███████║███████╗
//    ██╔══╝  ██║   ██║██║╚██╗██║██║         ██╔══██║██║     ██║██╔══██║╚════██║
//    ██║     ╚██████╔╝██║ ╚████║╚██████╗    ██║  ██║███████╗██║██║  ██║███████║
//    ╚═╝      ╚═════╝ ╚═╝  ╚═══╝ ╚═════╝    ╚═╝  ╚═╝╚══════╝╚═╝╚═╝  ╚═╝╚══════╝

namespace M
{
    // Float2
    FORCE_INLINE float  Float2Dot(Float2 _a, Float2 _b) { return Float2::Dot(_a, _b); }
    FORCE_INLINE float  Float2Len(Float2 _a) { return Float2::Len(_a); }
    FORCE_INLINE Float2 Float2Norm(Float2 _a) { return Float2::Norm(_a); }
    FORCE_INLINE Float2 Float2NormLen(Float2 _a, float* outlen) { return Float2::NormLen(_a, outlen); }
    FORCE_INLINE Float2 Float2Min(Float2 _a, Float2 _b) { return Float2::Min(_a, _b); }
    FORCE_INLINE Float2 Float2Max(Float2 _a, Float2 _b) { return Float2::Max(_a, _b); }
    FORCE_INLINE Float2 Float2Lerp(Float2 _a, Float2 _b, float _t) { return Float2::Lerp(_a, _b, _t); }
    FORCE_INLINE Float2 Float2Abs(Float2 _a) { return Float2::Abs(_a); }
    FORCE_INLINE Float2 Float2Neg(Float2 _a) { return Float2::Neg(_a); }
    FORCE_INLINE Float2 Float2Add(Float2 _a, Float2 _b) { return Float2::Add(_a, _b); }
    FORCE_INLINE Float2 Float2Add(Float2 _a, float _b) { return Float2::Add(_a, _b); }
    FORCE_INLINE Float2 Float2Sub(Float2 _a, Float2 _b) { return Float2::Sub(_a, _b); }
    FORCE_INLINE Float2 Float2Sub(Float2 _a, float _b) { return Float2::Sub(_a, _b); }
    FORCE_INLINE Float2 Float2Mul(Float2 _a, Float2 _b) { return Float2::Mul(_a, _b); }
    FORCE_INLINE Float2 Float2Mul(Float2 _a, float _b) { return Float2::Mul(_a, _b); }
    FORCE_INLINE Float2 Float2CalcLinearFit2D(const Float2* _points, int _num) { return Float2::CalcLinearFit2D(_points, _num); }

    // Float3
    FORCE_INLINE Float3 Float3Abs(Float3 _a) { return Float3::Abs(_a); }
    FORCE_INLINE Float3 Float3Neg(Float3 _a) { return Float3::Neg(_a); }
    FORCE_INLINE Float3 Float3Add(Float3 _a, Float3 _b) { return Float3::Add(_a, _b); }
    FORCE_INLINE Float3 Float3Add(Float3 _a, float _b) { return Float3::Add(_a, _b); }
    FORCE_INLINE Float3 Float3Sub(Float3 _a, Float3 _b) { return Float3::Sub(_a, _b); }
    FORCE_INLINE Float3 Float3Sub(Float3 _a, float _b) { return Float3::Sub(_a, _b); }
    FORCE_INLINE Float3 Float3Mul(Float3 _a, Float3 _b) { return Float3::Mul(_a, _b); }
    FORCE_INLINE Float3 Float3Mul(Float3 _a, float _b) { return Float3::Mul(_a, _b); }
    FORCE_INLINE float  Float3Dot(Float3 _a, Float3 _b) { return Float3::Dot(_a, _b); }
    FORCE_INLINE Float3 Float3Cross(Float3 _a, Float3 _b) { return Float3::Cross(_a, _b); }
    FORCE_INLINE float  Float3Len(Float3 _a) { return Float3::Len(_a); }
    FORCE_INLINE Float3 Float3Lerp(Float3 _a, Float3 _b, float _t) { return Float3::Lerp(_a, _b, _t); }
    FORCE_INLINE Float3 Float3SmoothLerp(Float3 _a, Float3 _b, float _dt, float _h) { return Float3::SmoothLerp(_a, _b, _dt, _h); }
    FORCE_INLINE Float3 Float3Norm(Float3 _a) { return Float3::Norm(_a); }
    FORCE_INLINE Float3 Float3NormLen(Float3 _a, float* _outlen) { return Float3::NormLen(_a, _outlen); }
    FORCE_INLINE Float3 Float3Min(Float3 _a, Float3 _b) { return Float3::Min(_a, _b); }
    FORCE_INLINE Float3 Float3Max(Float3 _a, Float3 _b) { return Float3::Max(_a, _b); }
    FORCE_INLINE Float3 Float3Rcp(Float3 _a) { return Float3::Rcp(_a); }
    FORCE_INLINE void   Float3Tangent(Float3* _t, Float3* _b, Float3 _n) { Float3::Tangent(_t, _b, _n); }
    FORCE_INLINE void   Float3TangentAngle(Float3* _t, Float3* _b, Float3 _n, float _angle) { Float3::TangentAngle(_t, _b, _n, _angle); }
    FORCE_INLINE Float3 Float3FromLatLong(float _u, float _v) { return Float3::FromLatLong(_u, _v); }
    FORCE_INLINE Float2 Float3ToLatLong(Float3 _dir) { return Float3::ToLatLong(_dir); }
    FORCE_INLINE Float3 Float3CalcLinearFit3D(const Float3* _points, int _num) { return Float3::CalcLinearFit3D(_points, _num); }

    // Float4
    FORCE_INLINE Float4 Float4Mul(Float4 _a, Float4 _b) { return Float4::Mul(_a, _b); }
    FORCE_INLINE Float4 Float4Mul(Float4 _a, float _b) { return Float4::Mul(_a, _b); }
    FORCE_INLINE Float4 Float4Add(Float4 _a, Float4 _b) { return Float4::Add(_a, _b); }
    FORCE_INLINE Float4 Float4Sub(Float4 _a, Float4 _b) { return Float4::Sub(_a, _b); }

    // Color
    FORCE_INLINE float ColorValueToLinear(float _a) { return Color4u::ValueToLinear(_a); }
    FORCE_INLINE float ColorValueToGamma(float _a) { return Color4u::ValueToGamma(_a); }
    FORCE_INLINE Float4 ColorToFloat4(Color4u c) { return Color4u::ToFloat4(c); }
    FORCE_INLINE Color4u  ColorBlend(Color4u _a, Color4u _b, float _t) { return Color4u::Blend(_a, _b, _t); }
    FORCE_INLINE Float4 ColorToFloat4SRGB(Float4 cf) { return Color4u::ToFloat4SRGB(cf); }
    FORCE_INLINE Float4 ColorToFloat4Linear(Float4 c) { return Color4u::ToFloat4Linear(c); }
    FORCE_INLINE Float3 ColorRGBtoHSV(Float3 rgb) { return Color4u::RGBtoHSV(rgb); }
    FORCE_INLINE Float3 ColorHSVtoRGB(Float3 hsv) { return Color4u::HSVtoRGB(hsv); }

    // Int2
    FORCE_INLINE Int2 Int2Add(Int2 _a, Int2 _b) { return Int2::Add(_a, _b); }
    FORCE_INLINE Int2 Int2Sub(Int2 _a, Int2 _b) { return Int2::Sub(_a, _b); }
    FORCE_INLINE Int2 Int2Min(Int2 _a, Int2 _b) { return Int2::Min(_a, _b); }
    FORCE_INLINE Int2 Int2Max(Int2 _a, Int2 _b) { return Int2::Max(_a, _b); }

    // Quat
    FORCE_INLINE Float3 QuatMulXYZ(Quat _qa, Quat _qb) { return Quat::MulXYZ(_qa, _qb); }
    FORCE_INLINE Quat   QuatMul(Quat p, Quat q) { return Quat::Mul(p, q); }
    FORCE_INLINE Quat   QuatInverse(Quat _q) { return Quat::Inverse(_q); }
    FORCE_INLINE float  QuatDot(Quat _a, Quat _b) { return Quat::Dot(_a, _b); }
    FORCE_INLINE float  QuatAngle(Quat _a, Quat _b) { return Quat::Angle(_a, _b); }
    FORCE_INLINE Quat   QuatNorm(Quat _q) { return Quat::Norm(_q); }
    FORCE_INLINE Quat   QuatRotateAxis(Float3 _axis, float _angle) { return Quat::RotateAxis(_axis, _angle); }
    FORCE_INLINE Quat   QuatRotateX(float _ax) { return Quat::RotateX(_ax); }
    FORCE_INLINE Quat   QuatRotateY(float _ay) { return Quat::RotateY(_ay); }
    FORCE_INLINE Quat   QuatRotateZ(float _az) { return Quat::RotateZ(_az); }
    FORCE_INLINE Quat   QuatLerp(Quat _a, Quat _b, float t) { return Quat::Lerp(_a, _b, t); }
    FORCE_INLINE Quat   QuatSlerp(Quat _a, Quat _b, float t) { return Quat::Slerp(_a, _b, t); }
    FORCE_INLINE Float3 QuatToEuler(Quat _q) { return Quat::ToEuler(_q); }
    FORCE_INLINE Quat   QuatFromEuler(Float3 _float3) { return Quat::FromEuler(_float3); }
    FORCE_INLINE Float3 QuatTransformFloat3(Float3 v, Quat q) { return Quat::TransformFloat3(v, q); }

    // Mat3
    FORCE_INLINE Mat3   Mat3Transpose(const Mat3& _a) { return Mat3::Transpose(_a); }
    FORCE_INLINE Float3 Mat3MulFloat3(const Mat3& _mat, Float3 _vec) { return Mat3::MulFloat3(_mat, _vec); }
    FORCE_INLINE Mat3   Mat3MulInverse(const Mat3& _a, const Mat3& _b) { return Mat3::MulInverse(_a, _b); }
    FORCE_INLINE Float3 Mat3MulFloat3Inverse(const Mat3& mat, Float3 v) { return Mat3::MulFloat3Inverse(mat, v); }
    FORCE_INLINE Float2 Mat3MulFloat2(const Mat3& _mat, Float2 _vec) { return Mat3::MulFloat2(_mat, _vec); }
    FORCE_INLINE Mat3   Mat3Translate(float x, float y) { return Mat3::Translate(x, y); }
    FORCE_INLINE Mat3   Mat3TranslateFloat2(Float2 p) { return Mat3::TranslateFloat2(p); }
    FORCE_INLINE Mat3   Mat3Rotate(float theta) { return Mat3::Rotate(theta); }
    FORCE_INLINE Mat3   Mat3Scale(float sx, float sy) { return Mat3::Scale(sx, sy); }
    FORCE_INLINE Mat3   Mat3ScaleRotateTranslate(float sx, float sy, float angle, float tx, float ty) { return Mat3::ScaleRotateTranslate(sx, sy, angle, tx, ty); }
    FORCE_INLINE Mat3   Mat3Inverse(const Mat3& _a) { return Mat3::Inverse(_a); }
    FORCE_INLINE Mat3   Mat3Mul(const Mat3& _a, const Mat3& _b) { return Mat3::Mul(_a, _b); }
    FORCE_INLINE Mat3   Mat3Abs(const Mat3& m) { return Mat3::Abs(m); }
    FORCE_INLINE Mat3   Mat3FromQuat(Quat q) { return Mat3::FromQuat(q); }

    // Mat4
    FORCE_INLINE Mat4   Mat4Translate(float _tx, float _ty, float _tz) { return Mat4::Translate(_tx, _ty, _tz); }
    FORCE_INLINE Mat4   Mat4Scale(float _sx, float _sy, float _sz) { return Mat4::Scale(_sx, _sy, _sz); }
    FORCE_INLINE Mat4   Mat4Scalef(float _scale) { return Mat4::Scale(_scale); }
    FORCE_INLINE Mat4   Mat4RotateX(float _ax) { return Mat4::RotateX(_ax); }
    FORCE_INLINE Mat4   Mat4RotateY(float _ay) { return Mat4::RotateY(_ay); }
    FORCE_INLINE Mat4   Mat4RotateZ(float _az) { return Mat4::RotateZ(_az); }
    FORCE_INLINE Mat4   Mat4RotateXY(float _ax, float _ay) { return Mat4::RotateXY(_ax, _ay); }
    FORCE_INLINE Mat4   Mat4RotateXYZ(float _ax, float _ay, float _az) { return Mat4::RotateXYZ(_ax, _ay, _az); }
    FORCE_INLINE Mat4   Mat4RotateZYX(float _ax, float _ay, float _az) { return Mat4::RotateZYX(_ax, _ay, _az); }
    FORCE_INLINE Mat4   Mat4ToQuatTranslate(Quat _quat, Float3 _translation) { return Mat4::ToQuatTranslate(_quat, _translation); }
    FORCE_INLINE Mat4   Mat4ToQuatTranslateHMD(Quat _quat, Float3 _translation) { return Mat4::ToQuatTranslateHMD(_quat, _translation); }
    FORCE_INLINE Float3 Mat4MulFloat3(const Mat4& _mat, Float3 _vec) { return Mat4::MulFloat3(_mat, _vec); }
    FORCE_INLINE Float3 Mat4MulFloat3_xyz0(const Mat4& _mat, Float3 _vec) { return Mat4::MulFloat3_xyz0(_mat, _vec); }
    FORCE_INLINE Float3 Mat4MulFloat3H(const Mat4& _mat, Float3 _vec) { return Mat4::MulFloat3H(_mat, _vec); }
    FORCE_INLINE Float4 Mat4MulFloat4(const Mat4& _mat, Float4 _vec) { return Mat4::MulFloat4(_mat, _vec); }
    FORCE_INLINE Mat4   Mat4Transpose(const Mat4& _a) { return Mat4::Transpose(_a); }
    FORCE_INLINE void   Mat4ProjFlipHandedness(Mat4* _dst, const Mat4& _src) { return Mat4::ProjFlipHandedness(_dst, _src); }
    FORCE_INLINE void   Mat4ViewFlipHandedness(Mat4* _dst, const Mat4& _src) { return Mat4::ViewFlipHandedness(_dst, _src); }
    FORCE_INLINE Mat4   Mat4FromNormal(Float3 _normal, float _scale, Float3 _pos) { return Mat4::FromNormal(_normal, _scale, _pos); }
    FORCE_INLINE Mat4   Mat4FromNormalAngle(Float3 _normal, float _scale, Float3 _pos, float _angle) { return Mat4::FromNormalAngle(_normal, _scale, _pos, _angle); }
    FORCE_INLINE Mat4   Mat4ViewLookAt(Float3 eye, Float3 target, Float3 up) { return Mat4::ViewLookAt(eye, target, up); }
    FORCE_INLINE Mat4   Mat4ViewLookAtLH(Float3 eye, Float3 target, Float3 up) { return Mat4::ViewLookAtLH(eye, target, up); }
    FORCE_INLINE Mat4   Mat4ViewFPS(Float3 eye, float pitch, float yaw) { return Mat4::ViewFPS(eye, pitch, yaw); }
    FORCE_INLINE Mat4   Mat4ViewArcBall(Float3 move, Quat rot, Float3 targetPos) { return Mat4::ViewArcBall(move, rot, targetPos); }
    FORCE_INLINE Mat4   Mat4Perspective(float width, float height, float zn, float zf, bool d3dNdc = false) { return Mat4::Perspective(width, height, zn, zf, d3dNdc); }
    FORCE_INLINE Mat4   Mat4PerspectiveLH(float width, float height, float zn, float zf, bool d3dNdc = false) { return Mat4::PerspectiveLH(width, height, zn, zf, d3dNdc); }
    FORCE_INLINE Mat4   Mat4PerspectiveOffCenter(float xmin, float ymin, float xmax, float ymax,
                                                 float zn, float zf, bool d3dNdc = false) { return Mat4::PerspectiveOffCenter(xmin, ymin, xmax, ymax, zn, zf, d3dNdc); }
    FORCE_INLINE Mat4   Mat4PerspectiveOffCenterLH(float xmin, float ymin, float xmax, float ymax,
                                                   float zn, float zf, bool d3dNdc = false) { return Mat4::PerspectiveOffCenterLH(xmin, ymin, xmax, ymax, zn, zf, d3dNdc); }
    FORCE_INLINE Mat4   Mat4PerspectiveFOV(float fov_y, float aspect, float zn, float zf, bool d3dNdc = false) { return Mat4::PerspectiveFOV(fov_y, aspect, zn, zf, d3dNdc); }
    FORCE_INLINE Mat4   Mat4PerspectiveFOVLH(float fov_y, float aspect, float zn, float zf, bool d3dNdc = false) { return Mat4::PerspectiveFOVLH(fov_y, aspect, zn, zf, d3dNdc); }
    FORCE_INLINE Mat4   Mat4Ortho(float width, float height, float zn, float zf, float offset = 0, bool d3dNdc = false) { return Mat4::Ortho(width, height, zn, zf, offset, d3dNdc); }
    FORCE_INLINE Mat4   Mat4OrthoLH(float width, float height, float zn, float zf, float offset = 0, bool d3dNdc = false) { return Mat4::OrthoLH(width, height, zn, zf, offset, d3dNdc); }
    FORCE_INLINE Mat4   Mat4OrthoOffCenter(float xmin, float ymin, float xmax, float ymax, float zn,
                                           float zf, float offset = 0, bool d3dNdc = false) { return Mat4::OrthoOffCenter(xmin, ymin, xmax, ymax, zn, zf, offset, d3dNdc); }
    FORCE_INLINE Mat4   Mat4OrthoOffCenterLH(float xmin, float ymin, float xmax, float ymax, float zn,
                                             float zf, float offset = 0, bool d3dNdc = false) { return Mat4::OrthoOffCenterLH(xmin, ymin, xmax, ymax, zn, zf, offset, d3dNdc); }
    FORCE_INLINE Mat4   Mat4ScaleRotateTranslate(float _sx, float _sy, float _sz, 
                                                 float _ax, float _ay, float _az,
                                                 float _tx, float _ty, float _tz) { return Mat4::ScaleRotateTranslate(_sx, _sy, _sz, _ax, _ay, _az, _tx, _ty, _tz); }
    FORCE_INLINE Mat4   Mat4Mul(const Mat4& _a, const Mat4& _b) { return Mat4::Mul(_a, _b); }
    FORCE_INLINE Mat4   Mat4Inverse(const Mat4& _a) { return Mat4::Inverse(_a); }
    FORCE_INLINE Mat4   Mat4InverseTransformMat(const Mat4& _a) { return Mat4::InverseTransformMat(_a); }
    FORCE_INLINE Quat   Mat4ToQuat(const Mat4& _mat) { return Mat4::ToQuat(_mat); }
    FORCE_INLINE Mat4   Mat4FromQuat(Quat q) { return Mat4::FromQuat(q); }
    FORCE_INLINE Mat4   Mat4ProjectPlane(Float3 planeNormal) { return Mat4::ProjectPlane(planeNormal); }

    // Rect
    FORCE_INLINE RectFloat   RectCenterExtents(Float2 center, Float2 extents) { return RectFloat::CenterExtents(center, extents); }
    FORCE_INLINE RectFloat   RectExpand(const RectFloat rc, Float2 expand) { return RectFloat::Expand(rc, expand); }
    FORCE_INLINE bool   RectTestPoint(const RectFloat rc, Float2 pt) { return RectFloat::TestPoint(rc, pt); }
    FORCE_INLINE bool   RectTest(const RectFloat rc1, const RectFloat rc2) { return RectFloat::Test(rc1, rc2); }
    FORCE_INLINE void   RectAddPoint(RectFloat* rc, Float2 pt) { RectFloat::AddPoint(rc, pt); }
    FORCE_INLINE Float2 RectGetCorner(const RectFloat* rc, int index) { return RectFloat::GetCorner(rc, index); }
    FORCE_INLINE void   RectGetCorners(Float2 corners[4], const RectFloat* rc) { RectFloat::GetCorners(corners, rc); }
    FORCE_INLINE Float2 RectExtents(const RectFloat rc) { return RectFloat::Extents(rc); }
    FORCE_INLINE Float2 RectCenter(const RectFloat rc) { return RectFloat::Center(rc); }
    FORCE_INLINE RectFloat   RectTranslate(const RectFloat rc, Float2 pos) { return RectFloat::Translate(rc, pos); }

    // RectInt
    FORCE_INLINE RectInt  RectIntExpand(const RectInt rc, Int2 expand) { return RectInt::Expand(rc, expand); }
    FORCE_INLINE bool     RectIntTestPoint(const RectInt rc, Int2 pt) { return RectInt::TestPoint(rc, pt); }
    FORCE_INLINE bool     RectIntTest(const RectInt rc1, const RectInt rc2) { return RectInt::Test(rc1, rc2); }
    FORCE_INLINE void     RectIntAddPoint(RectInt* rc, Int2 pt) { return RectInt::AddPoint(rc, pt); }
    FORCE_INLINE Int2     RectIntGetCorner(const RectInt* rc, int index) { return RectInt::GetCorner(rc, index); }
    FORCE_INLINE void     RectIntGetCorners(Int2 corners[4], const RectInt* rc) { return RectInt::GetCorners(corners, rc); }

    // AABB
    FORCE_INLINE void   AABBAddPoint(AABB* aabb, Float3 pt) { return AABB::AddPoint(aabb, pt); }
    FORCE_INLINE AABB   AABBUnify(const AABB& aabb1, const AABB& aabb2) { return AABB::Unify(aabb1, aabb2); }
    FORCE_INLINE bool   AABBTestPoint(const AABB& aabb, Float3 pt) { return AABB::TestPoint(aabb, pt); }
    FORCE_INLINE bool   AABBTest(const AABB& aabb1, const AABB& aabb2) { return AABB::Test(aabb1, aabb2); }
    FORCE_INLINE Float3 AABBGetCorner(const AABB& aabb, int index) { return AABB::GetCorner(aabb, index); }
    FORCE_INLINE void   AABBGetCorners(Float3 corners[8], const AABB& aabb) { return AABB::GetCorners(corners, aabb); }
    FORCE_INLINE AABB   AABBTranslate(const AABB& aabb, Float3 offset) { return AABB::Translate(aabb, offset); }
    FORCE_INLINE AABB   AABBSetPos(const AABB& aabb, Float3 pos) { return AABB::SetPos(aabb, pos); }
    FORCE_INLINE AABB   AABBExpand(const AABB& aabb, Float3 expand) { return AABB::Expand(aabb, expand); }
    FORCE_INLINE AABB   AABBScale(const AABB& aabb, Float3 scale) { return AABB::Scale(aabb, scale); }
    FORCE_INLINE AABB   AABBTransform(const AABB& aabb, const Mat4& mat) { return AABB::Transform(aabb, mat); }

    // Plane
    FORCE_INLINE Float3 PlaneCalcNormal(Float3 _va, Float3 _vb, Float3 _vc) { return Plane::CalcNormal(_va, _vb, _vc); }
    FORCE_INLINE Plane  PlaneFrom3Points(Float3 _va, Float3 _vb, Float3 _vc) { return Plane::From3Points(_va, _vb, _vc); }
    FORCE_INLINE Plane  PlaneFromNormalPoint(Float3 _normal, Float3 _p) { return Plane::FromNormalPoint(_normal, _p); }
    FORCE_INLINE float  PlaneDistance(Plane _plane, Float3 _p) { return Plane::Distance(_plane, _p); }
    FORCE_INLINE Float3 PlaneProjectPoint(Plane _plane, Float3 _p) { return Plane::ProjectPoint(_plane, _p); }
    FORCE_INLINE Float3 PlaneOrigin(Plane _plane) { return Plane::Origin(_plane); }

    // Transform3D
    FORCE_INLINE Transform3D Transform3DMul(const Transform3D& txa, const Transform3D& txb) { return Transform3D::Mul(txa, txb); }
    FORCE_INLINE Float3      Transform3DMulFloat3(const Transform3D& tx, Float3 v) { return Transform3D::MulFloat3(tx, v); }
    FORCE_INLINE Float3      Transform3DMulFloat3Scale(const Transform3D& tx, Float3 scale, Float3 v) { return Transform3D::MulFloat3Scale(tx, scale, v); }
    FORCE_INLINE Transform3D Transform3DInverse(const Transform3D& tx) { return Transform3D::Inverse(tx); }
    FORCE_INLINE Float3      Transform3DMulFloat3Inverse(const Transform3D& tx, Float3 v) { return Transform3D::MulFloat3Inverse(tx, v); }
    FORCE_INLINE Transform3D Transform3DMulInverse(const Transform3D& txa, const Transform3D& txb) { return Transform3D::MulInverse(txa, txb); }
    FORCE_INLINE Mat4        Transform3DToMat4(const Transform3D& tx) { return Transform3D::ToMat4(tx); }
    FORCE_INLINE Transform3D Transform3DMake(float x, float y, float z, float rx, float ry, float rz) { return Transform3D::Make(x, y, z, rx, ry, rz); }
    FORCE_INLINE Transform3D Transform3DFromMat4(const Mat4& mat) { return Transform3D::FromMat4(mat); }

    // Box
    FORCE_INLINE AABB BoxToAABB(const Box& box) { return Box::ToAABB(box); } 
}
