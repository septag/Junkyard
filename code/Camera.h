#pragma once

#include "Core/MathTypes.h"

struct AppEvent;    // Application.h
struct CameraFrustumPoints
{
    Float3 p[8];

    Float3& operator[](uint32 index) { ASSERT(index<8); return p[index]; }
    const Float3& operator[](uint32 index) const { ASSERT(index<8); return p[index]; }
    uint32 Count() const { return CountOf(p); }
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


    Plane p[_Count];
    
    Plane& operator[](uint32 index) { ASSERT(index < _Count); return p[index]; }
    const Plane& operator[](uint32 index) const { ASSERT(index < _Count); return p[index]; }
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

    Float3 Forward() const { return _forward; }
    Float3 Right() const { return _right; };
    Float3 Up() const { return _up; }
    Float3 Position() const { return _pos; }
    float Far() const { return _far; }
    float Near() const { return _near; }
    float Fov() const { return _fov; }

protected:
    Float3 _forward = kFloat3UnitY;
    Float3 _right = kFloat3UnitX;
    Float3 _up = kFloat3UnitZ;
    Float3 _pos = kFloat3Zero;

    float _near = 0.1f;
    float _far = 100.0f;
    float _fov = kPIQuarter;
};

struct CameraFPS : Camera
{
    void SetLookAt(Float3 pos, Float3 target, Float3 up = kFloat3UnitZ) override;
    void SetViewMat(const Mat4& viewMat) override;

    void RotatePitch(float pitch, float pitchMin = -kPI, float pitchMax = kPI);
    void RotateYaw(float yaw);
    void MoveForward(float forward);
    void Strafe(float strafe);  

    float GetPitch() const { return _pitch; };
    float GetYaw() const { return _yaw; };

    void HandleMovementKeyboard(float dt, float moveSpeed, float slowMoveSpeed) override;
    void HandleRotationMouse(const AppEvent& ev, float rotateSpeed, float zoomStep) override;

private:
    void UpdateRotation();

    Quat _quat = kQuatIdent;
    Float2 _lastMouse = kFloat2Zero;
    float _pitch = 0;
    float _yaw = 0;
    float _speedTime = 0;
    float _moveStrafe = 0;
    float _moveFwd = 0;
    bool _mouseDown = false;
    bool _keyDown = false;
};

struct CameraOrbit : Camera
{
    void SetLookAt(Float3 pos, Float3 target, Float3 up = kFloat3UnitZ) override;
    void SetViewMat(const Mat4& viewMat) override;

    void RotateOrbit(float orbit);

    void HandleRotationMouse(const AppEvent& ev, float rotateSpeed, float zoomStep) override;

private:
    Float3 _target = kFloat3Zero;
    float _distance = 0;
    float _elevation = 0;   // Angle for elevation, 0..PiHalf (Radians)
    float _orbit = 0;       // Angle for rotating around orbit (Radians)
    Float2 _lastMouse = kFloat2Zero;
    bool _mouseDown = false;
};

