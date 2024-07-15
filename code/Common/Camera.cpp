#include "Camera.h"
#include "Application.h"

#include "../Core/MathVector.h"

#ifdef near
    #undef near
#endif

#ifdef far
    #undef far
#endif

Camera::Camera(float fovDeg, float fnear, float ffar) :
    mNear(fnear),
    mFar(ffar),
    mFov(mathToRad(fovDeg))
{
    ASSERT(mFar > mNear);
}

void Camera::Setup(float fovDeg, float fnear, float ffar)
{
    ASSERT(ffar > fnear);

    mFov = mathToRad(fovDeg);
    mNear = fnear;
    mFar = ffar;
}

Mat4 Camera::GetOrthoMat(float viewWidth, float viewHeight) const
{
    return mat4Ortho(viewWidth, viewHeight, mNear, mFar);
}

Mat4 Camera::GetPerspectiveMat(float viewWidth, float viewHeight) const
{
    return mat4PerspectiveFOV(mFov, viewWidth/viewHeight, mNear, mFar);
}

Mat4 Camera::GetViewMat() const
{
    Float3 zaxis = mForward;
    Float3 xaxis = mRight;    // norm(cross(zaxis, up));
    Float3 yaxis = mUp;       // cross(xaxis, zaxis);

    return Mat4(xaxis.x,   xaxis.y,    xaxis.z,    -float3Dot(xaxis, mPos), 
                yaxis.x,   yaxis.y,    yaxis.z,    -float3Dot(yaxis, mPos), 
                -zaxis.x, -zaxis.y,    -zaxis.z,    float3Dot(zaxis, mPos), 
                0.0f,      0.0f,       0.0f,        1.0f);
}

void Camera::SetViewMat(const Mat4& viewMat)
{
    Mat4 vpInv = mat4Inverse(viewMat);
    mRight = Float3(vpInv.fc1);
    mUp = Float3(vpInv.fc2);
    mForward = Float3(vpInv.fc3) * -1.0f;
    mPos = Float3(vpInv.fc4);
}

CameraFrustumPoints Camera::GetFrustumPoints(float viewWidth, float viewHeight, float overrideNear, float overrideFar) const
{
    CameraFrustumPoints frustum;

    float near = overrideNear >= 0 ? overrideNear : mNear;
    float far = overrideFar >= 0 ? overrideFar : mFar;
    ASSERT(far > near);

    const float fov = mFov;
    const float aspect = viewWidth / viewHeight;

    Float3 xaxis = mRight;
    Float3 yaxis = mUp;
    Float3 zaxis = mForward;
    Float3 pos = mPos;

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
    mForward = float3Norm(target - pos);
    mRight = float3Norm(float3Cross(mForward, up));
    mUp = float3Cross(mRight, mForward);
    mPos = pos;
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

    Mat4 m = Mat4(Float4(mRight.x,     mRight.y,       mRight.z,   0),
                  Float4(-mUp.x,      -mUp.y,         -mUp.z,      0),
                  Float4(mForward.x,   mForward.y,     mForward.z, 0), 
                  Float4(0,            0,              0,          1.0f));
    mQuat = mat4ToQuat(m);

    Float3 euler = quatToEuler(mQuat);
    mPitch = euler.x;
    mYaw = euler.z;
}

void CameraFPS::SetViewMat(const Mat4& viewMat)
{
    Camera::SetViewMat(viewMat);

    Mat4 m = Mat4(Float4(mRight.x,     mRight.y,       mRight.z,   0),
                  Float4(-mUp.x,      -mUp.y,         -mUp.z,      0),
                  Float4(mForward.x,   mForward.y,     mForward.z, 0), 
                  Float4(0,            0,              0,          1.0f));
    mQuat = mat4ToQuat(m);

    Float3 euler = quatToEuler(mQuat);
    mPitch = euler.x;
    mYaw = euler.z;
}

void CameraFPS::UpdateRotation()
{
    Mat4 m = quatToMat4(mQuat);
    mRight = Float3(m.fc1);
    mUp = Float3(m.fc2)*-1.0f;
    mForward = Float3(m.fc3);
}

void CameraFPS::RotatePitch(float pitch, float pitchMin, float pitchMax)
{
    mPitch = Clamp(mPitch - pitch, pitchMin, pitchMax);
    mQuat = quatRotateZ(mYaw) * quatRotateX(mPitch);
    UpdateRotation();
}

void CameraFPS::RotateYaw(float yaw)
{
    mYaw -= yaw;
    mQuat = quatRotateZ(mYaw) * quatRotateX(mPitch);
    UpdateRotation();
}

void CameraFPS::MoveForward(float forward)
{
    mPos = mPos + mForward*forward;
}

void CameraFPS::Strafe(float strafe)
{
    mPos = mPos + mRight*strafe;
}

