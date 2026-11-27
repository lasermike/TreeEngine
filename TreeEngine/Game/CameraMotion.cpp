#include "pch.h"
#include "CameraMotion.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace
{
    // Virtual-key codes (WinUser.h), so the file needs no Windows header.
    constexpr int KeyShift = 0x10, KeyControl = 0x11, KeySpace = 0x20, KeyPageUp = 0x21, KeyPageDown = 0x22;
    constexpr int KeyLeft = 0x25, KeyUp = 0x26, KeyRight = 0x27, KeyDown = 0x28;

    // Eases value toward target at rate per second, so movement speeds up and slows down rather than jumping.
    float Approach(float value, float target, float rate, float seconds)
    {
        return value + (target - value) * (std::min)(1.0f, rate * seconds);
    }
}

void CameraMotion::Reset(Mode mode, const XMFLOAT3& position)
{
    m_mode = mode;
    m_velocity = XMFLOAT3(0, 0, 0);
    m_eyeHeight = EyeHeight;
    m_onGround = false;
    m_spaceWasDown = false;
    m_lastSpacePress = -1;
    m_lastGround = position.y - EyeHeight;
}

void CameraMotion::SetFlyBoost(float boost)
{
    m_flyBoost = (std::max)(MinBoost, (std::min)(MaxBoost, boost));
}

void CameraMotion::Look(float yaw, float pitch, XMFLOAT4& rotation)
{
    if (yaw == 0 && pitch == 0)
        return;
    // The view's heading and pitch (positive pitch looks down: forward.y = -sin(pitch)), turned and held in range.
    XMFLOAT3 view;
    XMStoreFloat3(&view, XMVector3Rotate(XMVectorSet(0, 0, 1, 0), XMLoadFloat4(&rotation)));
    const float heading = atan2f(view.x, view.z) + yaw;
    const float tilt = (std::max)(-MaxPitch, (std::min)(MaxPitch, asinf((std::max)(-1.0f, (std::min)(1.0f, -view.y))) + pitch));
    XMStoreFloat4(&rotation, XMQuaternionRotationRollPitchYaw(tilt, heading, 0));
}

void CameraMotion::SetMode(Mode mode)
{
    if (mode == m_mode)
        return;
    m_mode = mode;
    m_velocity.y = 0;       // A walker taking off hovers; a flyer landing starts to fall from rest.
    m_onGround = false;
}

