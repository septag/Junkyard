#pragma once

#include "MathScalar.h"

FORCE_INLINE Int2 int2Add(Int2 _a, Int2 _b);
FORCE_INLINE Int2 int2Sub(Int2 _a, Int2 _b);
FORCE_INLINE Int2 int2Min(Int2 _a, Int2 _b);
FORCE_INLINE Int2 int2Max(Int2 _a, Int2 _b);

FORCE_INLINE float  float2Dot(Float2 _a, Float2 _b);
FORCE_INLINE float  float2Len(Float2 _a);
FORCE_INLINE Float2 float2Norm(Float2 _a);
FORCE_INLINE Float2 float2NormLen(Float2 _a, float* outlen);
FORCE_INLINE Float2 float2Min(Float2 _a, Float2 _b);
FORCE_INLINE Float2 float2Max(Float2 _a, Float2 _b);
FORCE_INLINE Float2 float2Lerp(Float2 _a, Float2 _b, float _t);
FORCE_INLINE Float2 float2Abs(Float2 _a);
FORCE_INLINE Float2 float2Neg(Float2 _a);
FORCE_INLINE Float2 float2Add(Float2 _a, Float2 _b);
FORCE_INLINE Float2 float2Addf(Float2 _a, float _b);
FORCE_INLINE Float2 float2Sub(Float2 _a, Float2 _b);
FORCE_INLINE Float2 float2Subf(Float2 _a, float _b);
FORCE_INLINE Float2 float2Mul(Float2 _a, Float2 _b);
FORCE_INLINE Float2 float2Mulf(Float2 _a, float _b);
API          Float2 float2CalcLinearFit2D(const Float2* _points, int _num);

FORCE_INLINE Float3 float3Abs(Float3 _a);
FORCE_INLINE Float3 float3Neg(Float3 _a);
FORCE_INLINE Float3 float3Add(Float3 _a, Float3 _b);
FORCE_INLINE Float3 float3Addf(Float3 _a, float _b);
FORCE_INLINE Float3 float3Sub(Float3 _a, Float3 _b);
FORCE_INLINE Float3 float3Subf(Float3 _a, float _b);
FORCE_INLINE Float3 float3Mul(Float3 _a, Float3 _b);
FORCE_INLINE Float3 float3Mulf(Float3 _a, float _b);
FORCE_INLINE float  float3Dot(Float3 _a, Float3 _b);
FORCE_INLINE Float3 float3Cross(Float3 _a, Float3 _b);
FORCE_INLINE float  float3Len(Float3 _a);
FORCE_INLINE Float3 float3Lerp(Float3 _a, Float3 _b, float _t);
FORCE_INLINE Float3 float3SmoothLerp(Float3 _a, Float3 _b, float _dt, float _h);
FORCE_INLINE Float3 float3Norm(Float3 _a);
FORCE_INLINE Float3 float3NormLen(Float3 _a, float* _outlen);
FORCE_INLINE Float3 float3Min(Float3 _a, Float3 _b);
FORCE_INLINE Float3 float3Max(Float3 _a, Float3 _b);
FORCE_INLINE Float3 float3Rcp(Float3 _a);
FORCE_INLINE void   float3Tangent(Float3* _t, Float3* _b, Float3 _n);
FORCE_INLINE void   float3TangentAngle(Float3* _t, Float3* _b, Float3 _n, float _angle);
FORCE_INLINE Float3 float3FromLatLong(float _u, float _v);
FORCE_INLINE Float2 float3ToLatLong(Float3 _dir);
FORCE_INLINE Float3 float3MulQuat(Float3 _vec, Quat _quat);
API          Float3 float3CalcLinearFit3D(const Float3* _points, int _num);

FORCE_INLINE Float4 float4Mul(Float4 _a, Float4 _b);
FORCE_INLINE Float4 float4Mulf(Float4 _a, float _b);
FORCE_INLINE Float4 float4Add(Float4 _a, Float4 _b);
FORCE_INLINE Float4 float4Sub(Float4 _a, Float4 _b);

API          Float3 planeNormal(Float3 _va, Float3 _vb, Float3 _vc);
API          Plane  plane3Points(Float3 _va, Float3 _vb, Float3 _vc);
API          Plane  planeNormalPoint(Float3 _normal, Float3 _p);
API          float  planeDistance(Plane _plane, Float3 _p);
API          Float3 planeProjectPoint(Plane _plane, Float3 _p);
API          Float3 planeOrigin(Plane _plane);

FORCE_INLINE Rect   rectCenterExtents(Float2 center, Float2 extents);
FORCE_INLINE Rect   rectExpand(const Rect rc, Float2 expand);
FORCE_INLINE bool   rectTestPoint(const Rect rc, Float2 pt);
FORCE_INLINE bool   rectTest(const Rect rc1, const Rect rc2);
FORCE_INLINE void   rectAddPoint(Rect* rc, Float2 pt);
FORCE_INLINE bool   rectIsEmpty(const Rect rc);
FORCE_INLINE Float2 rectGetCorner(const Rect* rc, int index);
FORCE_INLINE void   rectGetCorners(Float2 corners[4], const Rect* rc);
FORCE_INLINE float  rectWidth(const Rect rc);
FORCE_INLINE float  rectHeight(const Rect rc);
FORCE_INLINE Float2 rectExtents(const Rect rc);
FORCE_INLINE Float2 rectCenter(const Rect rc);
FORCE_INLINE Rect   rectTranslate(const Rect rc, Float2 pos);
FORCE_INLINE Recti  rectiExpand(const Recti rc, Int2 expand);
FORCE_INLINE bool   rectiTestPoint(const Recti rc, Int2 pt);
FORCE_INLINE bool   rectiTest(const Recti rc1, const Recti rc2);
FORCE_INLINE void   rectiAddPoint(Recti* rc, Int2 pt);
FORCE_INLINE int    rectiWidth(const Recti rc);
FORCE_INLINE int    rectiHeight(const Recti rc);
FORCE_INLINE Int2   rectiGetCorner(const Recti* rc, int index);
FORCE_INLINE void   rectiGetCorners(Int2 corners[4], const Recti* rc);

