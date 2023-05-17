#include "Camera.h"

#include "Core/MathVector.h"

#include "Application.h"

#ifdef near
    #undef near
#endif

#ifdef far
    #undef far
#endif

Camera::Camera(float fovDeg, float fnear, float ffar) :
    _near(fnear),
    _far(ffar),
    _fov(mathToRad(fovDeg))
{
    ASSERT(_far > _near);
}

void Camera::Setup(float fovDeg, float fnear, float ffar)
{
    ASSERT(ffar > fnear);

    _fov = mathToRad(fovDeg);
    _near = fnear;
    _far = ffar;
}

Mat4 Camera::GetOrthoMat(float viewWidth, float viewHeight) const
{
    return mat4Ortho(viewWidth, viewHeight, _near, _far);
}

Mat4 Camera::GetPerspectiveMat(float viewWidth, float viewHeight) const
{
    return mat4PerspectiveFOV(_fov, viewWidth/viewHeight, _near, _far);
}

Mat4 Camera::GetViewMat() const
{
    Float3 zaxis = _forward;
    Float3 xaxis = _right;    // norm(cross(zaxis, up));
    Float3 yaxis = _up;       // cross(xaxis, zaxis);

    return Mat4(xaxis.x,   xaxis.y,    xaxis.z,    -float3Dot(xaxis, _pos), 
                yaxis.x,   yaxis.y,    yaxis.z,    -float3Dot(yaxis, _pos), 
                -zaxis.x, -zaxis.y,    -zaxis.z,    float3Dot(zaxis, _pos), 
                0.0f,      0.0f,       0.0f,        1.0f);
}

void Camera::SetViewMat(const Mat4& viewMat)
{
    Mat4 vpInv = mat4Inverse(viewMat);
    _right = Float3(vpInv.fc1);
    _up = Float3(vpInv.fc2);
    _forward = Float3(vpInv.fc3) * -1.0f;
    _pos = Float3(vpInv.fc4);
}

CameraFrustumPoints Camera::GetFrustumPoints(float viewWidth, float viewHeight, float overrideNear, float overrideFar) const
{
    CameraFrustumPoints frustum;

    float near = overrideNear >= 0 ? overrideNear : _near;
    float far = overrideFar >= 0 ? overrideFar : _far;
    ASSERT(far > near);

    const float fov = _fov;
    const float aspect = viewWidth / viewHeight;

    Float3 xaxis = _right;
    Float3 yaxis = _up;
    Float3 zaxis = _forward;
    Float3 pos = _pos;

    float nearPlaneH = mathTan(fov*0.5f) * near;
    float nearPlaneW = nearPlaneH * aspect;

    float farPlaneH = mathTan(fov*0.5f) * far;
    float farPlaneW = farPlaneH * aspect;

    Float3 centerNear = zaxis*near + pos;
    Float3 centerFar = zaxis*far + pos;

    // scaled axises
    Float3 xnearScaled = xaxis * nearPlaneW;
    Float3 xfarScaled = xaxis * farPlaneW;
    Float3 ynearScaled = yaxis * nearPlaneH;
    Float3 yfarScaled = yaxis * farPlaneH;

    // near quad (normal inwards)
    frustum[0] = centerNear - (xnearScaled + ynearScaled);
    frustum[1] = centerNear + (xnearScaled - ynearScaled);
    frustum[2] = centerNear + (xnearScaled + ynearScaled);
    frustum[3] = centerNear - (xnearScaled - ynearScaled);

    // far quad (normal inwards)
    frustum[4] = centerFar - (xfarScaled + yfarScaled);
    frustum[5] = centerFar - (xfarScaled - yfarScaled);
    frustum[6] = centerFar + (xfarScaled + yfarScaled);
    frustum[7] = centerFar + (xfarScaled - yfarScaled);
    
    return frustum;
}

CameraFrustumPlanes Camera::GetFrustumPlanes(const Mat4& viewProjMat) const
{
    UNUSED(viewProjMat);
    return {};
}

void Camera::SetLookAt(Float3 pos, Float3 target, Float3 up)
{
    // TODO: figure UP vector out, I hacked up vector (in the matrix) to make it work correctly
    _forward = float3Norm(target - pos);
    _right = float3Norm(float3Cross(_forward, up));
    _up = float3Cross(_right, _forward);
    _pos = pos;
}

