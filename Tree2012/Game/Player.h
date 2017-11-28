#pragma once
#include "worldobject.h"
#include "OrbitCamera.h"
#include "StepTimer.h"

enum CameraType
{
    OrbitCameraType = 0,
    WalkCameraType
};

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
    CameraType                            m_cameraType;
    Camera*                                m_camera;
    OrbitCamera*                        m_orbitCamera;
    WorldObject*                        _selection;
    float                                _rotateSpeed;
    float                                _dollySpeed;


public:
    Player(WorldObjectParams* params);
    ~Player(void);

    void HandleInput(bool key[256]);

    Camera* GetCamera() { return m_camera; }
    OrbitCamera* GetOrbitCamera() { return m_orbitCamera; }
    
    XMMATRIX GetViewMatrix();
    XMVECTOR GetEyePosition();
    void UpdateOrbitCamera(DX::StepTimer const& timer, RenderData* pRenderData);
    void Select(WorldObject* pSelected);
    void Select(int index);


    inline void Update(DX::StepTimer const& timer, RenderData* pRenderData)
    {
        if (m_cameraType == OrbitCameraType)
        {
            UpdateOrbitCamera(timer, pRenderData);
        }
    }
};

