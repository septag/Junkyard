#pragma once

#include "../Core/MathTypes.h"

struct AppEvent;    // Application.h
struct CameraFrustumPoints
{
    Float3 mPoints[8];

    Float3& operator[](uint32 index) { ASSERT(index<8); return mPoints[index]; }
    const Float3& operator[](uint32 index) const { ASSERT(index<8); return mPoints[index]; }
    uint32 Count() const { return CountOf(mPoints); }
};

struct CameraFrustumPlanes 
{
    enum FrustumPlane
    {
        PlaneLeft = 0,
        PlaneRight, 
        PlaneTop,
        PlaneBottom,
        PlaneNear,
        PlaneFar,
        _Count
    };


    Plane mPlanes[_Count];
    
    Plane& operator[](uint32 index) { ASSERT(index < _Count); return mPlanes[index]; }
    const Plane& operator[](uint32 index) const { ASSERT(index < _Count); return mPlanes[index]; }
};

struct Camera
{
    Camera() = default;
    explicit Camera(float fovDeg, float fnear, float ffar);

    void Setup(float fovDeg, float fnear, float ffar);

    virtual void SetLookAt(Float3 pos, Float3 target, Float3 up = FLOAT3_UNITZ);
    virtual void SetViewMat(const Mat4& viewMat);
    virtual void HandleMovementKeyboard(float dt, float moveSpeed, float slowMoveSpeed);
    virtual void HandleRotationMouse(const AppEvent& ev, float rotateSpeed, float zoomStep);

    Mat4 GetOrthoMat(float viewWidth, float viewHeight) const;
    Mat4 GetPerspectiveMat(float viewWidth, float viewHeight) const;
    Mat4 GetViewMat() const;

    CameraFrustumPoints GetFrustumPoints(float viewWidth, float viewHeight, float overrideNear = -1.0f, float overrideFar = -1.0f) const;
    CameraFrustumPlanes GetFrustumPlanes(const Mat4& viewProjMat) const;

    Float3 Forward() const { return mForward; }
    Float3 Right() const { return mRight; };
    Float3 Up() const { return mUp; }
    Float3 Position() const { return mPos; }
    float Far() const { return mFar; }
    float Near() const { return mNear; }
    float Fov() const { return mFov; }

protected:
    Float3 mForward = FLOAT3_UNITY;
    Float3 mRight = FLOAT3_UNIX;
    Float3 mUp = FLOAT3_UNITZ;
    Float3 mPos = FLOAT3_ZERO;

    float mNear = 0.1f;
    float mFar = 100.0f;
    float mFov = M_QUARTERPI;
};

struct CameraFPS : Camera
{
    void SetLookAt(Float3 pos, Float3 target, Float3 up = FLOAT3_UNITZ) override;
    void SetViewMat(const Mat4& viewMat) override;

    void RotatePitch(float pitch, float pitchMin = -M_PI, float pitchMax = M_PI);
    void RotateYaw(float yaw);
    void MoveForward(float forward);
    void Strafe(float strafe);  

    float GetPitch() const { return mPitch; };
    float GetYaw() const { return mYaw; };

    void HandleMovementKeyboard(float dt, float moveSpeed, float slowMoveSpeed) override;
    void HandleRotationMouse(const AppEvent& ev, float rotateSpeed, float zoomStep) override;

private:
    void UpdateRotation();

    Quat mQuat = QUAT_INDENT;
    Float2 mLastMouse = FLOAT2_ZERO;
    float mPitch = 0;
    float mYaw = 0;
    bool mMouseDown = false;
    bool mKeyDown = false;
};

struct CameraOrbit : Camera
{
    void SetLookAt(Float3 pos, Float3 target, Float3 up = FLOAT3_UNITZ) override;
    void SetViewMat(const Mat4& viewMat) override;

    void RotateOrbit(float orbit);

    void HandleRotationMouse(const AppEvent& ev, float rotateSpeed, float zoomStep) override;

private:
    Float3 mTarget = FLOAT3_ZERO;
    float mDistance = 0;
    float mElevation = 0;   // Angle for elevation, 0..PiHalf (Radians)
    float mOrbit = 0;       // Angle for rotating around orbit (Radians)
    Float2 mLastMouse = FLOAT2_ZERO;
    bool mMouseDown = false;
};

