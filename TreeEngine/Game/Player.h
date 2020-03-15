#pragma once
#include "worldobject.h"
#include "StepTimer.h"

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

class Player :
    public WorldObject
{

    // Camera
    Camera*                              m_camera;


public:
    Player(WorldObjectParams* params);
    ~Player(void);

    void HandleInput(bool key[512]);

    Camera* GetCamera() { return m_camera; }
    
    XMMATRIX GetViewMatrix();
    XMVECTOR GetEyePosition();

    inline void Update(DX::StepTimer const& timer, RenderData* pRenderData)
    {
    }
};