void Camera::HandleMovementKeyboard(float, float, float)
{
}

void Camera::HandleRotationMouse(const AppEvent&, float, float)
{
}

void CameraFPS::SetLookAt(Float3 pos, Float3 target, Float3 up)
{
    Camera::SetLookAt(pos, target, up);

    Mat4 m = Mat4(Float4(_right.x,     _right.y,       _right.z,   0),
                  Float4(-_up.x,      -_up.y,         -_up.z,      0),
                  Float4(_forward.x,   _forward.y,     _forward.z, 0), 
                  Float4(0,            0,              0,          1.0f));
    _quat = mat4ToQuat(m);

    Float3 euler = quatToEuler(_quat);
    _pitch = euler.x;
    _yaw = euler.z;
}

void CameraFPS::SetViewMat(const Mat4& viewMat)
{
    Camera::SetViewMat(viewMat);

    Mat4 m = Mat4(Float4(_right.x,     _right.y,       _right.z,   0),
                  Float4(-_up.x,      -_up.y,         -_up.z,      0),
                  Float4(_forward.x,   _forward.y,     _forward.z, 0), 
                  Float4(0,            0,              0,          1.0f));
    _quat = mat4ToQuat(m);

    Float3 euler = quatToEuler(_quat);
    _pitch = euler.x;
    _yaw = euler.z;
}

void CameraFPS::UpdateRotation()
{
    Mat4 m = quatToMat4(_quat);
    _right = Float3(m.fc1);
    _up = Float3(m.fc2)*-1.0f;
    _forward = Float3(m.fc3);
}

void CameraFPS::RotatePitch(float pitch, float pitchMin, float pitchMax)
{
    _pitch = Clamp(_pitch - pitch, pitchMin, pitchMax);
    _quat = quatRotateZ(_yaw) * quatRotateX(_pitch);
    UpdateRotation();
}

void CameraFPS::RotateYaw(float yaw)
{
    _yaw -= yaw;
    _quat = quatRotateZ(_yaw) * quatRotateX(_pitch);
    UpdateRotation();
}

void CameraFPS::MoveForward(float forward)
{
    _pos = _pos + _forward*forward;
}

void CameraFPS::Strafe(float strafe)
{
    _pos = _pos + _right*strafe;
}

void CameraFPS::HandleMovementKeyboard(float dt, float moveSpeed, float slowMoveSpeed)
{
    if (appIsKeyDown(AppKeycode::LeftShift) || appIsKeyDown(AppKeycode::RightShift))
        moveSpeed = slowMoveSpeed;
    
    _speedTime += dt;
    float t = mathLinearStep(_speedTime, 0, 0.03f);
    float speed = _keyDown ? mathBias(t, 0.9f)*moveSpeed : (1.0f - t)*moveSpeed;

    if (appIsKeyDown(AppKeycode::A) || appIsKeyDown(AppKeycode::Left)) 
        _moveStrafe -= speed*dt;        
    if (appIsKeyDown(AppKeycode::D) || appIsKeyDown(AppKeycode::Right))
        _moveStrafe += speed*dt;
    if (appIsKeyDown(AppKeycode::W) || appIsKeyDown(AppKeycode::Up))
        _moveFwd += speed*dt;
    if (appIsKeyDown(AppKeycode::S) || appIsKeyDown(AppKeycode::Down))
        _moveFwd -= speed*dt;

    // speed reaches zero, so reset movement variables
    if (mathIsEqual(speed, 0))
        _moveStrafe = _moveFwd = 0;
    _moveStrafe = Clamp(_moveStrafe, -moveSpeed*dt, moveSpeed*dt);
    _moveFwd = Clamp(_moveFwd, -moveSpeed*dt, moveSpeed*dt);

    Strafe(_moveStrafe);
    MoveForward(_moveFwd);
}

