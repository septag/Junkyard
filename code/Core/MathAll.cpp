#include "MathAll.h"

#include <math.h>

//    ███████╗ ██████╗ █████╗ ██╗      █████╗ ██████╗ 
//    ██╔════╝██╔════╝██╔══██╗██║     ██╔══██╗██╔══██╗
//    ███████╗██║     ███████║██║     ███████║██████╔╝
//    ╚════██║██║     ██╔══██║██║     ██╔══██║██╔══██╗
//    ███████║╚██████╗██║  ██║███████╗██║  ██║██║  ██║
//    ╚══════╝ ╚═════╝╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝
float M::CopySign(float _x, float _y)
{
    return ::copysignf(_x, _y);
}

float M::Floor(float _f)
{
    return ::floorf(_f);
}

float M::Cos(float _a)
{
    return ::cosf(_a);
}

float M::ACos(float _a)
{
    return ::acosf(_a);
}

float M::Sin(float _a)
{
    return ::sinf(_a);
}

float M::ASin(float _a)
{
    return ::asinf(_a);
}

float M::ATan2(float _y, float _x)
{
    return ::atan2f(_y, _x);
}

float M::Exp(float _a)
{
    return ::expf(_a);
}

float M::Log(float _a)
{
    return ::logf(_a);
}

#if !(defined(__SSE2__) || (COMPILER_MSVC && (ARCH_64BIT || _M_IX86_FP >= 2)))
    float M::Sqrt(float _a)
    {
        return ::sqrtf(_a);
    }

    float M::Rsqrt(float _a)
    {
        return 1.0f / ::sqrtf(_a);
    }
#endif // if not __SSE2__

    
//    ███╗   ███╗ █████╗ ████████╗██╗  ██╗
//    ████╗ ████║██╔══██╗╚══██╔══╝██║  ██║
//    ██╔████╔██║███████║   ██║   ███████║
//    ██║╚██╔╝██║██╔══██║   ██║   ╚════██║
//    ██║ ╚═╝ ██║██║  ██║   ██║        ██║
//    ╚═╝     ╚═╝╚═╝  ╚═╝   ╚═╝        ╚═╝
                                        
Mat4 Mat4::ViewLookAt(Float3 eye, Float3 target, Float3 up)
{
    Float3 zaxis = Float3::Norm(Float3::Sub(target, eye));
    Float3 xaxis = Float3::Norm(Float3::Cross(zaxis, up));
    Float3 yaxis = Float3::Cross(xaxis, zaxis);
    
    return Mat4(xaxis.x,    xaxis.y,    xaxis.z,    -Float3::Dot(xaxis, eye), 
                yaxis.x,    yaxis.y,    yaxis.z,    -Float3::Dot(yaxis, eye), 
                -zaxis.x,   -zaxis.y,   -zaxis.z,    Float3::Dot(zaxis, eye),
                0,          0,          0,           1.0f);
}

Mat4 Mat4::ViewLookAtLH(Float3 eye, Float3 target, Float3 up)
{
    Float3 zaxis = Float3::Norm(Float3::Sub(target, eye));
    Float3 xaxis = Float3::Norm(Float3::Cross(up, zaxis));
    Float3 yaxis = Float3::Cross(zaxis, xaxis);
    
    return Mat4(xaxis.x, xaxis.y, xaxis.z, -Float3::Dot(xaxis, eye), 
                yaxis.x, yaxis.y, yaxis.z, -Float3::Dot(yaxis, eye), 
                zaxis.x, zaxis.y, zaxis.z, -Float3::Dot(zaxis, eye),
                0,       0,       0,        1.0f);
}

Mat4 Mat4::ViewFPS(Float3 eye, float pitch, float yaw)
{
    float cos_pitch = M::Cos(pitch);
    float sin_pitch = M::Sin(pitch);
    float cos_yaw = M::Cos(yaw);
    float sin_yaw = M::Sin(yaw);
    
    Float3 xaxis = Float3(cos_yaw, 0, -sin_yaw);
    Float3 yaxis = Float3(sin_yaw * sin_pitch, cos_pitch, cos_yaw * sin_pitch);
    Float3 zaxis = Float3(sin_yaw * cos_pitch, -sin_pitch, cos_pitch * cos_yaw);
    
    return Mat4(xaxis.x, xaxis.y, xaxis.z, -Float3::Dot(xaxis, eye), yaxis.x, yaxis.y, yaxis.z,
                -Float3::Dot(yaxis, eye), zaxis.x, zaxis.y, zaxis.z, -Float3::Dot(zaxis, eye),
                0, 0, 0, 1.0f);
}

Mat4 Mat4::ViewArcBall(Float3 move, Quat rot, Float3 target_pos)
{
    // CameraMat = Tobj * Rcam * Tcam;      // move -> rotate around pivot pt -> move to object pos
    // ViewMat = CameraMat(inv) = Tobj(inv) * Rcam(inv) * Tobj(inv)
    Mat4 translateInv = Mat4::Translate(-move.x, -move.y, -move.z);
    Mat4 rotateInv = Mat4::FromQuat(Quat::Inverse(rot));
    Mat4 translateObjInv = Mat4::Translate(-target_pos.x, -target_pos.y, -target_pos.z);
    Mat4 TR = Mat4::Mul(translateObjInv, rotateInv);
    return Mat4::Mul(TR, translateInv);
}


