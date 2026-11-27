#pragma once
#include "worldobject.h"
#include "StepTimer.h"
#include "CameraMotion.h"
#include <chrono>

//-----------------------------------------------------------
class Camera
{
    WorldObject* m_parent;
    XMFLOAT3     m_hmdPosition;
    XMFLOAT4     m_hmdRotation;

public:
    Camera(WorldObject* parent)
    {
        m_parent = parent;
        m_hmdPosition = XMFLOAT3(0,0,0);
        XMStoreFloat4(&m_hmdRotation, XMQuaternionIdentity());
    }

    void SetHmdState(XMFLOAT3 hmdPosition, XMFLOAT4 hmdRotation) { m_hmdPosition = hmdPosition; m_hmdRotation = hmdRotation; }

    XMMATRIX GetViewMatrix();
};

//-----------------------------------------------------------
// The user's camera: walk and fly movement (CameraMotion) on the keys held.

class Player :
    public WorldObject
{
    // Camera
    Camera*      m_camera;
    CameraMotion m_motion;
    bool         m_keys[512] = {};
    float        m_lookYaw = 0, m_lookPitch = 0;     // Mouse look since the last update, radians.

public:
    using Mode = CameraMotion::Mode;

    Player(WorldObjectParams* params);
    ~Player(void);

    void HandleInput(bool key[512]);
    void ClearInput() { memset(m_keys, 0, sizeof(m_keys)); ClearLook(); }
    // Mouse look: radians right and down, applied at the next update.
    void AddLook(float yaw, float pitch) { m_lookYaw += yaw; m_lookPitch += pitch; }
    void ClearLook() { m_lookYaw = m_lookPitch = 0; }
    float GetFlyBoost() const { return m_motion.GetFlyBoost(); }
    void SetFlyBoost(float boost) { m_motion.SetFlyBoost(boost); }

    // Where the walk camera stands; without it, the ground is at height 0.
    void SetGround(CameraMotion::GroundHeight ground) { m_motion.SetGround(std::move(ground)); }
    // A new scene, or a restart: the mode, at rest.
    void ResetMovement(Mode mode) { m_motion.Reset(mode, _position); }
    Mode GetMode() const { return m_motion.GetMode(); }

    Camera* GetCamera() { return m_camera; }

    XMMATRIX GetViewMatrix();
    XMVECTOR GetEyePosition();

    float* GetEyePositionPtr() { return (float*) & _position; }
    float* GetEyeRotationPtr() { return (float*) &_rotation; }

    inline void Update(DX::StepTimer const& timer, RenderData* pRenderData)
    {
        // The wall clock times the double tap: the timer's steps are capped at 0.1 s, so they understate slow frames.
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        CameraMotion::Look(m_lookYaw, m_lookPitch, _rotation);
        ClearLook();
        m_motion.Step(m_keys, static_cast<float>(timer.GetElapsedSeconds()), _position, _rotation, now);
    }
};
