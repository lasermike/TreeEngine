#include "pch.h"
#include "Player.h"

XMMATRIX Camera::GetViewMatrix()
{
    XMMATRIX rotMat = XMMatrixRotationQuaternion(XMQuaternionMultiply(
        XMLoadFloat4(&m_hmdRotation), m_parent->GetRotation()
        
    ));
                                                                          
    //XMMATRIX rotMat = XMMatrixRotationQuaternion(XMLoadFloat4(&m_hmdRotation));

	XMVECTOR finalUp      = XMVector3Transform(XMVectorSet(0, 1, 0, 0), rotMat);
    XMVECTOR finalForward = XMVector3Transform(XMVectorSet(0, 0, 1, 0), rotMat);

	XMVECTOR pos = m_parent->GetPosition();
	return XMMatrixLookAtLH(pos, 
							pos + finalForward, 
							finalUp);
}


Player::Player(WorldObjectParams* params) : WorldObject(params)					   
{
    m_cameraType = WalkCameraType; //OrbitCamera

	// Create the scene
	if (m_cameraType == OrbitCameraType)
	{
		_selection = nullptr;
		_rotateSpeed = 0.0f;
		_dollySpeed = 0.0f;
		m_orbitCamera = new OrbitCamera();
	}
    else
    {
	    m_camera = new Camera(this);
    }
}


Player::~Player(void)
{
}

XMMATRIX Player::GetViewMatrix()
{
	if (m_cameraType == OrbitCameraType)
	{
		return m_orbitCamera->GetViewMatrix();
	}
	else 
	{
		return m_camera->GetViewMatrix();
	}
}

XMVECTOR Player::GetEyePosition()
{
	if (m_cameraType == OrbitCameraType)
	{
		return m_orbitCamera->GetEyePosition();
	}
	else 
	{
		return XMLoadFloat3(&_position);
	}
}


void Player::UpdateOrbitCamera(DX::StepTimer const& timer, RenderData* pRenderData)
{
	// Find top point of scene or selected object
	//XMVECTOR extent;
	//if (_selection == nullptr)
	//{
	//	extent = _pScene->GetExtents(TOP);
	//}
	//else
	//{
	//	extent = _selection->GetExtents(TOP);
	//}

	//XMFLOAT3 e;
	//XMStoreFloat3(&e, extent);

	if (_dollySpeed != 0.0f)
	{
		m_orbitCamera->AddDollyVelocity(_dollySpeed);
	}
	//else if (0)
	//{
	//	// Determine if extent point is inside or outside the view frustrum
	//	XMVECTOR v0, v1;
	//	m_orbitCamera->RayCast(pRenderData->projectionData.screenWidth / 2,0, pRenderData, v0, v1);
	//	XMFLOAT3 vv0, vv1;
	//	XMStoreFloat3(&vv0, v0);
	//	XMStoreFloat3(&vv1, v1);
	//	float topDelta = (vv1.y - vv0.y) / (pRenderData->projectionData.farClippingPlane - pRenderData->projectionData.nearClippingPlane);
	//	
	//	float frustumTopAtExtent = vv0.y + topDelta * sqrt((e.x - vv0.x) * (e.x - vv0.x) + (e.z - vv0.z) * (e.z - vv0.z)); 

	//	// Dolly nearest or further
	//	if (frustumTopAtExtent < e.y)
	//	{
	//		m_orbitCamera->SetDollyVelocity(0.5f);
	//	}
	//	else if (frustumTopAtExtent > e.y + 0.5f)
	//	{
	//		m_orbitCamera->SetDollyVelocity(-0.5f);
	//	}
	//	else 
	//	{
	//		m_orbitCamera->SetDollyVelocity(0);
	//	}
	//}

	// Rotate camera around the origin
	if (_rotateSpeed != 0.0f)
	{
		m_orbitCamera->AddHeadingVelocity(_rotateSpeed);
	}
	

	//if (_selection)
	//{
	//	XMVECTOR focusPoint = m_orbitCamera->GetFocusPosition();
	//	XMVECTOR desiredFocus = XMVectorSetY(extent, XMVectorGetY(extent) / 2.0f); 
	//	XMVECTOR difference = desiredFocus - focusPoint;
	//	float magnitude = XMVectorGetX(XMVector3Length(difference));
	//	if (magnitude > 1.0f)
	//	{
	//		m_orbitCamera->SetFocusPositionVelocity(difference / magnitude);
	//	}
	//	else
	//	{
	//		m_orbitCamera->SetFocusPositionAttenuation(250);
	//	}
	//}

	m_orbitCamera->Update((float) timer.GetElapsedSeconds());
	XMStoreFloat4(&pRenderData->eyePos, m_orbitCamera->GetEyePosition());
	//UpdateView();
}

void Player::HandleInput(bool key[256])  // WM_KEYDOWN
{
	_rotateSpeed = 0.0f;
	_dollySpeed = 0.0f;

    if (m_cameraType == OrbitCameraType)
    {
	    const char availableKeys[] = { 'W', 'S', 'D', 'A' };
	    for (char k : availableKeys)
	    {
		    if (key[k])
		    {
			    switch (k)
			    {
			    case 'A':
				    _rotateSpeed += key[k] ? -1.0f : 0.0f;
				    break;
			    case 'D':
				    _rotateSpeed += key[k] ? 1.0f : 0.0f;
				    break;
			    case 'W':
				    _dollySpeed += key[k] ? -0.5f : 0.0f;
				    break;
			    case 'S':
				    _dollySpeed += key[k] ? 0.5f : 0.0f;
				    break;
                }
            }
        }
    }
    else
    {
        XMMATRIX rot = XMMatrixRotationQuaternion(XMLoadFloat4(&_rotation));

	    const char availableKeys[] = { 'W', 'S', 'D', 'A', VK_LEFT, VK_RIGHT };
	    for (char k : availableKeys)
	    {
		    if (key[k])
		    {
				XMMATRIX startRot, addRot;

			    switch (k)
			    {
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
				default:
					assert(false);
					break;
                }
            }
        }
    }
}

void Player::Select(int index)
{
	//WorldObject* pObj = *_pScene->Children().begin();
	//int i = 0;
	//for (auto child = _pScene->Children().begin(); i < index && child != _pScene->Children().end(); child++)
	//{
	//	if ((*child)->GetObjectType() == TreeType)
	//	{
	//		pObj = *child;
	//		i++;
	//	}
	//}

	//Select(pObj);
}

void Player::Select(WorldObject* pSelected)
{
	_selection = pSelected;

	//m_orbitCamera->SetFocusPositionAttenuation(60);

	//XMVECTOR extent;
	//if (_selection == nullptr)
	//{
	//	extent = _pScene->GetExtents(TOP);
	//}
	//else
	//{
	//	extent = _selection->GetExtents(TOP);
	//}

	//extent = XMVectorSetY(extent, XMVectorGetY(extent) / 2.0f); 
	//m_orbitCamera->SetFocusPosition(extent);
}