void CameraFPS::HandleMovementKeyboard(float dt, float moveSpeed, float slowMoveSpeed)
{
    if (App::IsKeyDown(InputKeycode::LeftShift) || App::IsKeyDown(InputKeycode::RightShift))
        moveSpeed = slowMoveSpeed;

    Float3 targetPos = mPos;
    if (App::IsKeyDown(InputKeycode::A) || App::IsKeyDown(InputKeycode::Left)) 
        targetPos = mPos - mRight*(moveSpeed*dt);
    if (App::IsKeyDown(InputKeycode::D) || App::IsKeyDown(InputKeycode::Right))
        targetPos = mPos + mRight*(moveSpeed*dt);
    if (App::IsKeyDown(InputKeycode::W) || App::IsKeyDown(InputKeycode::Up))
        targetPos = mPos + mForward*(moveSpeed*dt);
    if (App::IsKeyDown(InputKeycode::S) || App::IsKeyDown(InputKeycode::Down))
        targetPos = mPos - mForward*(moveSpeed*dt);

    float h = -0.1f/mathLog2(0.01f);
    mPos = float3SmoothLerp(mPos, targetPos, dt, h);
}

void CameraFPS::HandleRotationMouse(const AppEvent& ev, float rotateSpeed, float zoomStep)
{
    UNUSED(zoomStep);

    const InputKeycode moveKeys[] = {
        InputKeycode::W, InputKeycode::A, InputKeycode::S, InputKeycode::D,
        InputKeycode::Up, InputKeycode::Left, InputKeycode::Down, InputKeycode::Right
    };

    InputMouseButton activeButton = InputMouseButton::Right;
    if constexpr (PLATFORM_ANDROID)
        activeButton = InputMouseButton::Left;

    switch (ev.type) {
    case AppEventType::MouseDown:
        if (ev.mouseButton == activeButton) {
            if (!mMouseDown)
                App::CaptureMouse();
            mMouseDown = true;
            mLastMouse = Float2(ev.mouseX, ev.mouseY);
        }
        break;
    case AppEventType::MouseUp:
        if (mMouseDown)
             App::ReleaseMouse();
    case AppEventType::MouseLeave:
        mMouseDown = false;
        break;
    case AppEventType::MouseMove:
        if (mMouseDown) {
            float dx = mathToRad(ev.mouseX - mLastMouse.x) * rotateSpeed;
            float dy = mathToRad(ev.mouseY - mLastMouse.y) * rotateSpeed;
            mLastMouse = Float2(ev.mouseX, ev.mouseY);
            RotatePitch(dy);
            RotateYaw(dx);
        }
        break;
    case AppEventType::KeyDown:
        if (!mKeyDown) 
            mKeyDown = true;
        break;

    case AppEventType::KeyUp:
        if (mKeyDown && !App::IsAnyKeysDown(moveKeys, CountOf(moveKeys)))
            mKeyDown = false;
        break;
    default:
        
        break;
    }
}

void CameraOrbit::SetLookAt(Float3 pos, Float3 target, Float3 up) 
{
    UNUSED(up);     // Maybe we should also keep `up` and pass it on ?

    mTarget = target;
    Float3 look = target - pos;

    mDistance = float3Len(look);
    mOrbit = -mathACos(float2Dot(float2Norm(Float2(look.f)*-1.0f), Float2(1.0f, 0)));

    float a = mathACos(float3Dot(float3Norm(look), mathIsEqual(look.z, 0, 0.00001f) ? FLOAT3_ZERO : float3Norm(Float3(0, 0, look.z))));
    mElevation = M_HALFPI - a;
    if (mElevation < 0)
        mElevation = -mElevation;
    ASSERT(mElevation >= 0 && mElevation <= M_HALFPI);

    RotateOrbit(0);
}

void CameraOrbit::SetViewMat(const Mat4& viewMat)
{
    Camera::SetViewMat(viewMat);
}

void CameraOrbit::RotateOrbit(float orbit)
{
    mOrbit += orbit;

    float x = mDistance * mathCos(mOrbit);
    float y = mDistance * mathSin(mOrbit);
    float z = mDistance * mathCos(M_HALFPI - mElevation);

    Camera::SetLookAt(Float3(x, y, z), mTarget);
}

void CameraOrbit::HandleRotationMouse(const AppEvent& ev, float rotateSpeed, float zoomStep)
{
    ASSERT(zoomStep > 0);
    ASSERT(rotateSpeed > 0);

    InputMouseButton activeButton = InputMouseButton::Right;
    if constexpr (PLATFORM_ANDROID)
        activeButton = InputMouseButton::Left;

    switch (ev.type) {
    case AppEventType::MouseDown:
        if (ev.mouseButton == activeButton) {
            if (!mMouseDown)
                App::CaptureMouse();
            mMouseDown = true;
            mLastMouse = Float2(ev.mouseX, ev.mouseY);
        }
        break;
    case AppEventType::MouseUp:
        if (mMouseDown)
             App::ReleaseMouse();
    case AppEventType::MouseLeave:
        mMouseDown = false;
        break;
    case AppEventType::MouseMove:
        if (mMouseDown) {
            float dx = mathToRad(ev.mouseX - mLastMouse.x) * rotateSpeed;
            mLastMouse = Float2(ev.mouseX, ev.mouseY);
            RotateOrbit(dx);
        }
        break;
    case AppEventType::MouseScroll:
        mDistance += -ev.scrollY * zoomStep;
        RotateOrbit(0);
        break;
    default:
        break;
    }
}