// Vulkan NDC:(-1, -1)=top-left
// D3D NDC:(-1, 1)=top-left
Mat4 Mat4::Perspective(float width, float height, float zn, float zf, bool d3dNdc)
{
    const float d = zf - zn;
    const float aa = zf / d;
    const float bb = zn * aa;
    const float invY = !d3dNdc ? -1.0f : 1.0f;
    return Mat4(width,  0,              0,      0, 
                0,      height*invY,    0,      0, 
                0,      0,              -aa,    -bb, 
                0,      0,              -1.0f,  0);
}

Mat4 Mat4::PerspectiveLH(float width, float height, float zn, float zf, bool d3dNdc)
{
    const float d = zf - zn;
    const float aa = zf / d;
    const float bb = zn * aa;
    const float invY = !d3dNdc ? -1.0f : 1.0f;
    return Mat4(width,  0,              0,      0, 
                0,      height*invY,    0,      0, 
                0,      0,              aa,     -bb, 
                0,      0,              1.0f,   0);
}

Mat4 Mat4::PerspectiveOffCenter(float xmin, float ymin, float xmax, float ymax, float zn, float zf, bool d3dNdc)
{
    const float d = zf - zn;
    const float aa = zf / d;
    const float bb = zn * aa;
    const float width = xmax - xmin;
    const float height = ymax - ymin;
    const float invY = !d3dNdc ? -1.0f : 1.0f;
    return Mat4(width,  0,              xmin,   0, 
                0,      height*invY,    ymin,   0, 
                0,      0,              -aa,    -bb, 
                0,      0,              -1.0f,  0);
}

Mat4 Mat4::PerspectiveOffCenterLH(float xmin, float ymin, float xmax, float ymax, float zn, float zf, bool d3dNdc)
{
    const float d = zf - zn;
    const float aa = zf / d;
    const float bb = zn * aa;
    const float width = xmax - xmin;
    const float height = ymax - ymin;
    const float invY = !d3dNdc ? -1.0f : 1.0f;
    return Mat4(width,  0,              -xmin,  0, 
                0,      height*invY,    -ymin,  0, 
                0,      0,              aa,     -bb, 
                0,      0,              1.0f,   0);
}

Mat4 Mat4::PerspectiveFOV(float fov_y, float aspect, float zn, float zf, bool d3dNdc)
{
    const float height = 1.0f / M::Tan(fov_y * 0.5f);
    const float width = height / aspect;
    return Mat4::Perspective(width, height, zn, zf, d3dNdc);
}

Mat4 Mat4::PerspectiveFOVLH(float fov_y, float aspect, float zn, float zf, bool d3dNdc)
{
    const float height = 1.0f / M::Tan(fov_y * 0.5f);
    const float width = height / aspect;
    return Mat4::PerspectiveLH(width, height, zn, zf, d3dNdc);
}

Mat4 Mat4::Ortho(float width, float height, float zn, float zf, float offset, bool d3dNdc)
{
    const float d = zf - zn;
    const float cc = 1.0f / d;
    const float ff = -zn / d;
    const float ym = !d3dNdc ? -1.0f : 1.0f;
    
    return Mat4(2.0f / width,   0,                      0,      offset, 
                0,              (2.0f / height)*ym,     0,      0, 
                0,              0,                      -cc,    ff, 
                0,              0,                      0,      1.0f);
}

Mat4 Mat4::OrthoLH(float width, float height, float zn, float zf, float offset, bool d3dNdc)
{
    const float d = zf - zn;
    const float cc = 1.0f / d;
    const float ff = -zn / d;
    const float ym = !d3dNdc ? -1.0f : 1.0f;
    
    return Mat4(2.0f / width,   0,                      0,      offset, 
                0,              (2.0f / height)*ym,     0,      0, 
                0,              0,                      cc,     ff, 
                0,              0,                      0,      1.0f);
}

Mat4 Mat4::OrthoOffCenter(float xmin, float ymin, float xmax, float ymax, float zn, float zf, float offset, bool d3dNdc)
{
    const float width = xmax - xmin;
    const float height = ymax - ymin;
    const float d = zf - zn;
    const float cc = 1.0f / d;
    const float dd = (xmin + xmax) / (xmin - xmax);
    const float ee = (ymin + ymax) / (ymin - ymax);
    const float ff = -zn / d;
    const float ym = !d3dNdc ? -1.0f : 1.0f;
    
    return Mat4(2.0f / width,   0,                  0,      dd + offset, 
                0,              (2.0f / height)*ym, 0,      ee*ym, 
                0,              0,                  -cc,    ff,
                0,              0,                  0,      1.0f);
}