void CameraMotion::Step(const bool keys[512], float seconds, XMFLOAT3& position, XMFLOAT4& rotation, double now)
{
    if (!(seconds > 0))
        return;
    // Real time, for the double tap; only the movement step is cut.
    m_clock = now >= 0 ? now : m_clock + seconds;
    seconds = (std::min)(seconds, MaxStep);
    auto held = [keys](int key) { return keys[key] ? 1.0f : 0.0f; };

    // Looking: the arrow keys, at a rate in time rather than per frame.
    const float turn = LookSpeed * seconds;
    auto rotate = [&rotation](XMVECTOR axis, float angle, bool world)
    {
        const XMMATRIX current = XMMatrixRotationQuaternion(XMLoadFloat4(&rotation));
        const XMMATRIX add = XMMatrixRotationAxis(axis, angle);
        XMStoreFloat4(&rotation, XMQuaternionNormalize(XMQuaternionRotationMatrix(world ? current * add : add * current)));
    };
    if (keys[KeyLeft])  rotate(XMVectorSet(0, 1, 0, 0), -turn, true);
    if (keys[KeyRight]) rotate(XMVectorSet(0, 1, 0, 0), +turn, true);
    if (keys[KeyUp])    rotate(XMVectorSet(1, 0, 0, 0), -turn, false);
    if (keys[KeyDown])  rotate(XMVectorSet(1, 0, 0, 0), +turn, false);

    // Double-tapping Space switches walk and fly.
    const bool space = keys[KeySpace];
    if (space && !m_spaceWasDown)
    {
        if (m_lastSpacePress >= 0 && m_clock - m_lastSpacePress <= DoubleTapSeconds)
        {
            SetMode(m_mode == Mode::Walk ? Mode::Fly : Mode::Walk);
            m_lastSpacePress = -1;
        }
        else
            m_lastSpacePress = m_clock;
    }
    m_spaceWasDown = space;

    // W/A/S/D move level, as in Minecraft: forward is the view direction in the ground plane, so looking down doesn't
    // dive, and a rolled camera still walks where it looks. Looking straight down or up, the heading is the screen's
    // top (down) or bottom (up), as a Minecraft player turned that way would walk.
    const XMMATRIX orientation = XMMatrixRotationQuaternion(XMLoadFloat4(&rotation));
    const XMVECTOR view = XMVector3Transform(XMVectorSet(0, 0, 1, 0), orientation);
    XMVECTOR heading = XMVectorSetY(view, 0);
    if (XMVectorGetX(XMVector3LengthSq(heading)) < 1e-6f)
        heading = XMVectorSetY(XMVector3Transform(XMVectorSet(0, 1, 0, 0), orientation), 0)
            * (XMVectorGetY(view) < 0 ? 1.0f : -1.0f);
    const XMVECTOR forward = XMVector3Normalize(heading);
    // Right is level and square to forward, on the side of the screen's right (flipped for a camera rolled over).
    XMVECTOR right = XMVector3Cross(XMVectorSet(0, 1, 0, 0), forward);
    if (XMVectorGetX(XMVector3Dot(right, XMVector3Transform(XMVectorSet(1, 0, 0, 0), orientation))) < 0)
        right = -right;
    XMVECTOR wish = forward * (held('W') - held('S')) + right * (held('D') - held('A'));
    if (XMVectorGetX(XMVector3LengthSq(wish)) > 0)
        wish = XMVector3Normalize(wish);

    const bool walking = m_mode == Mode::Walk;
    const bool crouching = walking && keys[KeyShift];
    const float speed = walking ? WalkSpeed * (crouching ? CrouchFactor : keys[KeyControl] ? SprintFactor : 1.0f)
                                : FlySpeed * (keys[KeyShift] ? m_flyBoost : 1.0f);
    const float response = walking ? (m_onGround ? 10.0f : 2.0f) : 5.0f;     // Little steering in the air.
    const XMVECTOR target = wish * speed;
    m_velocity.x = Approach(m_velocity.x, XMVectorGetX(target), response, seconds);
    m_velocity.z = Approach(m_velocity.z, XMVectorGetZ(target), response, seconds);

    float rise;
    if (walking)
    {
        if (space && m_onGround)
        {
            m_velocity.y = sqrtf(2 * Gravity * JumpHeight);
            m_onGround = false;
        }
        // Falling exactly over the step (not a step of the new speed), so a jump is as high at 10 fps as at 120.
        rise = m_velocity.y * seconds - 0.5f * Gravity * seconds * seconds;
        m_velocity.y -= Gravity * seconds;
    }
    else
    {
        const float vertical = (std::max)(-1.0f, (std::min)(1.0f,
            held(KeySpace) + held(KeyPageUp) - held(KeyControl) - held(KeyPageDown)));
        m_velocity.y = Approach(m_velocity.y, vertical * speed, response, seconds);
        rise = m_velocity.y * seconds;
    }
    position.x += m_velocity.x * seconds;
    position.y += rise;
    position.z += m_velocity.z * seconds;

    if (walking)
    {
        // The ground under the camera: the terrain's height there (the last one found, off its edge), or 0 without
        // terrain. The eye stays at least its height above it; crouching lowers the eye smoothly.
        float ground = 0;
        if (m_ground)
        {
            if (m_ground(position.x, position.z, ground))
                m_lastGround = ground;
            else
                ground = m_lastGround;
        }
        m_eyeHeight = Approach(m_eyeHeight, crouching ? CrouchEyeHeight : EyeHeight, 12.0f, seconds);
        if (position.y <= ground + m_eyeHeight)
        {
            position.y = ground + m_eyeHeight;
            m_velocity.y = (std::max)(0.0f, m_velocity.y);
            m_onGround = true;
        }
        else
            m_onGround = false;
    }
}