FORCE_INLINE Float4 mat4Row1(const Mat4& m);
FORCE_INLINE Float4 mat4Row2(const Mat4& m);
FORCE_INLINE Float4 mat4Row3(const Mat4& m);
FORCE_INLINE Float4 mat4Row4(const Mat4& m);
FORCE_INLINE Mat4   mat4Translate(float _tx, float _ty, float _tz);
FORCE_INLINE Mat4   mat4Scale(float _sx, float _sy, float _sz);
FORCE_INLINE Mat4   mat4Scalef(float _scale);
FORCE_INLINE Mat4   mat4RotateX(float _ax);
FORCE_INLINE Mat4   mat4RotateY(float _ay);
FORCE_INLINE Mat4   mat4RotateZ(float _az);
FORCE_INLINE Mat4   mat4RotateXY(float _ax, float _ay);
FORCE_INLINE Mat4   mat4RotateXYZ(float _ax, float _ay, float _az);
FORCE_INLINE Mat4   mat4RotateZYX(float _ax, float _ay, float _az);
FORCE_INLINE Mat4   mat4ToQuatTranslate(Quat _quat, Float3 _translation);
FORCE_INLINE Mat4   mat4ToQuatTranslateHMD(Quat _quat, Float3 _translation);
FORCE_INLINE Float3 mat4MulFloat3(const Mat4& _mat, Float3 _vec);
FORCE_INLINE Float3 mat4MulFloat3_xyz0(const Mat4& _mat, Float3 _vec);
FORCE_INLINE Float3 mat4MulFloat3H(const Mat4& _mat, Float3 _vec);
FORCE_INLINE Float4 mat4MulFloat4(const Mat4& _mat, Float4 _vec);
FORCE_INLINE Float4 float4Mul(Float4 _a, Float4 _b);
FORCE_INLINE Float4 float4Mulf(Float4 _a, float _b);
FORCE_INLINE Float4 float4Add(Float4 _a, Float4 _b);
FORCE_INLINE Float4 float4Sub(Float4 _a, Float4 _b);
FORCE_INLINE Mat4   mat4Transpose(const Mat4& _a);
FORCE_INLINE void   mat4ProjFlipHandedness(Mat4* _dst, const Mat4& _src);
FORCE_INLINE void   mat4ViewFlipHandedness(Mat4* _dst, const Mat4& _src);
API          Mat4   mat4FromNormal(Float3 _normal, float _scale, Float3 _pos);
API          Mat4   mat4FromNormalAngle(Float3 _normal, float _scale, Float3 _pos, float _angle);
API          Mat4   mat4ViewLookAt(Float3 eye, Float3 target, Float3 up);
API          Mat4   mat4ViewLookAtLH(Float3 eye, Float3 target, Float3 up);
API          Mat4   mat4ViewFPS(Float3 eye, float pitch, float yaw);
API          Mat4   mat4ViewArcBall(Float3 move, Quat rot, Float3 target_pos);
API          Mat4   mat4Perspective(float width, float height, float zn, float zf, bool d3dNdc = false);
API          Mat4   mat4PerspectiveLH(float width, float height, float zn, float zf, bool d3dNdc = false);
API          Mat4   mat4PerspectiveOffCenter(float xmin, float ymin, float xmax, float ymax,
                                                 float zn, float zf, bool d3dNdc = false);
API          Mat4   mat4PerspectiveOffCenterLH(float xmin, float ymin, float xmax, float ymax,
                                                   float zn, float zf, bool d3dNdc = false);
API          Mat4   mat4PerspectiveFOV(float fov_y, float aspect, float zn, float zf, bool d3dNdc = false);
API          Mat4   mat4PerspectiveFOVLH(float fov_y, float aspect, float zn, float zf, bool d3dNdc = false);
API          Mat4   mat4Ortho(float width, float height, float zn, float zf, float offset = 0, bool d3dNdc = false);
API          Mat4   mat4OrthoLH(float width, float height, float zn, float zf, float offset = 0, bool d3dNdc = false);
API          Mat4   mat4OrthoOffCenter(float xmin, float ymin, float xmax, float ymax, float zn,
                                           float zf, float offset = 0, bool d3dNdc = false);
API          Mat4   mat4OrthoOffCenterLH(float xmin, float ymin, float xmax, float ymax, float zn,
                                             float zf, float offset = 0, bool d3dNdc = false);
API          Mat4   mat4ScaleRotateTranslate(float _sx, float _sy, float _sz, 
                                                 float _ax, float _ay, float _az,
                                                 float _tx, float _ty, float _tz);
API          Mat4   mat4Mul(const Mat4& _a, const Mat4& _b);
API          Mat4   mat4Inverse(const Mat4& _a);
API          Mat4   mat4InverseTransform(const Mat4& _a);
API          Quat   mat4ToQuat(const Mat4& _mat);
API          Mat4   mat4ProjectPlane(Float3 planeNormal);
FORCE_INLINE Transform3D mat4ToTransform3D(const Mat4& mat);

FORCE_INLINE Mat3   mat3Transpose(const Mat3& _a);
FORCE_INLINE Float3 mat3MulFloat3(const Mat3& _mat, Float3 _vec);
FORCE_INLINE Mat3   mat3MulInverse(const Mat3& _a, const Mat3& _b);
FORCE_INLINE Float3 mat3MulFloat3Inverse(const Mat3& mat, Float3 v);
FORCE_INLINE Float2 mat3MulFloat2(const Mat3& _mat, Float2 _vec);
FORCE_INLINE Mat3   mat3Translate(float x, float y);
FORCE_INLINE Mat3   mat3TranslateFloat2(Float2 p);
FORCE_INLINE Mat3   mat3Rotate(float theta);
FORCE_INLINE Mat3   mat3Scale(float sx, float sy);
FORCE_INLINE Mat3   mat3ScaleRotateTranslate(float sx, float sy, float angle, float tx, float ty);
API          Mat3   mat3Inverse(const Mat3& _a);
API          Mat3   mat3Mul(const Mat3& _a, const Mat3& _b);

API          Mat3   quatToMat3(Quat quat);
API          Mat4   quatToMat4(Quat quat);
FORCE_INLINE Float3 quaMulXYZ(Quat _qa, Quat _qb);
FORCE_INLINE Quat   quatMul(Quat p, Quat q);
FORCE_INLINE Quat   quatInverse(Quat _quat);
FORCE_INLINE float  quatDot(Quat _a, Quat _b);
FORCE_INLINE float  quatAngle(Quat _a, Quat _b);
FORCE_INLINE Quat   quatNorm(Quat _quat);
FORCE_INLINE Quat   quatRotateAxis(Float3 _axis, float _angle);
FORCE_INLINE Quat   quatRotateX(float _ax);
FORCE_INLINE Quat   quatRotateY(float _ay);
FORCE_INLINE Quat   quatRotateZ(float _az);
API          Quat   quatLerp(Quat _a, Quat _b, float t);
API          Quat   quatSlerp(Quat _a, Quat _b, float t);
API          Float3 quatToEuler(Quat _quat);
API          Quat   quatFromEuler(Float3 _float3);