Mat4 Mat4::OrthoOffCenterLH(float xmin, float ymin, float xmax, float ymax, float zn, float zf, float offset, bool d3dNdc)
{
    const float width = xmax - xmin;
    const float height = ymax - ymin;
    const float d = zf - zn;
    const float cc = 1.0f / d;
    const float dd = (xmin + xmax) / (xmin - xmax);
    const float ee = (ymin + ymax) / (ymin - ymax);
    const float ff = -zn / d;
    const float ym = !d3dNdc ? -1.0f : 1.0f;
    
    return Mat4(2.0f / width,   0,                      0,      dd + offset, 
                0,              (2.0f / height)*ym,     0,      ee*ym, 
                0,              0,                      cc,     ff, 
                0,              0,                      0,      1.0f);
}

Mat4 Mat4::ScaleRotateTranslate(float _sx, float _sy, float _sz, float _ax, float _ay, float _az, float _tx, float _ty, float _tz)
{
    float sx, cx, sy, cy, sz, cz;
    
    if (_ax != 0) {
        sx = M::Sin(_ax);
        cx = M::Cos(_ax);
    } else {
        sx = 0;
        cx = 1.0f;
    }
    
    if (_ay != 0) {
        sy = M::Sin(_ay);
        cy = M::Cos(_ay);
    } else {
        sy = 0;
        cy = 1.0f;
    }
    
    if (_az != 0) {
        sz = M::Sin(_az);
        cz = M::Cos(_az);
    } else {
        sz = 0;
        cz = 1.0f;
    }
    
    const float sxsz = sx * sz;
    const float cycz = cy * cz;
    
    return Mat4(_sx * (cycz - sxsz * sy),       _sx * -cx * sz, _sx * (cz * sy + cy * sxsz),    _tx,
                    _sy * (cz * sx * sy + cy * sz), _sy * cx * cz,  _sy * (sy * sz - cycz * sx),    _ty,
                    _sz * -cx * sy,                 _sz * sx,       _sz * cx * cy,                  _tz, 
                    0.0f,                           0.0f,           0.0f,                           1.0f);
}

Mat4 Mat4::FromNormal(Float3 _normal, float _scale, Float3 _pos)
{
    Float3 tangent;
    Float3 bitangent;
    Float3::Tangent(&tangent, &bitangent, _normal);
    
    Float4 row1 = Float4(Float3::Mul(bitangent, _scale), 0.0f);
    Float4 row2 = Float4(Float3::Mul(_normal, _scale), 0.0f);
    Float4 row3 = Float4(Float3::Mul(tangent, _scale), 0.0f);
    
    return Mat4(row1.f, row2.f, row3.f, Float4(_pos, 1.0f).f);
}

Mat4 Mat4::Inverse(const Mat4& _a)
{
    float xx = _a.f[0];
    float xy = _a.f[1];
    float xz = _a.f[2];
    float xw = _a.f[3];
    float yx = _a.f[4];
    float yy = _a.f[5];
    float yz = _a.f[6];
    float yw = _a.f[7];
    float zx = _a.f[8];
    float zy = _a.f[9];
    float zz = _a.f[10];
    float zw = _a.f[11];
    float wx = _a.f[12];
    float wy = _a.f[13];
    float wz = _a.f[14];
    float ww = _a.f[15];
    
    float det = 0.0f;
    det += xx * (yy * (zz * ww - zw * wz) - yz * (zy * ww - zw * wy) + yw * (zy * wz - zz * wy));
    det -= xy * (yx * (zz * ww - zw * wz) - yz * (zx * ww - zw * wx) + yw * (zx * wz - zz * wx));
    det += xz * (yx * (zy * ww - zw * wy) - yy * (zx * ww - zw * wx) + yw * (zx * wy - zy * wx));
    det -= xw * (yx * (zy * wz - zz * wy) - yy * (zx * wz - zz * wx) + yz * (zx * wy - zy * wx));
    
    float det_rcp = 1.0f / det;
    
    return Mat4(
        Float4(
            +(yy * (zz*ww - wz*zw) - yz * (zy * ww - wy * zw) + yw * (zy * wz - wy * zz))*det_rcp,
            -(xy * (zz * ww - wz * zw) - xz * (zy * ww - wy * zw) + xw * (zy * wz - wy * zz))*det_rcp,
            +(xy * (yz * ww - wz * yw) - xz * (yy * ww - wy * yw) + xw * (yy * wz - wy * yz))*det_rcp,
            -(xy * (yz * zw - zz * yw) - xz * (yy * zw - zy * yw) + xw * (yy * zz - zy * yz))*det_rcp),
        Float4(
            -(yx * (zz * ww - wz * zw) - yz * (zx * ww - wx * zw) + yw * (zx * wz - wx * zz))*det_rcp,
            +(xx * (zz * ww - wz * zw) - xz * (zx * ww - wx * zw) + xw * (zx * wz - wx * zz))*det_rcp,
            -(xx * (yz * ww - wz * yw) - xz * (yx * ww - wx * yw) + xw * (yx * wz - wx * yz))*det_rcp,
            +(xx * (yz * zw - zz * yw) - xz * (yx * zw - zx * yw) + xw * (yx * zz - zx * yz))*det_rcp),
        Float4(
            +(yx * (zy * ww - wy * zw) - yy * (zx * ww - wx * zw) + yw * (zx * wy - wx * zy))*det_rcp,
            -(xx * (zy * ww - wy * zw) - xy * (zx * ww - wx * zw) + xw * (zx * wy - wx * zy))*det_rcp,
            +(xx * (yy * ww - wy * yw) - xy * (yx * ww - wx * yw) + xw * (yx * wy - wx * yy))*det_rcp,
            -(xx * (yy * zw - zy * yw) - xy * (yx * zw - zx * yw) + xw * (yx * zy - zx * yy))*det_rcp),
        Float4(
            -(yx * (zy * wz - wy * zz) - yy * (zx * wz - wx * zz) + yz * (zx * wy - wx * zy))*det_rcp,
            +(xx * (zy * wz - wy * zz) - xy * (zx * wz - wx * zz) + xz * (zx * wy - wx * zy))*det_rcp,
            -(xx * (yy * wz - wy * yz) - xy * (yx * wz - wx * yz) + xz * (yx * wy - wx * yy))*det_rcp,
            +(xx * (yy * zz - zy * yz) - xy * (yx * zz - zx * yz) + xz * (yx * zy - zx * yy))*det_rcp));
}

