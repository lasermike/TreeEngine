#pragma once
#include "worldobject.h"
#include "OrbitCamera.h"
#include "StepTimer.h"

enum CameraType
{
	OrbitCamera = 0,
	WalkCamera
};

//-----------------------------------------------------------
class Camera
{
	WorldObject* m_parent;

public:
	Camera(WorldObject* parent) { m_parent = parent; }

    XMMATRIX GetViewMatrix()
	{
		XMMATRIX rotMat = XMMatrixRotationQuaternion(m_parent->GetRotation());
		XMVECTOR finalUp      = XMVector3Transform(XMVectorSet(0, 1, 0, 1), rotMat);
        XMVECTOR finalForward = XMVector3Transform(XMVectorSet(0, 0, 1, 1), rotMat);

		XMVECTOR pos = m_parent->GetPosition();
		return XMMatrixLookAtLH(pos, 
								pos + finalForward, 
								finalUp);
	}

};

//-----------------------------------------------------------

class Player :
	public WorldObject
{

	// Camera
	CameraType							m_cameraType;
	Camera* m_camera;
	XSF::OrbitCamera*					m_orbitCamera;
	WorldObject*						_selection;
	float								_rotateSpeed;
	float								_dollySpeed;


public:
	Player(WorldObjectParams* params);
	~Player(void);

    void HandleInput(bool key[256]);

	Camera* GetCamera() { return m_camera; }
    XSF::OrbitCamera* GetOrbitCamera() { return m_orbitCamera; }
    
    XMMATRIX GetViewMatrix();
    XMVECTOR GetEyePosition();
    void UpdateOrbitCamera(DX::StepTimer const& timer, RenderData* pRenderData);
	void Select(WorldObject* pSelected);
	void Select(int index);


    inline void Update(DX::StepTimer const& timer, RenderData* pRenderData)
    {
	    if (m_cameraType == OrbitCamera)
	    {
		    UpdateOrbitCamera(timer, pRenderData);
	    }
    }
};