FORCE_INLINE Float4 colorToFloat4(Color c);
API          Color  colorBlend(Color _a, Color _b, float _t);
API          Float4 colorToFloat4SRGB(Float4 cf);
API          Float4 colorToFloat4Linear(Float4 c);
API          void   colorRGBToHSV(float _hsv[3], const float _rgb[3]);
API          void   colorHSVToRGB(float _rgb[3], const float _hsv[3]);

FORCE_INLINE bool   AABBIsEmpty(const AABB& aabb);
FORCE_INLINE void   AABBAddPoint(AABB* aabb, Float3 pt);
FORCE_INLINE AABB   AABBUnify(const AABB& aabb1, const AABB& aabb2);
FORCE_INLINE bool   AABBTestPoint(const AABB& aabb, Float3 pt);
FORCE_INLINE bool   AABBTest(const AABB& aabb1, const AABB& aabb2);
FORCE_INLINE Float3 AABBGetCorner(const AABB& aabb, int index);
FORCE_INLINE void   AABBGetCorners(Float3 corners[8], const AABB& aabb);
FORCE_INLINE Float3 AABBExtents(const AABB& aabb);
FORCE_INLINE Float3 AABBCenter(const AABB& aabb);
FORCE_INLINE AABB   AABBTranslate(const AABB& aabb, Float3 offset);
FORCE_INLINE AABB   AABBSetPos(const AABB& aabb, Float3 pos);
FORCE_INLINE AABB   AABBExpand(const AABB& aabb, Float3 expand);
FORCE_INLINE AABB   AABBScale(const AABB& aabb, Float3 scale);
API          AABB   AABBTransform(const AABB& aabb, const Mat4& mat);
API          AABB   AABBFromBox(const Box* box);

FORCE_INLINE Transform3D transform3DMul(const Transform3D& txa, const Transform3D& txb);
FORCE_INLINE Float3      transform3DMulFloat3(const Transform3D& tx, Float3 v);
FORCE_INLINE Float3      transform3DMulFloat3Scale(const Transform3D& tx, Float3 scale, Float3 v);
FORCE_INLINE Transform3D transform3DInverse(const Transform3D& tx);
FORCE_INLINE Float3      transform3DMulFloat3Inverse(const Transform3D& tx, Float3 v);
FORCE_INLINE Transform3D transform3DMulInverse(const Transform3D& txa, const Transform3D& txb);
FORCE_INLINE Mat4        transform3DToMat4(const Transform3D& tx);
FORCE_INLINE Transform3D transform3Df(float x, float y, float z, float rx, float ry, float rz);

////////////////////////////////////////////////////////////////////////////////////////////////////
// implementation

FORCE_INLINE Float3 quaMulXYZ(Quat _qa, Quat _qb)
{
    const float ax = _qa.x;
    const float ay = _qa.y;
    const float az = _qa.z;
    const float aw = _qa.w;

    const float bx = _qb.x;
    const float by = _qb.y;
    const float bz = _qb.z;
    const float bw = _qb.w;

    return Float3(aw * bx + ax * bw + ay * bz - az * by, 
                       aw * by - ax * bz + ay * bw + az * bx,
                       aw * bz + ax * by - ay * bx + az * bw);
}

// The product of two rotation quaternions will be equivalent to the rotation q followed by
// the rotation p
FORCE_INLINE Quat quatMul(Quat p, Quat q)
{
    return Quat(
        p.f[3] * q.f[0] + p.f[0] * q.f[3] + p.f[1] * q.f[2] - p.f[2] * q.f[1],
        p.f[3] * q.f[1] - p.f[0] * q.f[2] + p.f[1] * q.f[3] + p.f[2] * q.f[0],
        p.f[3] * q.f[2] + p.f[0] * q.f[1] - p.f[1] * q.f[0] + p.f[2] * q.f[3],
        p.f[3] * q.f[3] - p.f[0] * q.f[0] - p.f[1] * q.f[1] - p.f[2] * q.f[2]
    );
}

FORCE_INLINE Quat quatInverse(Quat _quat)
{
    return Quat(-_quat.x, -_quat.y, -_quat.z, _quat.w);
}

FORCE_INLINE float quatDot(Quat _a, Quat _b)
{
    return _a.x * _b.x + _a.y * _b.y + _a.z * _b.z + _a.w * _b.w;
}

FORCE_INLINE float quatAngle(Quat _a, Quat _b)
{
    float a = mathAbs(quatDot(_a, _b));
    return mathACos((a < 1.0f ? a : 1.0f) * 2.0f);
}

FORCE_INLINE Quat quatNorm(Quat _quat)
{
    const float inv_norm = mathRsqrt(quatDot(_quat, _quat));
    return Quat(_quat.x*inv_norm, _quat.y*inv_norm, _quat.z*inv_norm, _quat.w*inv_norm);
}

FORCE_INLINE Quat quatRotateAxis(Float3 _axis, float _angle)
{
    const float ha = _angle * 0.5f;
    const float ca = mathCos(ha);
    const float sa = mathSin(ha);
    return Quat(_axis.x * sa, _axis.y * sa, _axis.z * sa, ca);
}

FORCE_INLINE Quat quatRotateX(float _ax)
{
    const float hx = _ax * 0.5f;
    const float cx = mathCos(hx);
    const float sx = mathSin(hx);
    return Quat(sx, 0.0f, 0.0f, cx);
}

FORCE_INLINE Quat quatRotateY(float _ay)
{
    const float hy = _ay * 0.5f;
    const float cy = mathCos(hy);
    const float sy = mathSin(hy);
    return Quat(0.0f, sy, 0.0f, cy);
}