Mat4 Mat4::InverseTransformMat(const Mat4& _mat)
{
    ASSERT((_mat.m41 + _mat.m42 + _mat.m43) == 0 && _mat.m44 == 1.0f);

    float det = (_mat.m11 * (_mat.m22 * _mat.m33 - _mat.m23 * _mat.m32) +
        _mat.m12 * (_mat.m23 * _mat.m31 - _mat.m21 * _mat.m33) +
        _mat.m13 * (_mat.m21 * _mat.m32 - _mat.m22 * _mat.m31));
    float det_rcp = 1.0f / det;
    float tx = _mat.m14;
    float ty = _mat.m24;
    float tz = _mat.m34;
    
    Mat4 r = Mat4((_mat.m22 * _mat.m33 - _mat.m23 * _mat.m32) * det_rcp,
                  (_mat.m13 * _mat.m32 - _mat.m12 * _mat.m33) * det_rcp,
                  (_mat.m12 * _mat.m23 - _mat.m13 * _mat.m22) * det_rcp, 0.0f,
                  (_mat.m23 * _mat.m31 - _mat.m21 * _mat.m33) * det_rcp,
                  (_mat.m11 * _mat.m33 - _mat.m13 * _mat.m31) * det_rcp,
                  (_mat.m13 * _mat.m21 - _mat.m11 * _mat.m23) * det_rcp, 0,
                  (_mat.m21 * _mat.m32 - _mat.m22 * _mat.m31) * det_rcp,
                  (_mat.m12 * _mat.m31 - _mat.m11 * _mat.m32) * det_rcp,
                  (_mat.m11 * _mat.m22 - _mat.m12 * _mat.m21) * det_rcp, 0, 0.0f,
                  0.0f, 0.0f, 1.0f);
    
    r.f[12] = -(tx * r.m11 + ty * r.m12 + tz * r.m13);
    r.f[13] = -(tx * r.m21 + ty * r.m22 + tz * r.m23);
    r.f[14] = -(tx * r.m31 + ty * r.m32 + tz * r.m33);
    return r;
}

Quat Mat4::ToQuat(const Mat4& m)
{
    float trace, r, rinv;
    Quat q;
    
    trace = m.m11 + m.m22 + m.m33;
    if (trace >= 0.0f) {
        r = M::Sqrt(1.0f + trace);
        rinv = 0.5f / r;
        
        q.x = rinv * (m.m32 - m.m23);
        q.y = rinv * (m.m13 - m.m31);
        q.z = rinv * (m.m21 - m.m12);
        q.w = r * 0.5f;
    } 
    else if (m.m11 >= m.m22 && m.m11 >= m.m33) {
        r = M::Sqrt(1.0f - m.m22 - m.m33 + m.m11);
        rinv = 0.5f / r;
        
        q.x = r * 0.5f;
        q.y = rinv * (m.m21 + m.m12);
        q.z = rinv * (m.m31 + m.m13);
        q.w = rinv * (m.m32 - m.m23);
    } 
    else if (m.m22 >= m.m33) {
        r = M::Sqrt(1.0f - m.m11 - m.m33 + m.m22);
        rinv = 0.5f / r;
        
        q.x = rinv * (m.m21 + m.m12);
        q.y = r * 0.5f;
        q.z = rinv * (m.m32 + m.m23);
        q.w = rinv * (m.m13 - m.m31);
    } 
    else {
        r = M::Sqrt(1.0f - m.m11 - m.m22 + m.m33);
        rinv = 0.5f / r;
        
        q.x = rinv * (m.m31 + m.m13);
        q.y = rinv * (m.m32 + m.m23);
        q.z = r * 0.5f;
        q.w = rinv * (m.m21 - m.m12);
    }
    
    return q;
}