void CameraFPS::HandleRotationMouse(const AppEvent& ev, float rotateSpeed, float zoomStep)
{
    UNUSED(zoomStep);

    const AppKeycode moveKeys[] = {
        AppKeycode::W, AppKeycode::A, AppKeycode::S, AppKeycode::D,
        AppKeycode::Up, AppKeycode::Left, AppKeycode::Down, AppKeycode::Right
    };

    AppMouseButton activeButton = AppMouseButton::Right;
    if constexpr (PLATFORM_ANDROID)
        activeButton = AppMouseButton::Left;

    switch (ev.type) {
    case AppEventType::MouseDown:
        if (ev.mouseButton == activeButton) {
            if (!_mouseDown)
                appCaptureMouse();
            _mouseDown = true;
            _lastMouse = Float2(ev.mouseX, ev.mouseY);
        }
        break;
    case AppEventType::MouseUp:
        if (_mouseDown)
            appReleaseMouse();
    case AppEventType::MouseLeave:
        _mouseDown = false;
        break;
    case AppEventType::MouseMove:
        if (_mouseDown) {
            float dx = mathToRad(ev.mouseX - _lastMouse.x) * rotateSpeed;
            float dy = mathToRad(ev.mouseY - _lastMouse.y) * rotateSpeed;
            _lastMouse = Float2(ev.mouseX, ev.mouseY);
            RotatePitch(dy);
            RotateYaw(dx);
        }
        break;
    case AppEventType::KeyDown:
        if (!_keyDown) {
            _speedTime = 0;
            _keyDown = true;
        }
        break;

    case AppEventType::KeyUp:
        if (_keyDown && !appIsAnyKeysDown(moveKeys, CountOf(moveKeys))) {
            _speedTime = 0;
            _keyDown = false;
        }
        break;
    default:
        
        break;
    }
}

void CameraOrbit::SetLookAt(Float3 pos, Float3 target, Float3 up) 
{
    UNUSED(up);     // Maybe we should also keep `up` and pass it on ?

    _target = target;
    Float3 look = target - pos;

    _distance = float3Len(look);
    _orbit = -mathACos(float2Dot(float2Norm(Float2(look.f)*-1.0f), Float2(1.0f, 0)));

    float a = mathACos(float3Dot(float3Norm(look), mathIsEqual(look.z, 0, 0.00001f) ? kFloat3Zero : float3Norm(Float3(0, 0, look.z))));
    _elevation = kPIHalf - a;
    if (_elevation < 0)
        _elevation = -_elevation;
    ASSERT(_elevation >= 0 && _elevation <= kPIHalf);

    RotateOrbit(0);
}

void CameraOrbit::SetViewMat(const Mat4& viewMat)
{
    Camera::SetViewMat(viewMat);
}

void CameraOrbit::RotateOrbit(float orbit)
{
    _orbit += orbit;

    float x = _distance * mathCos(_orbit);
    float y = _distance * mathSin(_orbit);
    float z = _distance * mathCos(kPIHalf - _elevation);

    Camera::SetLookAt(Float3(x, y, z), _target);
}

void CameraOrbit::HandleRotationMouse(const AppEvent& ev, float rotateSpeed, float zoomStep)
{
    ASSERT(zoomStep > 0);
    ASSERT(rotateSpeed > 0);

    AppMouseButton activeButton = AppMouseButton::Right;
    if constexpr (PLATFORM_ANDROID)
        activeButton = AppMouseButton::Left;

    switch (ev.type) {
    case AppEventType::MouseDown:
        if (ev.mouseButton == activeButton) {
            if (!_mouseDown)
                appCaptureMouse();
            _mouseDown = true;
            _lastMouse = Float2(ev.mouseX, ev.mouseY);
        }
        break;
    case AppEventType::MouseUp:
        if (_mouseDown)
            appReleaseMouse();
    case AppEventType::MouseLeave:
        _mouseDown = false;
        break;
    case AppEventType::MouseMove:
        if (_mouseDown) {
            float dx = mathToRad(ev.mouseX - _lastMouse.x) * rotateSpeed;
            _lastMouse = Float2(ev.mouseX, ev.mouseY);
            RotateOrbit(dx);
        }
        break;
    case AppEventType::MouseScroll:
        _distance += -ev.scrollY * zoomStep;
        RotateOrbit(0);
        break;
    default:
        break;
    }
}