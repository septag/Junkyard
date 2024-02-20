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

    virtual void SetLookAt(Float3 pos, Float3 target, Float3 up = kFloat3UnitZ);
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
    Float3 mForward = kFloat3UnitY;
    Float3 mRight = kFloat3UnitX;
    Float3 mUp = kFloat3UnitZ;
    Float3 mPos = kFloat3Zero;

    float mNear = 0.1f;
    float mFar = 100.0f;
    float mFov = kPIQuarter;
};

struct CameraFPS : Camera
{
    void SetLookAt(Float3 pos, Float3 target, Float3 up = kFloat3UnitZ) override;
    void SetViewMat(const Mat4& viewMat) override;

    void RotatePitch(float pitch, float pitchMin = -kPI, float pitchMax = kPI);
    void RotateYaw(float yaw);
    void MoveForward(float forward);
    void Strafe(float strafe);  

    float GetPitch() const { return mPitch; };
    float GetYaw() const { return mYaw; };

    void HandleMovementKeyboard(float dt, float moveSpeed, float slowMoveSpeed) override;
    void HandleRotationMouse(const AppEvent& ev, float rotateSpeed, float zoomStep) override;

private:
    void UpdateRotation();

    Quat mQuat = kQuatIdent;
    Float2 mLastMouse = kFloat2Zero;
    float mPitch = 0;
    float mYaw = 0;
    bool mMouseDown = false;
    bool mKeyDown = false;
};

struct CameraOrbit : Camera
{
    void SetLookAt(Float3 pos, Float3 target, Float3 up = kFloat3UnitZ) override;
    void SetViewMat(const Mat4& viewMat) override;

    void RotateOrbit(float orbit);

    void HandleRotationMouse(const AppEvent& ev, float rotateSpeed, float zoomStep) override;

private:
    Float3 mTarget = kFloat3Zero;
    float mDistance = 0;
    float mElevation = 0;   // Angle for elevation, 0..PiHalf (Radians)
    float mOrbit = 0;       // Angle for rotating around orbit (Radians)
    Float2 mLastMouse = kFloat2Zero;
    bool mMouseDown = false;
};