Mat4 Mat4::FromQuat(Quat q)
{
    float norm = M::Sqrt(Quat::Dot(q, q));
    float s = norm > 0.0f ? (2.0f / norm) : 0.0f;
    
    float x = q.x;
    float y = q.y;
    float z = q.z;
    float w = q.w;
    
    float xx = s * x * x;
    float xy = s * x * y;
    float wx = s * w * x;
    float yy = s * y * y;
    float yz = s * y * z;
    float wy = s * w * y;
    float zz = s * z * z;
    float xz = s * x * z;
    float wz = s * w * z;
    
    return Mat4(1.0f - yy - zz,     xy - wz,            xz + wy,        0.0f,
                xy + wz,            1.0f - xx - zz,     yz - wx,        0.0f,
                xz - wy,            yz + wx,            1.0f - xx - yy, 0.0f,
                0.0f,               0.0f,               0.0f,           1.0f);
}

Mat4 Mat4::FromNormalAngle(Float3 _normal, float _scale, Float3 _pos, float _angle)
{
    Float3 tangent;
    Float3 bitangent;
    Float3::TangentAngle(&tangent, &bitangent, _normal, _angle);
    
    Float4 row1 = Float4(Float3::Mul(bitangent, _scale), 0.0f);
    Float4 row2 = Float4(Float3::Mul(_normal, _scale), 0.0f);
    Float4 row3 = Float4(Float3::Mul(tangent, _scale), 0.0f);
    
    return Mat4(row1.f, row2.f, row3.f, Float4(_pos, 1.0f).f);
}

Mat4 Mat4::ProjectPlane(Float3 planeNormal)
{
    float xx = planeNormal.x * planeNormal.x;
    float yy = planeNormal.y * planeNormal.y;
    float zz = planeNormal.z * planeNormal.z;
    float xy = planeNormal.x * planeNormal.y;
    float xz = planeNormal.x * planeNormal.z;
    float yz = planeNormal.y * planeNormal.z;
    
    return Mat4(1.0f - xx,      -xy,        -xz,        0.0f,
                -xy,            1.0f - yy,  -yz,        0.0f,
                -xz,            -yz,        1.0f - zz,  0.0f,
                0.0f,           0.0f,       0.0f,       1.0f);
}

Mat4 Mat4::Mul(const Mat4& _a, const Mat4& _b)
{
    return Mat4(
        Mat4::MulFloat4(_a, Float4(_b.fc1)).f, 
        Mat4::MulFloat4(_a, Float4(_b.fc2)).f,
        Mat4::MulFloat4(_a, Float4(_b.fc3)).f, 
        Mat4::MulFloat4(_a, Float4(_b.fc4)).f);
}

//    ███╗   ███╗ █████╗ ████████╗██████╗ 
//    ████╗ ████║██╔══██╗╚══██╔══╝╚════██╗
//    ██╔████╔██║███████║   ██║    █████╔╝
//    ██║╚██╔╝██║██╔══██║   ██║    ╚═══██╗
//    ██║ ╚═╝ ██║██║  ██║   ██║   ██████╔╝
//    ╚═╝     ╚═╝╚═╝  ╚═╝   ╚═╝   ╚═════╝ 
Mat3 Mat3::Inverse(const Mat3& _a)
{
    float xx = _a.f[0];
    float xy = _a.f[3];
    float xz = _a.f[6];
    float yx = _a.f[1];
    float yy = _a.f[4];
    float yz = _a.f[7];
    float zx = _a.f[2];
    float zy = _a.f[5];
    float zz = _a.f[8];
    
    float det = 0.0f;
    det += xx * (yy * zz - yz * zy);
    det -= xy * (yx * zz - yz * zx);
    det += xz * (yx * zy - yy * zx);
    
    float det_rcp = 1.0f / det;
    
    return Mat3(+(yy * zz - yz * zy) * det_rcp, -(xy * zz - xz * zy) * det_rcp,
        +(xy * yz - xz * yy) * det_rcp, -(yx * zz - yz * zx) * det_rcp,
        +(xx * zz - xz * zx) * det_rcp, -(xx * yz - xz * yx) * det_rcp,
        +(yx * zy - yy * zx) * det_rcp, -(xx * zy - xy * zx) * det_rcp,
        +(xx * yy - xy * yx) * det_rcp);
}

Mat3 Mat3::Mul(const Mat3& _a, const Mat3& _b)
{
    return Mat3(
        Mat3::MulFloat3(_a, Float3(_b.fc1)), 
        Mat3::MulFloat3(_a, Float3(_b.fc2)),
        Mat3::MulFloat3(_a, Float3(_b.fc3)));
}

Mat3 Mat3::Abs(const Mat3& m)
{
    return Mat3(
        M::Abs(m.m11), M::Abs(m.m12), M::Abs(m.m13), 
        M::Abs(m.m21), M::Abs(m.m22), M::Abs(m.m23), 
        M::Abs(m.m31), M::Abs(m.m32), M::Abs(m.m33));
}