FORCE_INLINE Quat quatRotateZ(float _az)
{
    const float hz = _az * 0.5f;
    const float cz = mathCos(hz);
    const float sz = mathSin(hz);
    return Quat(0.0f, 0.0f, sz, cz);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Vec3
FORCE_INLINE Float3 float3Abs(Float3 _a)
{
    return Float3(mathAbs(_a.x), mathAbs(_a.y), mathAbs(_a.z));
}

FORCE_INLINE Float3 float3Neg(Float3 _a)
{
    return Float3(-_a.x, -_a.y, -_a.z);
}

FORCE_INLINE Float3 float3Add(Float3 _a, Float3 _b)
{
    return Float3(_a.x + _b.x, _a.y + _b.y, _a.z + _b.z);
}

FORCE_INLINE Float3 float3Addf(Float3 _a, float _b)
{
    return Float3(_a.x + _b, _a.y + _b, _a.z + _b);
}

FORCE_INLINE Float3 float3Sub(Float3 _a, Float3 _b)
{
    return Float3(_a.x - _b.x, _a.y - _b.y, _a.z - _b.z);
}

FORCE_INLINE Float3 float3Subf(Float3 _a, float _b)
{
    return Float3(_a.x - _b, _a.y - _b, _a.z - _b);
}

FORCE_INLINE Float3 float3Mul(Float3 _a, Float3 _b)
{
    return Float3(_a.x * _b.x, _a.y * _b.y, _a.z * _b.z);
}

FORCE_INLINE Float3 float3Mulf(Float3 _a, float _b)
{
    return Float3(_a.x * _b, _a.y * _b, _a.z * _b);
}

FORCE_INLINE float float3Dot(Float3 _a, Float3 _b)
{
    return _a.x * _b.x + _a.y * _b.y + _a.z * _b.z;
}

FORCE_INLINE Float3 float3Cross(Float3 _a, Float3 _b)
{
    return Float3(_a.y * _b.z - _a.z * _b.y, 
                  _a.z * _b.x - _a.x * _b.z,
                  _a.x * _b.y - _a.y * _b.x);
}

FORCE_INLINE float float3Len(Float3 _a)
{
    return mathSqrt(float3Dot(_a, _a));
}

FORCE_INLINE Float3 float3Lerp(Float3 _a, Float3 _b, float _t)
{
    return Float3(mathLerp(_a.x, _b.x, _t), mathLerp(_a.y, _b.y, _t), mathLerp(_a.z, _b.z, _t));
}

FORCE_INLINE Float3 float3SmoothLerp(Float3 _a, Float3 _b, float _dt, float _h)
{
    float f = mathExp2(-_dt/_h);
    return Float3(_b.x + (_a.x - _b.x)*f,
                  _b.y + (_a.y - _b.y)*f,
                  _b.z + (_a.z - _b.z)*f);
}

FORCE_INLINE Float3 float3Norm(Float3 _a)
{
    return float3Mulf(_a, mathRsqrt(float3Dot(_a, _a)));
}

FORCE_INLINE Float3 float3NormLen(Float3 _a, float* _outlen)
{
    ASSERT(_outlen);
    const float len = float3Len(_a);
    if (len > 0.0f) {
        const float invlen = 1.0f / len;
        *_outlen = len;
        return Float3(_a.x * invlen, _a.y * invlen, _a.z * invlen);
    } else {
        ASSERT_MSG(0, "Divide by zero");
        return Float3(0.0f, 0.0f, 0.0f);
    }
}

FORCE_INLINE Float3 float3Min(Float3 _a, Float3 _b)
{
    float xmin = _a.x < _b.x ? _a.x : _b.x;
    float ymin = _a.y < _b.y ? _a.y : _b.y;
    float zmin = _a.z < _b.z ? _a.z : _b.z;
    return Float3(xmin, ymin, zmin);
}

FORCE_INLINE Float3 float3Max(Float3 _a, Float3 _b)
{
    float xmax = _a.x > _b.x ? _a.x : _b.x;
    float ymax = _a.y > _b.y ? _a.y : _b.y;
    float zmax = _a.z > _b.z ? _a.z : _b.z;
    return Float3(xmax, ymax, zmax);
}

FORCE_INLINE Float3 float3Rcp(Float3 _a)
{
    return Float3(1.0f / _a.x, 1.0f / _a.y, 1.0f / _a.z);
}

FORCE_INLINE void float3Tangent(Float3* _t, Float3* _b, Float3 _n)
{
    const float nx = _n.x;
    const float ny = _n.y;
    const float nz = _n.z;

    if (mathAbs(nx) > mathAbs(nz)) {
        float inv_len = 1.0f / mathSqrt(nx * nx + nz * nz);
        *_t = Float3(-nz * inv_len, 0.0f, nx * inv_len);
    } else {
        float inv_len = 1.0f / mathSqrt(ny * ny + nz * nz);
        *_t = Float3(0.0f, nz * inv_len, -ny * inv_len);
    }

    *_b = float3Cross(_n, *_t);
}

FORCE_INLINE void float3TangentAngle(Float3* _t, Float3* _b, Float3 _n, float _angle)
{
    float3Tangent(_t, _b, _n);

    const float sa = mathSin(_angle);
    const float ca = mathCos(_angle);

    *_t = Float3(-sa * _b->x + ca * _t->x, -sa * _b->y + ca * _t->y, -sa * _b->z + ca * _t->z);

    *_b = float3Cross(_n, *_t);
}

FORCE_INLINE Float3 float3FromLatLong(float _u, float _v)
{
    const float phi = _u * kPI2;
    const float theta = _v * kPI;

    const float st = mathSin(theta);
    const float sp = mathSin(phi);
    const float ct = mathCos(theta);
    const float cp = mathCos(phi);

    return Float3(-st * sp, -st * cp, ct);
}

FORCE_INLINE Float2 float3ToLatLong(Float3 _dir)
{
    const float phi = mathATan2(_dir.x, _dir.y);
    const float theta = mathACos(_dir.z);

    return Float2((kPI + phi) / kPI2, theta * kInvPI);
}

FORCE_INLINE Float3 float3MulQuat(Float3 _vec, Quat _quat)
{
    Quat tmp0 = quatInverse(_quat);
    Quat qv = Quat(_vec.x, _vec.y, _vec.z, 0.0f);
    Quat tmp1 = quatMul(qv, tmp0);
    return quaMulXYZ(_quat, tmp1);
}

FORCE_INLINE Float4 mat4Row1(const Mat4& m)
{
    return Float4(m.m11, m.m12, m.m13, m.m14);
}

FORCE_INLINE Float4 mat4Row2(const Mat4& m)
{
    return Float4(m.m21, m.m22, m.m23, m.m24);
}

FORCE_INLINE Float4 mat4Row3(const Mat4& m)
{
    return Float4(m.m31, m.m32, m.m33, m.m34);
}

FORCE_INLINE Float4 mat4Row4(const Mat4& m)
{
    return Float4(m.m41, m.m42, m.m43, m.m44);
}


FORCE_INLINE Mat4 mat4Translate(float _tx, float _ty, float _tz)
{
    return Mat4(1.0f, 0.0f, 0.0f, _tx, 
                0.0f, 1.0f, 0.0f, _ty, 
                0.0f, 0.0f, 1.0f, _tz, 
                0.0f, 0.0f, 0.0f, 1.0f);
}

FORCE_INLINE Mat4 mat4Scale(float _sx, float _sy, float _sz)
{
    return Mat4(_sx, 0.0f, 0.0f, 0.0f, 
                0.0f, _sy, 0.0f, 0.0f, 
                0.0f, 0.0f, _sz, 0.0f, 
                0.0f, 0.0f,0.0f, 1.0f);
}

FORCE_INLINE Mat4 mat4Scalef(float _scale)
{
    return mat4Scale(_scale, _scale, _scale);
}

FORCE_INLINE Mat4 mat4RotateX(float _ax)
{
    const float sx = mathSin(_ax);
    const float cx = mathCos(_ax);

    return Mat4(1.0f, 0.0f, 0.0f, 0.0f, 
                0.0f, cx, -sx, 0.0f, 
                0.0f, sx, cx, 0.0f, 
                0.0f, 0.0f, 0.0f, 1.0f);
}

FORCE_INLINE Mat4 mat4RotateY(float _ay)
{
    const float sy = mathSin(_ay);
    const float cy = mathCos(_ay);

    return Mat4(cy, 0.0f, sy, 0.0f, 
                0.0f, 1.0f, 0.0f, 0.0f, 
                -sy, 0.0f, cy, 0.0f, 
                0.0f, 0.0f, 0.0f, 1.0f);
}

FORCE_INLINE Mat4 mat4RotateZ(float _az)
{
    const float sz = mathSin(_az);
    const float cz = mathCos(_az);

    return Mat4(cz, -sz, 0.0f, 0.0f, 
                sz, cz, 0.0f, 0.0f, 
                0.0f, 0.0f, 1.0f, 0.0f, 
                0.0f, 0.0f, 0.0f, 1.0f);
}

FORCE_INLINE Mat4 mat4RotateXY(float _ax, float _ay)
{
    const float sx = mathSin(_ax);
    const float cx = mathCos(_ax);
    const float sy = mathSin(_ay);
    const float cy = mathCos(_ay);

    return Mat4(cy, 0.0f, sy, 0.0f, 
                sx * sy, cx, -sx * cy, 0.0f, 
                -cx * sy, sx, cx * cy, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f);
}

FORCE_INLINE Mat4 mat4RotateXYZ(float _ax, float _ay, float _az)
{
    const float sx = mathSin(_ax);
    const float cx = mathCos(_ax);
    const float sy = mathSin(_ay);
    const float cy = mathCos(_ay);
    const float sz = mathSin(_az);
    const float cz = mathCos(_az);

    return Mat4(cy * cz, -cy * sz, sy, 0.0f, 
                cz * sx * sy + cx * sz, cx * cz - sx * sy * sz, -cy * sx, 0.0f, 
                -cx * cz * sy + sx * sz, cz * sx + cx * sy * sz, cx * cy, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f);
}

FORCE_INLINE Mat4 mat4RotateZYX(float _ax, float _ay, float _az)
{
    const float sx = mathSin(_ax);
    const float cx = mathCos(_ax);
    const float sy = mathSin(_ay);
    const float cy = mathCos(_ay);
    const float sz = mathSin(_az);
    const float cz = mathCos(_az);

    return Mat4(cy * cz, cz * sx * sy - cx * sz, cx * cz * sy + sx * sz, 0.0f, 
                cy * sz, cx * cz + sx * sy * sz, -cz * sx + cx * sy * sz, 0.0f, 
                -sy, cy * sx, cx * cy, 0.0f, 
                0.0f, 0.0f, 0.0f, 1.0f);
};

FORCE_INLINE Mat4 mat4ToQuatTranslate(Quat _quat, Float3 _translation)
{
    Mat4 mat = quatToMat4(_quat);
    mat.m14 = -(mat.m11 * _translation.x + mat.m12 * _translation.y + mat.m13 * _translation.z);
    mat.m24 = -(mat.m21 * _translation.x + mat.m22 * _translation.y + mat.m23 * _translation.z);
    mat.m34 = -(mat.m31 * _translation.x + mat.m32 * _translation.y + mat.m33 * _translation.z);
    return mat;
}

FORCE_INLINE Mat4 mat4ToQuatTranslateHMD(Quat _quat, Float3 _translation)
{
    return mat4ToQuatTranslate(Quat(-_quat.x, -_quat.y, _quat.z, _quat.w), _translation);
}

/// multiply vector3 into 4x4 matrix without considering 4th column, which is not used in transform
/// matrices
FORCE_INLINE Float3 mat4MulFloat3(const Mat4& _mat, Float3 _vec)
{
    return Float3(_vec.x * _mat.m11 + _vec.y * _mat.m12 + _vec.z * _mat.m13 + _mat.m14,
                  _vec.x * _mat.m21 + _vec.y * _mat.m22 + _vec.z * _mat.m23 + _mat.m24,
                  _vec.x * _mat.m31 + _vec.y * _mat.m32 + _vec.z * _mat.m33 + _mat.m34);
}

/// multiply vector3 into rotation part of the matrix only (used for normal vectors, etc...)
FORCE_INLINE Float3 mat4MulFloat3_xyz0(const Mat4& _mat, Float3 _vec)
{
    return Float3(_vec.x * _mat.m11 + _vec.y * _mat.m12 + _vec.z * _mat.m13,
                  _vec.x * _mat.m21 + _vec.y * _mat.m22 + _vec.z * _mat.m23,
                  _vec.x * _mat.m31 + _vec.y * _mat.m32 + _vec.z * _mat.m33);
}

FORCE_INLINE Float3 mat4MulFloat3H(const Mat4& _mat, Float3 _vec)
{
    float xx = _vec.x * _mat.m11 + _vec.y * _mat.m12 + _vec.z * _mat.m13 + _mat.m14;
    float yy = _vec.x * _mat.m21 + _vec.y * _mat.m22 + _vec.z * _mat.m23 + _mat.m24;
    float zz = _vec.x * _mat.m31 + _vec.y * _mat.m32 + _vec.z * _mat.m33 + _mat.m34;
    float ww = _vec.x * _mat.m41 + _vec.y * _mat.m42 + _vec.z * _mat.m43 + _mat.m44;
    float iw = mathSign(ww) / ww;
    return Float3(xx * iw, yy * iw, zz * iw);
}

FORCE_INLINE Float4 mat4MulFloat4(const Mat4& _mat, Float4 _vec)
{
    return Float4(
        _vec.x * _mat.m11 + _vec.y * _mat.m12 + _vec.z * _mat.m13 + _vec.w * _mat.m14,
        _vec.x * _mat.m21 + _vec.y * _mat.m22 + _vec.z * _mat.m23 + _vec.w * _mat.m24,
        _vec.x * _mat.m31 + _vec.y * _mat.m32 + _vec.z * _mat.m33 + _vec.w * _mat.m34,
        _vec.x * _mat.m41 + _vec.y * _mat.m42 + _vec.z * _mat.m43 + _vec.w * _mat.m44);
}

FORCE_INLINE Float4 float4Mul(Float4 _a, Float4 _b)
{
    return Float4(_a.x * _b.x, _a.y * _b.y, _a.z * _b.z, _a.w * _b.w);
}

FORCE_INLINE Float4 float4Mulf(Float4 _a, float _b)
{
    return Float4(_a.x * _b, _a.y * _b, _a.z * _b, _a.w * _b);
}

FORCE_INLINE Float4 float4Add(Float4 _a, Float4 _b)
{
    return Float4(_a.x + _b.x, _a.y + _b.y, _a.z + _b.z, _a.w + _b.w);
}

FORCE_INLINE Float4 float4Sub(Float4 _a, Float4 _b)
{
    return Float4(_a.x - _b.x, _a.y - _b.y, _a.z - _b.z, _a.w - _b.w);
}

FORCE_INLINE Mat4 mat4Transpose(const Mat4& _a)
{
    return Mat4(_a.m11, _a.m21, _a.m31, _a.m41,
                _a.m12, _a.m22, _a.m32, _a.m42, 
                _a.m13, _a.m23, _a.m33, _a.m43, 
                _a.m14, _a.m24, _a.m34, _a.m44);
}


/// Convert LH to RH projection matrix and vice versa.
FORCE_INLINE void mat4ProjFlipHandedness(Mat4* _dst, const Mat4& _src)
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
FORCE_INLINE void mat4ViewFlipHandedness(Mat4* _dst, const Mat4& _src)
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

FORCE_INLINE CONSTFN float colorValueToLinear(float _a)
{
    const float lo = _a / 12.92f;
    const float hi = mathPow((_a + 0.055f) / 1.055f, 2.4f);
    const float result = mathLerp(hi, lo, _a <= 0.04045f ? 1.0f : 0.0f);
    return result;
}

FORCE_INLINE CONSTFN float colorValueToGamma(float _a)
{
    const float lo = _a * 12.92f;
    const float hi = mathPow(mathAbs(_a), 1.0f / 2.4f) * 1.055f - 0.055f;
    const float result = mathLerp(hi, lo, _a <= 0.0031308f ? 1.0f : 0.0f);
    return result;
}

FORCE_INLINE Float4 colorToFloat4(Color c)
{
    float rcp = 1.0f / 255.0f;
    return Float4((float)c.r * rcp, (float)c.g * rcp, (float)c.b * rcp, (float)c.a * rcp);
}


FORCE_INLINE Mat3 mat3Transpose(const Mat3& _a)
{
    return Mat3(_a.m11, _a.m21, _a.m31, 
                _a.m12, _a.m22, _a.m32, 
                _a.m13, _a.m23, _a.m33);
}

FORCE_INLINE Float3 mat3MulFloat3(const Mat3& _mat, Float3 _vec)
{
    return Float3(_vec.x * _mat.m11 + _vec.y * _mat.m12 + _vec.z * _mat.m13,
                  _vec.x * _mat.m21 + _vec.y * _mat.m22 + _vec.z * _mat.m23,
                  _vec.x * _mat.m31 + _vec.y * _mat.m32 + _vec.z * _mat.m33);
}

FORCE_INLINE Mat3 mat3MulInverse(const Mat3& _a, const Mat3& _b)
{
    Mat3 _atrans = mat3Transpose(_a);
    return mat3Mul(_atrans, _b);
}

FORCE_INLINE Float3 mat3MulFloat3Inverse(const Mat3& mat, Float3 v)
{
    Mat3 rmat = mat3Transpose(mat);
    return mat3MulFloat3(rmat, v);
}

FORCE_INLINE Float2 mat3MulFloat2(const Mat3& _mat, Float2 _vec)
{
    return Float2(_vec.x * _mat.m11 + _vec.y * _mat.m12 + _mat.m13,
                  _vec.x * _mat.m21 + _vec.y * _mat.m22 + _mat.m23);
}

FORCE_INLINE Mat3 mat3Translate(float x, float y)
{
    return Mat3(1.0f, 0.0f, x, 
                0.0f, 1.0f, y, 
                0.0f, 0.0f, 1.0f);
}

FORCE_INLINE Mat3 mat3TranslateFloat2(Float2 p)
{
    return mat3Translate(p.x, p.y);
}

FORCE_INLINE Mat3 mat3Rotate(float theta)
{
    float c = mathCos(theta);
    float s = mathSin(theta);
    return Mat3(c, -s, 0.0f, 
                       s, c, 0.0f, 
                       0.0f, 0.0f, 1.0f);
}

FORCE_INLINE Mat3 mat3Scale(float sx, float sy)
{
    return Mat3(sx, 0.0f, 0.0f, 
                0.0f, sy, 0.0f, 
                0.0f, 0.0f, 1.0f);
}

FORCE_INLINE Mat3 mat3ScaleRotateTranslate(float sx, float sy, float angle, float tx, float ty)
{
    // scale -> rotate -> translate
    // result of T(translate) * R(rotate) * S(scale)
    float c = mathCos(angle);
    float s = mathSin(angle);
    return Mat3(sx*c,  -sy*s,  tx, 
                sx*s,   sy*c,  ty, 
                0.0f,   0.0f,  1.0f);
}

//
FORCE_INLINE float float2Dot(Float2 _a, Float2 _b)
{
    return _a.x * _b.x + _a.y * _b.y;
}

FORCE_INLINE float float2Len(Float2 _a)
{
    return mathSqrt(float2Dot(_a, _a));
}

FORCE_INLINE Float2 float2Norm(Float2 _a)
{
    return float2Mulf(_a, mathRsqrt(float2Dot(_a, _a)));
}

FORCE_INLINE Float2 float2NormLen(Float2 _a, float* outlen)
{
    const float len = float2Len(_a);
    if (len > 0.0f) {
        *outlen = len;
        return Float2(_a.x / len, _a.y / len);
    } else {
        ASSERT_MSG(0, "Divide by zero");
        return _a;
    }
}

FORCE_INLINE Float2 float2Min(Float2 _a, Float2 _b)
{
    return Float2(_a.x < _b.x ? _a.x : _b.x, _a.y < _b.y ? _a.y : _b.y);
}

FORCE_INLINE Float2 float2Max(Float2 _a, Float2 _b)
{
    return Float2(_a.x > _b.x ? _a.x : _b.x, _a.y > _b.y ? _a.y : _b.y );
}

FORCE_INLINE Float2 float2Lerp(Float2 _a, Float2 _b, float _t)
{
    return Float2(mathLerp(_a.x, _b.x, _t), mathLerp(_a.y, _b.y, _t));
}

FORCE_INLINE Float2 float2Abs(Float2 _a)
{
    return Float2(mathAbs(_a.x), mathAbs(_a.y));
}

FORCE_INLINE Float2 float2Neg(Float2 _a)
{
    return Float2(-_a.x, -_a.y);
}

FORCE_INLINE Float2 float2Add(Float2 _a, Float2 _b)
{
    return Float2(_a.x + _b.x, _a.y + _b.y);
}

FORCE_INLINE Float2 float2Addf(Float2 _a, float _b)
{
    return Float2(_a.x + _b, _a.y + _b);
}

FORCE_INLINE Float2 float2Sub(Float2 _a, Float2 _b)
{
    return Float2(_a.x - _b.x, _a.y - _b.y);
}

FORCE_INLINE Float2 float2Subf(Float2 _a, float _b)
{
    return Float2(_a.x - _b, _a.y - _b);
}

FORCE_INLINE Float2 float2Mul(Float2 _a, Float2 _b)
{
    return Float2(_a.x * _b.x, _a.y * _b.y);
}

FORCE_INLINE Float2 float2Mulf(Float2 _a, float _b)
{
    return Float2(_a.x * _b, _a.y * _b);
}

FORCE_INLINE Int2 int2Add(Int2 _a, Int2 _b)
{
    return Int2(_a.x + _b.x, _a.y + _b.y);
}

FORCE_INLINE Int2 int2Sub(Int2 _a, Int2 _b)
{
    return Int2(_a.x - _b.x, _a.y - _b.y);
}

FORCE_INLINE Int2 int2Min(Int2 _a, Int2 _b)
{
    return Int2(_a.x < _b.x ? _a.x : _b.x, _a.y < _b.y ? _a.y : _b.y);
}

FORCE_INLINE Int2 int2Max(Int2 _a, Int2 _b)
{
    return Int2(_a.x > _b.x ? _a.x : _b.x, _a.y > _b.y ? _a.y : _b.y);
}

FORCE_INLINE Rect rectCenterExtents(Float2 center, Float2 extents)
{
    return Rect(float2Sub(center, extents), float2Add(center, extents));
}

FORCE_INLINE Rect rectExpand(const Rect rc, Float2 expand)
{
    return Rect(rc.xmin - expand.x, rc.ymin - expand.y, rc.xmax + expand.x, rc.ymax + expand.y);
}

FORCE_INLINE bool rectTestPoint(const Rect rc, Float2 pt)
{
    if (pt.x < rc.xmin || pt.y < rc.ymin || pt.x > rc.xmax || pt.y > rc.ymax)
        return false;
    return true;
}

FORCE_INLINE bool rectTest(const Rect rc1, const Rect rc2)
{
    if (rc1.xmax < rc2.xmin || rc1.xmin > rc2.xmax)
        return false;
    if (rc1.ymax < rc2.ymin || rc1.ymin > rc2.ymax)
        return false;
    return true;
}

FORCE_INLINE void rectAddPoint(Rect* rc, Float2 pt)
{
    *rc = Rect(float2Min(Float2(rc->vmin), pt), float2Max(Float2(rc->vmax), pt));
}

FORCE_INLINE bool rectIsEmpty(const Rect rc)
{
    return rc.xmin >= rc.xmax || rc.ymin >= rc.ymax;
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
FORCE_INLINE Float2 rectGetCorner(const Rect* rc, int index)
{
    return Float2((index & 1) ? rc->xmax : rc->xmin, (index & 2) ? rc->ymax : rc->ymin);
}

FORCE_INLINE void rectGetCorners(Float2 corners[4], const Rect* rc)
{
    for (int i = 0; i < 4; i++)
        corners[0] = rectGetCorner(rc, i);
}

FORCE_INLINE float rectWidth(const Rect rc)
{
    return rc.xmax - rc.xmin;
}

FORCE_INLINE float rectHeight(const Rect rc)
{
    return rc.ymax - rc.ymin;
}

FORCE_INLINE Float2 rectExtents(const Rect rc)
{
    return float2Mulf(float2Sub(Float2(rc.vmax), Float2(rc.vmin)), 0.5f);
}

FORCE_INLINE Float2 rectCenter(const Rect rc)
{
    return float2Mulf(float2Add(Float2(rc.vmin), Float2(rc.vmax)), 0.5f);
}

FORCE_INLINE Rect rectTranslate(const Rect rc, Float2 pos) 
{
    return Rect(float2Add(pos, Float2(rc.vmin)), float2Add(pos, Float2(rc.vmax)));
}

FORCE_INLINE Recti rectiExpand(const Recti rc, Int2 expand)
{
    return Recti(rc.xmin - expand.x, rc.ymin - expand.y, rc.xmax + expand.x, rc.ymax + expand.y);
}

FORCE_INLINE bool rectiTestPoint(const Recti rc, Int2 pt)
{
    if (pt.x < rc.xmin || pt.y < rc.ymin || pt.x > rc.xmax || pt.y > rc.ymax)
        return false;
    return true;
}

FORCE_INLINE bool rectiTest(const Recti rc1, const Recti rc2)
{
    if (rc1.xmax < rc2.xmin || rc1.xmin > rc2.xmax)
        return false;
    if (rc1.ymax < rc2.ymin || rc1.ymin > rc2.ymax)
        return false;
    return true;
}

FORCE_INLINE void rectiAddPoint(Recti* rc, Int2 pt)
{
    *rc = Recti(int2Min(Int2(rc->vmin), pt), int2Max(Int2(rc->vmax), pt));
}

FORCE_INLINE int rectiWidth(const Recti rc)
{
    return rc.xmax - rc.xmin;
}

FORCE_INLINE int rectiHeight(const Recti rc)
{
    return rc.ymax - rc.ymin;
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
FORCE_INLINE Int2 rectiGetCorner(const Recti* rc, int index)
{
    return Int2((index & 1) ? rc->xmax : rc->xmin, (index & 2) ? rc->ymax : rc->ymin);
}

FORCE_INLINE void rectiGetCorners(Int2 corners[4], const Recti* rc)
{
    for (int i = 0; i < 4; i++)
        corners[0] = rectiGetCorner(rc, i);
}

FORCE_INLINE bool AABBIsEmpty(const AABB& aabb)
{
    return aabb.xmin >= aabb.xmax || aabb.ymin >= aabb.ymax || aabb.zmin >= aabb.zmax;
}

FORCE_INLINE void AABBAddPoint(AABB* aabb, Float3 pt)
{
    *aabb = AABB(float3Min(Float3(aabb->vmin), pt), float3Max(Float3(aabb->vmax), pt));
}

FORCE_INLINE AABB AABBUnify(const AABB& aabb1, const AABB& aabb2)
{
    AABB r = aabb1;
    AABBAddPoint(&r, Float3(aabb2.vmin));
    AABBAddPoint(&r, Float3(aabb2.vmax));
    return r;
}

FORCE_INLINE bool AABBTestPoint(const AABB& aabb, Float3 pt)
{
    if (aabb.xmax < pt.x || aabb.xmin > pt.x)
        return false;
    if (aabb.ymax < pt.y || aabb.ymin > pt.y)
        return false;
    if (aabb.zmax < pt.z || aabb.zmin > pt.z)
        return false;
    return true;
}

FORCE_INLINE bool AABBTest(const AABB& aabb1, const AABB& aabb2)
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
FORCE_INLINE Float3 AABBGetCorner(const AABB& aabb, int index)
{
    ASSERT(index < 8);
    return Float3((index & 1) ? aabb.xmax : aabb.xmin,
                  (index & 4) ? aabb.ymax : aabb.ymin,
                  (index & 2) ? aabb.zmax : aabb.zmin);
}

FORCE_INLINE void AABBGetCorners(Float3 corners[8], const AABB& aabb)
{
    for (int i = 0; i < 8; i++)
        corners[i] = AABBGetCorner(aabb, i);
}

FORCE_INLINE Float3 AABBExtents(const AABB& aabb)
{
    return float3Mulf(Float3(aabb.xmax - aabb.xmin, 
                             aabb.ymax - aabb.ymin,
                             aabb.zmax - aabb.zmin), 
                             0.5f);
}

FORCE_INLINE Float3 AABBCenter(const AABB& aabb)
{
    return float3Mulf(float3Add(Float3(aabb.vmin), Float3(aabb.vmax)), 0.5f);
}

FORCE_INLINE AABB AABBTranslate(const AABB& aabb, Float3 offset)
{
    return AABB(float3Add(Float3(aabb.vmin), offset), float3Add(Float3(aabb.vmax), offset));
}

FORCE_INLINE AABB AABBSetPos(const AABB& aabb, Float3 pos)
{
    Float3 e = AABBExtents(aabb);
    return AABB(pos.x - e.x, pos.y - e.y, pos.z - e.z, 
                pos.x + e.x, pos.y + e.y, pos.z + e.z);
}

FORCE_INLINE AABB AABBExpand(const AABB& aabb, Float3 expand)
{
    Float3 p = AABBCenter(aabb);
    Float3 e = float3Add(AABBExtents(aabb), expand);
    return AABB(p.x - e.x, p.y - e.y, p.z - e.z, 
                p.x + e.x, p.y + e.y, p.z + e.z);
}

FORCE_INLINE AABB AABBScale(const AABB& aabb, Float3 scale)
{
    Float3 p = AABBCenter(aabb);
    Float3 e = float3Mul(AABBExtents(aabb), scale);
    return AABB(p.x - e.x, p.y - e.y, p.z - e.z, 
                p.x + e.x, p.y + e.y, p.z + e.z);
}

FORCE_INLINE Transform3D transform3DMul(const Transform3D& txa, const Transform3D& txb)
{
    return Transform3D(float3Add(mat3MulFloat3(txa.rot, txb.pos), txa.pos), mat3Mul(txa.rot, txb.rot));
}

FORCE_INLINE Float3 transform3DMulFloat3(const Transform3D& tx, Float3 v)
{
    return float3Add(mat3MulFloat3(tx.rot, v), tx.pos);
}   

FORCE_INLINE Float3 transform3DMulFloat3Scale(const Transform3D& tx, Float3 scale, Float3 v)
{
    return float3Add(mat3MulFloat3(tx.rot, float3Mul(v, scale)), tx.pos);
}

FORCE_INLINE Transform3D transform3DInverse(const Transform3D& tx)
{   
    Mat3 rotInv = mat3Transpose(tx.rot);
    return Transform3D(mat3MulFloat3(rotInv, float3Mulf(tx.pos, -1.0f)), rotInv);
}

FORCE_INLINE Float3 transform3DMulFloat3Inverse(const Transform3D& tx, Float3 v)
{   
    Mat3 rmat = mat3Transpose(tx.rot);
    return mat3MulFloat3(rmat, float3Sub(v, tx.pos));
}

FORCE_INLINE Transform3D transform3DMulInverse(const Transform3D& txa, const Transform3D& txb)
{
    return Transform3D(mat3MulFloat3Inverse(txa.rot, float3Sub(txb.pos, txa.pos)), mat3MulInverse(txa.rot, txb.rot));
}

FORCE_INLINE Mat4 transform3DToMat4(const Transform3D& tx)
{
    return Mat4(Float4(Float3(tx.rot.fc1), 0.0f),
                Float4(Float3(tx.rot.fc2), 0.0f),
                Float4(Float3(tx.rot.fc3), 0.0f),
                Float4(tx.pos,             1.0f));
}

FORCE_INLINE Transform3D transform3Df(float x, float y, float z, float rx, float ry, float rz)
{
    Mat4 rot = mat4RotateXYZ(rx, ry, rz);
    return Transform3D(Float3(x, y, z), Mat3(rot.fc1, rot.fc2, rot.fc3));
}


FORCE_INLINE Transform3D mat4ToTransform3D(const Mat4& mat)
{
   return Transform3D(Float3(mat.fc4),  Mat3(mat.fc1, mat.fc2, mat.fc3));
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// cpp operators
FORCE_INLINE Float2 operator+(Float2 a, Float2 b)
{
    return float2Add(a, b);
}

FORCE_INLINE Float2 operator-(Float2 a, Float2 b)
{
    return float2Sub(a, b);
}

FORCE_INLINE Float2 operator*(Float2 v, float k)
{
    return float2Mulf(v, k);
}

FORCE_INLINE Float2 operator*(float k, Float2 v)
{
    return float2Mulf(v, k);
}

FORCE_INLINE Float2 operator*(Float2 v0, Float2 v1)
{
    return float2Mul(v0, v1);
}

FORCE_INLINE Int2 operator+(Int2 a, Int2 b)
{
    return int2Add(a, b);
}

FORCE_INLINE Int2 operator-(Int2 a, Int2 b)
{
    return int2Sub(a, b);
}

FORCE_INLINE Float3 operator+(Float3 v1, Float3 v2)
{
    return float3Add(v1, v2);
}

FORCE_INLINE Float3 operator-(Float3 v1, Float3 v2)
{
    return float3Sub(v1, v2);
}

FORCE_INLINE Float3 operator*(Float3 v, float k)
{
    return float3Mulf(v, k);
}

FORCE_INLINE Float3 operator*(float k, Float3 v)
{
    return float3Mulf(v, k);
}

FORCE_INLINE Mat4 operator*(const Mat4& a, const Mat4& b)
{
    return mat4Mul(a, b);
}

FORCE_INLINE Mat3 operator*(const Mat3& a, const Mat3& b)
{
    return mat3Mul(a, b);
}

FORCE_INLINE Quat operator*(const Quat& a, const Quat& b)
{
    return quatMul(a, b);
}

