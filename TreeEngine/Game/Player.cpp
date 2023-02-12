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
    pos += XMVectorSet(m_hmdPosition.x, m_hmdPosition.y, m_hmdPosition.z, 0);
    return XMMatrixLookAtLH(pos, 
                            pos + finalForward, 
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
    XMMATRIX rot = XMMatrixRotationQuaternion(XMLoadFloat4(&_rotation));

    const char availableKeys[] = { 'W', 'S', 'D', 'A', VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, 'Y'};
    for (char k : availableKeys)
    {
        if (key[k])
        {
            XMMATRIX startRot, addRot;

            switch (k)
            {
            case 'Y':
                SetPosition(XMVectorSet(-6.0f, 1.5f, -6.0f, 1.0));
                SetRotation(XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 1), XM_PIDIV4));
                key[k] = false;
                break;
            case 'A':
                XMStoreFloat3(&_position, XMLoadFloat3(&_position) + XMVector4Transform(XMVectorSet(-0.05f, 0, 0, 1), rot));
                break;
            case 'D':
                XMStoreFloat3(&_position, XMLoadFloat3(&_position) + XMVector4Transform(XMVectorSet(+0.05f, 0, 0, 1), rot));
                break;
            case 'W':
                XMStoreFloat3(&_position, XMLoadFloat3(&_position) + XMVector4Transform(XMVectorSet(0,0, +0.05f, 1), rot));
                break;
            case 'S':
                XMStoreFloat3(&_position, XMLoadFloat3(&_position) + XMVector4Transform(XMVectorSet(0,0, -0.05f, 1), rot));
                break;
            case VK_LEFT:
                startRot = XMMatrixRotationQuaternion(XMLoadFloat4(&_rotation));
                addRot = XMMatrixRotationAxis(XMVectorSet(0,1,0,1), -0.02f);
                XMStoreFloat4(&_rotation, XMQuaternionRotationMatrix(startRot * addRot));
                break;
            case VK_RIGHT:
                startRot = XMMatrixRotationQuaternion(XMLoadFloat4(&_rotation));
                addRot = XMMatrixRotationAxis(XMVectorSet(0,1,0,1), 0.02f);
                XMStoreFloat4(&_rotation, XMQuaternionRotationMatrix(startRot * addRot));
                break;
            case VK_UP:
                startRot = XMMatrixRotationQuaternion(XMLoadFloat4(&_rotation));
                addRot = XMMatrixRotationAxis(XMVectorSet(1, 0, 0, 0), -0.02f);
                XMStoreFloat4(&_rotation, XMQuaternionRotationMatrix(addRot * startRot));
                break;
            case VK_DOWN:
                startRot = XMMatrixRotationQuaternion(XMLoadFloat4(&_rotation));
                addRot = XMMatrixRotationAxis(XMVectorSet(1, 0, 0, 0), 0.02f);
                XMStoreFloat4(&_rotation, XMQuaternionRotationMatrix(addRot * startRot));
                break;
            default:
                assert(false);
                break;
            }
        }
    }
}