Mat3 Mat3::FromQuat(Quat q)
{
    float norm = M::Sqrt(Quat::Dot(q, q));
    float s = norm > 0.0f ? (2.0f / norm) : 0.0f;
    
    float x = q.x;
    float y = q.y;
    float z = q.z;
    float w = q.w;
    
    float xx = s * x * x;
    float xy = s * x * y;
    float wx = s * w * x;
    float yy = s * y * y;
    float yz = s * y * z;
    float wy = s * w * y;
    float zz = s * z * z;
    float xz = s * x * z;
    float wz = s * w * z;
    
    return Mat3(1.0f - yy - zz,     xy - wz,            xz + wy,
                xy + wz,            1.0f - xx - zz,     yz - wx,
                xz - wy,            yz + wx,            1.0f - xx - yy);
}

//    ███████╗██╗      ██████╗  █████╗ ████████╗██████╗ 
//    ██╔════╝██║     ██╔═══██╗██╔══██╗╚══██╔══╝╚════██╗
//    █████╗  ██║     ██║   ██║███████║   ██║    █████╔╝
//    ██╔══╝  ██║     ██║   ██║██╔══██║   ██║   ██╔═══╝ 
//    ██║     ███████╗╚██████╔╝██║  ██║   ██║   ███████╗
//    ╚═╝     ╚══════╝ ╚═════╝ ╚═╝  ╚═╝   ╚═╝   ╚══════╝
Float2 float2CalcLinearFit2D(const Float2* _points, int _num)
{
    float sumX = 0.0f;
    float sumY = 0.0f;
    float sumXX = 0.0f;
    float sumXY = 0.0f;
    
    for (int ii = 0; ii < _num; ++ii) {
        float xx = _points[ii].f[0];
        float yy = _points[ii].f[1];
        sumX += xx;
        sumY += yy;
        sumXX += xx * xx;
        sumXY += xx * yy;
    }
    
    // [ sum(x^2) sum(x)    ] [ A ] = [ sum(x*y) ]
    // [ sum(x)   numPoints ] [ B ]   [ sum(y)   ]
    
    float det = (sumXX * _num - sumX * sumX);
    float invDet = 1.0f / det;
    
    return Float2((-sumX * sumY + _num * sumXY) * invDet, (sumXX * sumY - sumX * sumXY) * invDet);
}


//    ███████╗██╗      ██████╗  █████╗ ████████╗██████╗ 
//    ██╔════╝██║     ██╔═══██╗██╔══██╗╚══██╔══╝╚════██╗
//    █████╗  ██║     ██║   ██║███████║   ██║    █████╔╝
//    ██╔══╝  ██║     ██║   ██║██╔══██║   ██║    ╚═══██╗
//    ██║     ███████╗╚██████╔╝██║  ██║   ██║   ██████╔╝
//    ╚═╝     ╚══════╝ ╚═════╝ ╚═╝  ╚═╝   ╚═╝   ╚═════╝ 
Float3 Float3::CalcLinearFit3D(const Float3* _points, int _num)
{
    float sumX = 0.0f;
    float sumY = 0.0f;
    float sumZ = 0.0f;
    float sumXX = 0.0f;
    float sumXY = 0.0f;
    float sumXZ = 0.0f;
    float sumYY = 0.0f;
    float sumYZ = 0.0f;
    
    for (int ii = 0; ii < _num; ++ii) {
        float xx = _points[ii].f[0];
        float yy = _points[ii].f[1];
        float zz = _points[ii].f[2];
        
        sumX += xx;
        sumY += yy;
        sumZ += zz;
        sumXX += xx * xx;
        sumXY += xx * yy;
        sumXZ += xx * zz;
        sumYY += yy * yy;
        sumYZ += yy * zz;
    }
    
    // [ sum(x^2) sum(x*y) sum(x)    ] [ A ]   [ sum(x*z) ]
    // [ sum(x*y) sum(y^2) sum(y)    ] [ B ] = [ sum(y*z) ]
    // [ sum(x)   sum(y)   numPoints ] [ C ]   [ sum(z)   ]
    
    Mat3 mat(sumXX, sumXY, sumX, sumXY, sumYY, sumY, sumX, sumY, (float)(_num));
    Mat3 matInv = Mat3::Inverse(mat);
    
    return Float3(matInv.f[0] * sumXZ + matInv.f[1] * sumYZ + matInv.f[2] * sumZ,
                  matInv.f[3] * sumXZ + matInv.f[4] * sumYZ + matInv.f[5] * sumZ,
                  matInv.f[6] * sumXZ + matInv.f[7] * sumYZ + matInv.f[8] * sumZ);
}


