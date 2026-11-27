#include "pch.h"
#include "Player.h"

XMMATRIX Camera::GetViewMatrix()
{
    XMMATRIX rotMat = XMMatrixRotationQuaternion(XMQuaternionMultiply(
        XMLoadFloat4(&m_hmdRotation), m_parent->GetRotation()
        
    ));
                                                     
    XMVECTOR finalUp      = XMVector3Transform(XMVectorSet(0, 1, 0, 0), rotMat);
    XMVECTOR finalForward = XMVector3Transform(XMVectorSet(0, 0, 1, 0), rotMat);

    XMVECTOR pos = m_parent->GetPosition();
    pos += XMVectorSet(m_hmdPosition.x, m_hmdPosition.y, m_hmdPosition.z, 0);  //VR

    if (XMVector3Equal(finalForward, XMVectorZero()) ||
        XMVector3IsInfinite(finalForward) ||
        XMVector3Equal(finalUp, XMVectorZero()) ||
        XMVector3IsInfinite(finalUp))
    {
        return XMMatrixIdentity();
    }

    XMVECTOR focusPosition = pos + finalForward;

    return XMMatrixLookAtLH(pos, 
                            focusPosition, 
                            finalUp);
}


Player::Player(WorldObjectParams* params) : WorldObject(params), m_camera(nullptr)
{
    m_camera = new Camera(this);
}


Player::~Player(void)
{
}

XMMATRIX Player::GetViewMatrix()
{
    return m_camera->GetViewMatrix();
}

XMVECTOR Player::GetEyePosition()
{
    return XMLoadFloat3(&_position);
}


void Player::HandleInput(bool key[512])  // WM_KEYDOWN
{
    // Movement reads the held keys each update; Y (back to the default view) is a one-shot.
    memcpy(m_keys, key, sizeof(m_keys));
    if (key['Y'])
    {
        SetPosition(XMVectorSet(-6.0f, 1.5f, -6.0f, 1.0));
        SetRotation(XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 1), XM_PIDIV4));
        ResetMovement(GetMode());
        key['Y'] = false;
        m_keys['Y'] = false;
    }
}
