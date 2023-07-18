#include "MathAll.h"

#include <math.h>

float mathCopySign(float _x, float _y)
{
    return ::copysignf(_x, _y);
}

float mathFloor(float _f)
{
    return ::floorf(_f);
}

float mathCos(float _a)
{
    return ::cosf(_a);
}

float mathACos(float _a)
{
    return ::acosf(_a);
}

float mathSin(float _a)
{
    return ::sinf(_a);
}

float mathASin(float _a)
{
    return ::asinf(_a);
}

float mathATan2(float _y, float _x)
{
    return ::atan2f(_y, _x);
}

float mathExp(float _a)
{
    return ::expf(_a);
}

float mathLog(float _a)
{
    return ::logf(_a);
}

#if !(defined(__SSE2__) || (COMPILER_MSVC && (ARCH_64BIT || _M_IX86_FP >= 2)))
    float mathSqrt(float _a)
    {
        return ::sqrtf(_a);
    }

    float mathRsqrt(float _a)
    {
        return 1.0f / ::sqrtf(_a);
    }
#endif // if not __SSE2__

Mat4 mat4ViewLookAt(Float3 eye, Float3 target, Float3 up)
{
    Float3 zaxis = float3Norm(float3Sub(target, eye));
    Float3 xaxis = float3Norm(float3Cross(zaxis, up));
    Float3 yaxis = float3Cross(xaxis, zaxis);
    
    return Mat4(xaxis.x,    xaxis.y,    xaxis.z,    -float3Dot(xaxis, eye), 
                yaxis.x,    yaxis.y,    yaxis.z,    -float3Dot(yaxis, eye), 
                -zaxis.x,   -zaxis.y,   -zaxis.z,    float3Dot(zaxis, eye),
                0,          0,          0,           1.0f);
}

Mat4 mat4ViewLookAtLH(Float3 eye, Float3 target, Float3 up)
{
    Float3 zaxis = float3Norm(float3Sub(target, eye));
    Float3 xaxis = float3Norm(float3Cross(up, zaxis));
    Float3 yaxis = float3Cross(zaxis, xaxis);
    
    return Mat4(xaxis.x, xaxis.y, xaxis.z, -float3Dot(xaxis, eye), 
                yaxis.x, yaxis.y, yaxis.z, -float3Dot(yaxis, eye), 
                zaxis.x, zaxis.y, zaxis.z, -float3Dot(zaxis, eye),
                0,       0,       0,        1.0f);
}

Mat4 mat4ViewFPS(Float3 eye, float pitch, float yaw)
{
    float cos_pitch = mathCos(pitch);
    float sin_pitch = mathSin(pitch);
    float cos_yaw = mathCos(yaw);
    float sin_yaw = mathSin(yaw);
    
    Float3 xaxis = Float3(cos_yaw, 0, -sin_yaw);
    Float3 yaxis = Float3(sin_yaw * sin_pitch, cos_pitch, cos_yaw * sin_pitch);
    Float3 zaxis = Float3(sin_yaw * cos_pitch, -sin_pitch, cos_pitch * cos_yaw);
    
    return Mat4(xaxis.x, xaxis.y, xaxis.z, -float3Dot(xaxis, eye), yaxis.x, yaxis.y, yaxis.z,
                -float3Dot(yaxis, eye), zaxis.x, zaxis.y, zaxis.z, -float3Dot(zaxis, eye),
                0, 0, 0, 1.0f);
}

Mat4 mat4ViewArcBall(Float3 move, Quat rot, Float3 target_pos)
{
    // CameraMat = Tobj * Rcam * Tcam;      // move -> rotate around pivot pt -> move to object pos
    // ViewMat = CameraMat(inv) = Tobj(inv) * Rcam(inv) * Tobj(inv)
    Mat4 translateInv = mat4Translate(-move.x, -move.y, -move.z);
    Mat4 rotateInv = quatToMat4(quatInverse(rot));
    Mat4 translateObjInv = mat4Translate(-target_pos.x, -target_pos.y, -target_pos.z);
    Mat4 TR = mat4Mul(translateObjInv, rotateInv);
    return mat4Mul(TR, translateInv);
}


// Vulkan NDC:(-1, -1)=top-left
// D3D NDC:(-1, 1)=top-left
Mat4 mat4Perspective(float width, float height, float zn, float zf, bool d3dNdc)
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

Mat4 mat4PerspectiveLH(float width, float height, float zn, float zf, bool d3dNdc)
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

Mat4 mat4PerspectiveOffCenter(float xmin, float ymin, float xmax, float ymax, float zn, float zf, bool d3dNdc)
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

Mat4 mat4PerspectiveOffCenterLH(float xmin, float ymin, float xmax, float ymax, float zn, float zf, bool d3dNdc)
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