//     ██████╗ ██████╗ ██╗      ██████╗ ██████╗ 
//    ██╔════╝██╔═══██╗██║     ██╔═══██╗██╔══██╗
//    ██║     ██║   ██║██║     ██║   ██║██████╔╝
//    ██║     ██║   ██║██║     ██║   ██║██╔══██╗
//    ╚██████╗╚██████╔╝███████╗╚██████╔╝██║  ██║
//     ╚═════╝ ╚═════╝ ╚══════╝ ╚═════╝ ╚═╝  ╚═╝
Float3 Color4u::RGBtoHSV(Float3 rgb)
{
    float K = 0.f;
    float r = rgb.f[0];
    float g = rgb.f[1];
    float b = rgb.f[2];
    
    if (g < b)
    {
        Swap(g, b);
        K = -1.f;
    }
    
    if (r < g)
    {
        Swap(r, g);
        K = -2.f / 6.f - K;
    }
    
    float chroma = r - Min(g, b);
    return Float3(M::Abs(K + (g - b) / (6.f * chroma + 1e-20f)),
                  chroma / (r + 1e-20f),
                  r);
}

Float3 Color4u::HSVtoRGB(Float3 hsv)
{
    const float hh = hsv.f[0];
    const float ss = hsv.f[1];
    const float vv = hsv.f[2];
    
    const float px = M::Abs(M::Fract(hh + 1.0f) * 6.0f - 3.0f);
    const float py = M::Abs(M::Fract(hh + 2.0f / 3.0f) * 6.0f - 3.0f);
    const float pz = M::Abs(M::Fract(hh + 1.0f / 3.0f) * 6.0f - 3.0f);
    
    return Float3(vv * M::Lerp(1.0f, M::Saturate(px - 1.0f), ss), 
                  vv * M::Lerp(1.0f, M::Saturate(py - 1.0f), ss),
                  vv * M::Lerp(1.0f, M::Saturate(pz - 1.0f), ss));
}

Color4u Color4u::Blend(Color4u _a, Color4u _b, float _t)
{
    Float4 c1 = Color4u::ToFloat4(_a);
    Float4 c2 = Color4u::ToFloat4(_b);
    
    return Color4u(
        M::Lerp(c1.x, c2.x, _t),
        M::Lerp(c1.y, c2.y, _t),
        M::Lerp(c1.z, c2.z, _t),
        M::Lerp(c1.w, c2.w, _t)
    );
}

// https://en.wikipedia.org/wiki/SRGB#Specification_of_the_transformation
Float4 Color4u::ToFloat4Linear(Float4 c)
{
    for (int i = 0; i < 3; i++) {
        c.f[i] = c.f[i] < 0.04045f ? c.f[i]/12.92f : M::Pow((c.f[i] + 0.055f)/1.055f, 2.4f);
    }
    return c;
}

Float4 Color4u::ToFloat4SRGB(Float4 cf) 
{
    for (int i = 0; i < 3; i++) {
        cf.f[i] = cf.f[i] <= 0.0031308 ? 
            (12.92f*cf.f[i]) : 
            1.055f*M::Pow(cf.f[i], 0.416666f) - 0.055f;
    }
    return cf;
}

//     ██████╗ ██╗   ██╗ █████╗ ████████╗
//    ██╔═══██╗██║   ██║██╔══██╗╚══██╔══╝
//    ██║   ██║██║   ██║███████║   ██║   
//    ██║▄▄ ██║██║   ██║██╔══██║   ██║   
//    ╚██████╔╝╚██████╔╝██║  ██║   ██║   
//     ╚══▀▀═╝  ╚═════╝ ╚═╝  ╚═╝   ╚═╝   
Quat Quat::Lerp(Quat _a, Quat _b, float t)
{
    float tinv = 1.0f - t;
    float dot = Quat::Dot(_a, _b);
    Quat r;
    if (dot >= 0.0f) {
        r = Quat(tinv * _a.x + t * _b.x, 
                 tinv * _a.y + t * _b.y, 
                 tinv * _a.z + t * _b.z, 
                 tinv * _a.w + t * _b.w);
    } else {
        r = Quat(tinv * _a.x - t * _b.x, 
                 tinv * _a.y - t * _b.y, 
                 tinv * _a.z - t * _b.z, 
                 tinv * _a.w - t * _b.w);
    }
    return Quat::Norm(r);
}

Quat Quat::Slerp(Quat _a, Quat _b, float t)
{
    const float epsilon = 1e-6f;
    
    float dot = Quat::Dot(_a, _b);
    bool flip = false;
    if (dot < 0.0f) {
        flip = true;
        dot *= -1.0f;
    }
    
    float s1, s2;
    if (dot > (1.0f - epsilon)) {
        s1 = 1.0f - t;
        s2 = t;
        if (flip)
            s2 *= -1.0f;
    } else {
        float omega = M::ACos(dot);
        float inv_omega_sin = 1.0f / M::Sin(omega);
        s1 = M::Sin((1.0f - t) * omega) * inv_omega_sin;
        s2 = M::Sin(t * omega) * inv_omega_sin;
        if (flip)
            s2 *= -1.0f;
    }

    return Quat(s1 * _a.x + s2 * _b.x, 
                s1 * _a.y + s2 * _b.y, 
                s1 * _a.z + s2 * _b.z,
                s1 * _a.w + s2 * _b.w);
}

