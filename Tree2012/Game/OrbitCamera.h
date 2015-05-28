//--------------------------------------------------------------------------------------
// OrbitCamera.h
//
// A camera that's useful for 3D object inspection.
//
// Xbox Advanced Technology Group (ATG).
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once
#ifndef XSF_ORBIT_CAMERA_H_INCLUDED
#define XSF_ORBIT_CAMERA_H_INCLUDED

struct RenderData;


__declspec(align(16)) 
class Camera2
{
    XMVECTOR Pos;
    XMMATRIX Rot;
    Camera2() { };
    //Camera(XMVECTOR pos, XMMATRIX rot) : Pos(pos), Rot(rot) { };
    XMMATRIX GetViewMatrix()
    {    
		XMVECTOR finalUp      = XMVector3Transform(XMVectorSet(0, 1, 0, 1),  Rot);
        XMVECTOR finalForward = XMVector3Transform(XMVectorSet(0, 0, -1, 1), Rot);

		return XMMatrixLookAtLH(Pos, Pos + finalForward, finalUp);

        //return(Matrix4f::LookAtRH(Pos, Pos + finalForward, finalUp));
    }
};

//----------------------------------------------------

__declspec(align(16)) 
class OrbitCamera
{
public:
	OrbitCamera();
	~OrbitCamera();

	void* operator new(size_t size) 
	{ 
		return _aligned_malloc(size, 16); 
	}
	void operator delete(void* mem) { return _aligned_free(mem); }

    // Move the focus position (but keep the camera where it is)
    void SetFocusPosition( _In_ FXMVECTOR focusPos ) { m_FocusPosition = focusPos; }
    XMVECTOR GetFocusPosition() const { return m_FocusPosition; }

    void SetFocusPositionVelocity( _In_ FXMVECTOR focusPosVel ) { m_FocusPositionVelocity = focusPosVel; }
    void SetFocusPositionAttenuation( _In_ float focusPosVelAtt ) { m_FocusPositionVelocityAttenuation = focusPosVelAtt; }

    // Sets the focus position to the center of a bounding box.
    // The box is determined of the min/max of the supplied points.
    // The distance from the center is determined by the size of the
    // box.
    // This also updates the minimum distance so the camera cannot
    // intersect the box (but leaves the max distance unaffected.)
    void FocusOnBoundingBox(
        _In_reads_(numCorners) const XMFLOAT3* pBoxCorners,
        _In_ UINT numCorners );

    // Add rotational velocity
	void SetHeading( _In_ float h );
	float GetHeading() const { return m_Heading; }

	void AddHeadingVelocity( _In_ float hv ) { m_HeadingVelocity += hv; }
	void SetHeadingVelocity( _In_ float hv ) { m_HeadingVelocity = hv; }
	float GetHeadingVelocity() const { return m_HeadingVelocity; }

	void SetHeadingVelocityAttenuation( _In_ float hva ) { m_HeadingVelocityAttenuation = hva; }
	float GetHeadingVelocityAttenuation() const { return m_HeadingVelocityAttenuation; }

	void SetPitch( _In_ float p );
	float GetPitch() const { return m_Pitch; }

	void AddPitchVelocity( _In_ float pv ) { m_PitchVelocity += pv; }
	void SetPitchVelocity( _In_ float pv ) { m_PitchVelocity = pv; }
	float GetPitchVelocity() const { return m_PitchVelocity; }

	void SetPitchVelocityAttenuation( _In_ float pva ) { m_PitchVelocityAttenuation = pva; }
	float GetPitchVelocityAttenuation() const { return m_PitchVelocityAttenuation; }

    // Zoom in and out from the center (negative = towards, positive = towards)
    void SetDolly( _In_ float dolly );
    float GetDolly() const { return m_Dolly; }

	void AddDollyVelocity( _In_ float dolly );
	void SetDollyVelocity( _In_ float dolly ) { m_DollyVelocity = dolly; }
	float GetDollyVelocity() const { return m_DollyVelocity; }

	void SetDollyVelocityAttenuation( _In_ float va ) { m_DollyVelocityAttenuation = va; }
	float GetDollyVelocityAttenuation() const { return m_DollyVelocityAttenuation; }

    // Setting either of these will not recompute the matrix until the next Update.
    void SetDollyDistanceLimits( _In_ float minDist , _In_ float maxDist );
    float GetMaxDollyDistance() const { return m_DistanceMax; }
    float GetMinDollyDistance() const { return m_DistanceMin; }

    // Matrix generation/accessors
	const XMMATRIX& Update( _In_ float deltaTime );
	const XMMATRIX& GetTransform() const { return m_Transform; }
	const XMMATRIX& GetViewMatrix() const { return m_ViewMatrix; }
    XMVECTOR GetEyePosition() const { return m_EyePosition; }
	XMFLOAT3* GetBoundingBox() { return m_boundingBox; }

	void RayCast(int x, int y, RenderData* pData, XMVECTOR &p1, XMVECTOR &p2);

private:

	XMMATRIX m_Transform;
	XMMATRIX m_ViewMatrix;
	XMMATRIX m_InverseViewMatrix;
	XMVECTOR m_FocusPosition;
    XMVECTOR m_EyePosition;
	XMVECTOR m_FocusPositionVelocity;
	float m_FocusPositionVelocityAttenuation;
    float m_Dolly;
	float m_DollyVelocity;
	float m_DollyVelocityAttenuation;
    float m_DistanceMin;
    float m_DistanceMax;
	float m_Heading;
	float m_HeadingVelocity;
	float m_HeadingVelocityAttenuation;
	float m_Pitch;
	float m_PitchVelocity;
	float m_PitchVelocityAttenuation;

	XMFLOAT3 m_boundingBox[2];
};

#endif