Mat4 mat4PerspectiveFOV(float fov_y, float aspect, float zn, float zf, bool d3dNdc)
{
    const float height = 1.0f / mathTan(fov_y * 0.5f);
    const float width = height / aspect;
    return mat4Perspective(width, height, zn, zf, d3dNdc);
}

Mat4 mat4PerspectiveFOVLH(float fov_y, float aspect, float zn, float zf, bool d3dNdc)
{
    const float height = 1.0f / mathTan(fov_y * 0.5f);
    const float width = height / aspect;
    return mat4PerspectiveLH(width, height, zn, zf, d3dNdc);
}

Mat4 mat4Ortho(float width, float height, float zn, float zf, float offset, bool d3dNdc)
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

Mat4 mat4OrthoLH(float width, float height, float zn, float zf, float offset, bool d3dNdc)
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

Mat4 mat4OrthoOffCenter(float xmin, float ymin, float xmax, float ymax, float zn, float zf, float offset, bool d3dNdc)
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

Mat4 mat4OrthoOffCenterLH(float xmin, float ymin, float xmax, float ymax, float zn, float zf, float offset, bool d3dNdc)
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

Mat4 mat4ScaleRotateTranslate(float _sx, float _sy, float _sz, float _ax, float _ay, float _az, float _tx, float _ty, float _tz)
{
    float sx, cx, sy, cy, sz, cz;
    
    if (_ax != 0) {
        sx = mathSin(_ax);
        cx = mathCos(_ax);
    } else {
        sx = 0;
        cx = 1.0f;
    }
    
    if (_ay != 0) {
        sy = mathSin(_ay);
        cy = mathCos(_ay);
    } else {
        sy = 0;
        cy = 1.0f;
    }
    
    if (_az != 0) {
        sz = mathSin(_az);
        cz = mathCos(_az);
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

Mat3 mat3Inverse(const Mat3& _a)
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

Mat4 mat4Inverse(const Mat4& _a)
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

Float3 float3CalcLinearFit3D(const Float3* _points, int _num)
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
    Mat3 matInv = mat3Inverse(mat);
    
    return Float3(matInv.f[0] * sumXZ + matInv.f[1] * sumYZ + matInv.f[2] * sumZ,
                  matInv.f[3] * sumXZ + matInv.f[4] * sumYZ + matInv.f[5] * sumZ,
                  matInv.f[6] * sumXZ + matInv.f[7] * sumYZ + matInv.f[8] * sumZ);
}

void colorRGBtoHSV(float _hsv[3], const float _rgb[3])
{
    float K = 0.f;
    float r = _rgb[0];
    float g = _rgb[1];
    float b = _rgb[2];
    
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
    _hsv[0] = mathAbs(K + (g - b) / (6.f * chroma + 1e-20f));
    _hsv[1] = chroma / (r + 1e-20f);
    _hsv[2] = r;
}

void colorRGBToHSV(float _rgb[3], const float _hsv[3])
{
    const float hh = _hsv[0];
    const float ss = _hsv[1];
    const float vv = _hsv[2];
    
    const float px = mathAbs(mathFract(hh + 1.0f) * 6.0f - 3.0f);
    const float py = mathAbs(mathFract(hh + 2.0f / 3.0f) * 6.0f - 3.0f);
    const float pz = mathAbs(mathFract(hh + 1.0f / 3.0f) * 6.0f - 3.0f);
    
    _rgb[0] = vv * mathLerp(1.0f, mathSaturate(px - 1.0f), ss);
    _rgb[1] = vv * mathLerp(1.0f, mathSaturate(py - 1.0f), ss);
    _rgb[2] = vv * mathLerp(1.0f, mathSaturate(pz - 1.0f), ss);
}

Color colorBlend(Color _a, Color _b, float _t)
{
    Float4 c1 = colorToFloat4(_a);
    Float4 c2 = colorToFloat4(_b);
    
    return Color(
        mathLerp(c1.x, c2.x, _t),
        mathLerp(c1.y, c2.y, _t),
        mathLerp(c1.z, c2.z, _t),
        mathLerp(c1.w, c2.w, _t)
    );
}

// https://en.wikipedia.org/wiki/SRGB#Specification_of_the_transformation
Float4 colorToFloat4Linear(Float4 c)
{
    for (int i = 0; i < 3; i++) {
        c.f[i] = c.f[i] < 0.04045f ? c.f[i]/12.92f : mathPow((c.f[i] + 0.055f)/1.055f, 2.4f);
    }
    return c;
}

Float4 colorToFloat4SRGB(Float4 cf) 
{
    for (int i = 0; i < 3; i++) {
        cf.f[i] = cf.f[i] <= 0.0031308 ? 
            (12.92f*cf.f[i]) : 
            1.055f*mathPow(cf.f[i], 0.416666f) - 0.055f;
    }
    return cf;
}

Mat3 mat3Mul(const Mat3& _a, const Mat3& _b)
{
    return Mat3(
        mat3MulFloat3(_a, Float3(_b.fc1)), 
        mat3MulFloat3(_a, Float3(_b.fc2)),
        mat3MulFloat3(_a, Float3(_b.fc3)));
}

Quat mat4ToQuat(const Mat4& m)
{
    float trace, r, rinv;
    Quat q;
    
    trace = m.m11 + m.m22 + m.m33;
    if (trace >= 0.0f) {
        r = mathSqrt(1.0f + trace);
        rinv = 0.5f / r;
        
        q.x = rinv * (m.m32 - m.m23);
        q.y = rinv * (m.m13 - m.m31);
        q.z = rinv * (m.m21 - m.m12);
        q.w = r * 0.5f;
    } 
    else if (m.m11 >= m.m22 && m.m11 >= m.m33) {
        r = mathSqrt(1.0f - m.m22 - m.m33 + m.m11);
        rinv = 0.5f / r;
        
        q.x = r * 0.5f;
        q.y = rinv * (m.m21 + m.m12);
        q.z = rinv * (m.m31 + m.m13);
        q.w = rinv * (m.m32 - m.m23);
    } 
    else if (m.m22 >= m.m33) {
        r = mathSqrt(1.0f - m.m11 - m.m33 + m.m22);
        rinv = 0.5f / r;
        
        q.x = rinv * (m.m21 + m.m12);
        q.y = r * 0.5f;
        q.z = rinv * (m.m32 + m.m23);
        q.w = rinv * (m.m13 - m.m31);
    } 
    else {
        r = mathSqrt(1.0f - m.m11 - m.m22 + m.m33);
        rinv = 0.5f / r;
        
        q.x = rinv * (m.m31 + m.m13);
        q.y = rinv * (m.m32 + m.m23);
        q.z = r * 0.5f;
        q.w = rinv * (m.m21 - m.m12);
    }
    
    return q;
}

Mat4 mat3InverseTransform(const Mat4& _mat)
{
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

Mat4 mat4FromNormal(Float3 _normal, float _scale, Float3 _pos)
{
    Float3 tangent;
    Float3 bitangent;
    float3Tangent(&tangent, &bitangent, _normal);
    
    Float4 row1 = Float4(float3Mulf(bitangent, _scale), 0.0f);
    Float4 row2 = Float4(float3Mulf(_normal, _scale), 0.0f);
    Float4 row3 = Float4(float3Mulf(tangent, _scale), 0.0f);
    
    return Mat4(row1.f, row2.f, row3.f, Float4(_pos, 1.0f).f);
}

Mat4 sx_mat4_from_normal_angle(Float3 _normal, float _scale, Float3 _pos,
    float _angle)
{
    Float3 tangent;
    Float3 bitangent;
    float3TangentAngle(&tangent, &bitangent, _normal, _angle);
    
    Float4 row1 = Float4(float3Mulf(bitangent, _scale), 0.0f);
    Float4 row2 = Float4(float3Mulf(_normal, _scale), 0.0f);
    Float4 row3 = Float4(float3Mulf(tangent, _scale), 0.0f);
    
    return Mat4(row1.f, row2.f, row3.f, Float4(_pos, 1.0f).f);
}

Mat4 mat4ProjectPlane(Float3 planeNormal)
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

Mat3 quatToMat3(Quat quat)
{
    float norm = mathSqrt(quatDot(quat, quat));
    float s = norm > 0.0f ? (2.0f / norm) : 0.0f;
    
    float x = quat.x;
    float y = quat.y;
    float z = quat.z;
    float w = quat.w;
    
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

Mat4 quatToMat4(Quat quat)
{
    float norm = mathSqrt(quatDot(quat, quat));
    float s = norm > 0.0f ? (2.0f / norm) : 0.0f;
    
    float x = quat.x;
    float y = quat.y;
    float z = quat.z;
    float w = quat.w;
    
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

Quat quatLerp(Quat _a, Quat _b, float t)
{
    float tinv = 1.0f - t;
    float dot = quatDot(_a, _b);
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
    return quatNorm(r);
}

Quat quatSlerp(Quat _a, Quat _b, float t)
{
    const float epsilon = 1e-6f;
    
    float dot = quatDot(_a, _b);
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
        float omega = mathACos(dot);
        float inv_omega_sin = 1.0f / mathSin(omega);
        s1 = mathSin((1.0f - t) * omega) * inv_omega_sin;
        s2 = mathSin(t * omega) * inv_omega_sin;
        if (flip)
            s2 *= -1.0f;
    }

    return Quat(s1 * _a.x + s2 * _b.x, 
                s1 * _a.y + s2 * _b.y, 
                s1 * _a.z + s2 * _b.z,
                s1 * _a.w + s2 * _b.w);
}

Float3 quatToEuler(Quat _quat)
{
    float sinr_cosp = 2 * (_quat.w * _quat.x + _quat.y * _quat.z);
    float cosr_cosp = 1 - 2 * (_quat.x * _quat.x + _quat.y * _quat.y);
    float x = mathATan2(sinr_cosp, cosr_cosp);
    
    float sinp = 2 * (_quat.w * _quat.y - _quat.z * _quat.x);
    float y;
    if (mathAbs(sinp) >= 1)
        y = mathCopySign(kPIHalf, sinp);
    else
        y = mathASin(sinp);
    
    float siny_cosp = 2 * (_quat.w * _quat.z + _quat.x * _quat.y);
    float cosy_cosp = 1 - 2 * (_quat.y * _quat.y + _quat.z * _quat.z);
    float z = mathATan2(siny_cosp, cosy_cosp);
    
    return Float3(x, y, z);
}

Quat quatFromEuler(Float3 _vec3)
{
    float z = _vec3.z;
    float x = _vec3.x;
    float y = _vec3.y;
    
    float cy = mathCos(z * 0.5f);
    float sy = mathSin(z * 0.5f);
    float cp = mathCos(y * 0.5f);
    float sp = mathSin(y * 0.5f);
    float cr = mathCos(x * 0.5f);
    float sr = mathSin(x * 0.5f);
    
    Quat q;
    q.w = cr * cp * cy + sr * sp * sy;
    q.x = sr * cp * cy - cr * sp * sy;
    q.y = cr * sp * cy + sr * cp * sy;
    q.z = cr * cp * sy - sr * sp * cy;
    
    return q;
}

Mat4 mat4Mul(const Mat4& _a, const Mat4& _b)
{
    return Mat4(
        mat4MulFloat4(_a, Float4(_b.fc1)).f, 
        mat4MulFloat4(_a, Float4(_b.fc2)).f,
        mat4MulFloat4(_a, Float4(_b.fc3)).f, 
        mat4MulFloat4(_a, Float4(_b.fc4)).f);
}

Float3 planeNormal(Float3 _va, Float3 _vb, Float3 _vc)
{
    Float3 ba = float3Sub(_vb, _va);
    Float3 ca = float3Sub(_vc, _va);
    Float3 baca = float3Cross(ca, ba);
    
    return float3Norm(baca);
}

Plane plane3Points(Float3 _va, Float3 _vb, Float3 _vc)
{
    Float3 normal = planeNormal(_va, _vb, _vc);
    return Plane(normal, -float3Dot(normal, _va));
}

Plane planeNormalPoint(Float3 _normal, Float3 _p)
{
    Float3 normal = float3Norm(_normal);
    float d = float3Dot(_normal, _p);
    return Plane(normal, -d);
}

float planeDistance(Plane _plane, Float3 _p)
{
    return float3Dot(Float3(_plane.normal), _p) + _plane.dist;
}

Float3 planeProjectPoint(Plane _plane, Float3 _p)
{
    return float3Sub(_p, float3Mulf(Float3(_plane.normal), planeDistance(_plane, _p)));
}

Float3 planeOrigin(Plane _plane)
{
    return float3Mulf(Float3(_plane.normal), -_plane.dist);
}

INLINE Mat3 mat3Abs(const Mat3& m)
{
    return Mat3(
        mathAbs(m.m11), mathAbs(m.m12), mathAbs(m.m13), 
        mathAbs(m.m21), mathAbs(m.m22), mathAbs(m.m23), 
        mathAbs(m.m31), mathAbs(m.m32), mathAbs(m.m33));
}

AABB AABBFromBox(const Box* box)
{
    Float3 center = box->tx.pos;
    Mat3 absMat = mat3Abs(box->tx.rot);
    Float3 extents = mat3MulFloat3(absMat, box->e);
    return AABB(float3Sub(center, extents), float3Add(center, extents));
}

// https://zeux.io/2010/10/17/aabb-from-obb-with-component-wise-abs/
AABB AABBTransform(const AABB& aabb, const Mat4& mat)
{
    Float3 center = AABBCenter(aabb);
    Float3 extents = AABBExtents(aabb);
    
    Mat3 rotMat = Mat3(mat.fc1, mat.fc2, mat.fc3);
    Mat3 absMat  = mat3Abs(rotMat);
    Float3 newCenter = mat4MulFloat3(mat, center);
    Float3 newExtents = mat3MulFloat3(absMat, extents);
    
    return AABB(float3Sub(newCenter, newExtents), float3Add(newCenter, newExtents));
}