Float3 Quat::ToEuler(Quat q)
{
    float sinr_cosp = 2 * (q.w * q.x + q.y * q.z);
    float cosr_cosp = 1 - 2 * (q.x * q.x + q.y * q.y);
    float x = M::ATan2(sinr_cosp, cosr_cosp);
    
    float sinp = 2 * (q.w * q.y - q.z * q.x);
    float y;
    if (M::Abs(sinp) >= 1)
        y = M::CopySign(M_HALFPI, sinp);
    else
        y = M::ASin(sinp);
    
    float siny_cosp = 2 * (q.w * q.z + q.x * q.y);
    float cosy_cosp = 1 - 2 * (q.y * q.y + q.z * q.z);
    float z = M::ATan2(siny_cosp, cosy_cosp);
    
    return Float3(x, y, z);
}

Quat Quat::FromEuler(Float3 _vec3)
{
    float z = _vec3.z;
    float x = _vec3.x;
    float y = _vec3.y;
    
    float cy = M::Cos(z * 0.5f);
    float sy = M::Sin(z * 0.5f);
    float cp = M::Cos(y * 0.5f);
    float sp = M::Sin(y * 0.5f);
    float cr = M::Cos(x * 0.5f);
    float sr = M::Sin(x * 0.5f);
    
    Quat q;
    q.w = cr * cp * cy + sr * sp * sy;
    q.x = sr * cp * cy - cr * sp * sy;
    q.y = cr * sp * cy + sr * cp * sy;
    q.z = cr * cp * sy - sr * sp * cy;
    
    return q;
}


//    ██████╗ ██╗      █████╗ ███╗   ██╗███████╗
//    ██╔══██╗██║     ██╔══██╗████╗  ██║██╔════╝
//    ██████╔╝██║     ███████║██╔██╗ ██║█████╗  
//    ██╔═══╝ ██║     ██╔══██║██║╚██╗██║██╔══╝  
//    ██║     ███████╗██║  ██║██║ ╚████║███████╗
//    ╚═╝     ╚══════╝╚═╝  ╚═╝╚═╝  ╚═══╝╚══════╝
Float3 Plane::CalcNormal(Float3 _va, Float3 _vb, Float3 _vc)
{
    Float3 ba = Float3::Sub(_vb, _va);
    Float3 ca = Float3::Sub(_vc, _va);
    Float3 baca = Float3::Cross(ca, ba);
    
    return Float3::Norm(baca);
}

Plane Plane::From3Points(Float3 _va, Float3 _vb, Float3 _vc)
{
    Float3 normal = Plane::CalcNormal(_va, _vb, _vc);
    return Plane(normal, -Float3::Dot(normal, _va));
}

Plane Plane::FromNormalPoint(Float3 _normal, Float3 _p)
{
    Float3 normal = Float3::Norm(_normal);
    float d = Float3::Dot(_normal, _p);
    return Plane(normal, -d);
}

float Plane::Distance(Plane _plane, Float3 _p)
{
    return Float3::Dot(Float3(_plane.normal), _p) + _plane.dist;
}

Float3 Plane::ProjectPoint(Plane _plane, Float3 _p)
{
    return Float3::Sub(_p, Float3::Mul(Float3(_plane.normal), Distance(_plane, _p)));
}

Float3 Plane::Origin(Plane _plane)
{
    return Float3::Mul(Float3(_plane.normal), -_plane.dist);
}

//     █████╗  █████╗ ██████╗ ██████╗ 
//    ██╔══██╗██╔══██╗██╔══██╗██╔══██╗
//    ███████║███████║██████╔╝██████╔╝
//    ██╔══██║██╔══██║██╔══██╗██╔══██╗
//    ██║  ██║██║  ██║██████╔╝██████╔╝
//    ╚═╝  ╚═╝╚═╝  ╚═╝╚═════╝ ╚═════╝ 
// https://zeux.io/2010/10/17/aabb-from-obb-with-component-wise-abs/
AABB AABB::Transform(const AABB& aabb, const Mat4& mat)
{
    Float3 center = aabb.Center();
    Float3 extents = aabb.Extents();
    
    Mat3 rotMat = Mat3(mat.fc1, mat.fc2, mat.fc3);
    Mat3 absMat  = Mat3::Abs(rotMat);
    Float3 newCenter = Mat4::MulFloat3(mat, center);
    Float3 newExtents = Mat3::MulFloat3(absMat, extents);
    
    return AABB(Float3::Sub(newCenter, newExtents), Float3::Add(newCenter, newExtents));
}

AABB Box::ToAABB(const Box& box)
{
    Float3 center = box.tx.pos;
    Mat3 absMat = Mat3::Abs(box.tx.rot);
    Float3 extents = Mat3::MulFloat3(absMat, box.e);
    return AABB(Float3::Sub(center, extents), Float3::Add(center, extents));
}


