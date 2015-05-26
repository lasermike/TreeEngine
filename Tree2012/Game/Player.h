#pragma once
#include "worldobject.h"

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
	Camera* m_camera;

public:
	Player(WorldObjectParams* params);
	~Player(void);

	Camera* GetCamera() { return m_camera; }
};

