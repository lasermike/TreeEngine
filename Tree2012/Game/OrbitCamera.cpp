//--------------------------------------------------------------------------------------
// OrbitCamera.h
//
// A camera that's useful for 3D object inspection.
//
// Xbox Advanced Technology Group (ATG).
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include <pch.h>
#include "OrbitCamera.h"
#include "MathHelper.h"
#include "RenderData.h"

using namespace XboxSampleFramework;

static const float c_MinPitchAngle = -XM_PI * 0.49f;
static const float c_MaxPitchAngle =  XM_PI * 0.49f;

OrbitCamera::OrbitCamera(void)
	: m_Dolly(0)
    , m_DollyVelocity(0)
    , m_DollyVelocityAttenuation(20)
	, m_Heading(0)
	, m_HeadingVelocity(0)
	, m_HeadingVelocityAttenuation(60)
	, m_Pitch(0)
	, m_PitchVelocity(0)
	, m_PitchVelocityAttenuation(60)
    , m_DistanceMin(1.0f)
    , m_DistanceMax(FLT_MAX)
{
	m_EyePosition = XMVectorZero();
	m_FocusPosition = XMVectorZero();
	m_FocusPositionVelocity = XMVectorZero();
	m_Transform = XMMatrixIdentity();
	m_ViewMatrix = XMMatrixIdentity();
	m_boundingBox[0] = XMFLOAT3(0,0,0);
	m_boundingBox[1] = XMFLOAT3(0,0,0);

	Update(0);
}

OrbitCamera::~OrbitCamera(void)
{
}

void OrbitCamera::SetDolly( float v ) 
{ 
	m_Dolly = MathHelper::Clamp( v, m_DistanceMin, m_DistanceMax );
}

void OrbitCamera::AddDollyVelocity( float v ) 
{ 
	m_DollyVelocity = m_DollyVelocity + v; 
}

void OrbitCamera::SetHeading( float h )
{
	m_Heading = MathHelper::WrapHalfPi(h);
}

void OrbitCamera::SetPitch( float p )
{
	m_Pitch = MathHelper::Clamp(p, -c_MaxPitchAngle, c_MaxPitchAngle);
}

void OrbitCamera::SetDollyDistanceLimits( float mins, float maxs )
{
	if ( mins > maxs ) MathHelper::Swap( mins, maxs );
    m_DistanceMin = mins;
    m_DistanceMax = maxs;
    m_Dolly = MathHelper::Clamp( m_Dolly, mins, maxs );
}

const XMMATRIX& OrbitCamera::Update( _In_ float delta )
{
    float velocityScaleFactor = 1.0f;
    if ( m_Dolly > 0 && m_Dolly < FLT_MAX ) 
    {
        float curve = (m_Dolly - m_DistanceMin);

        velocityScaleFactor = 0.1f + sqrtf(curve) * 0.9f;
    }

	m_Dolly += m_DollyVelocity * velocityScaleFactor * delta;
	m_DollyVelocity *= XMMax(0.0f, 1 - (delta * m_DollyVelocityAttenuation));

	m_Pitch += m_PitchVelocity * delta;
	m_PitchVelocity *= XMMax(0.0f, 1 - (delta * m_PitchVelocityAttenuation));

	m_Heading += m_HeadingVelocity * delta;
	m_HeadingVelocity *= XMMax(0.0f, 1 - (delta * m_HeadingVelocityAttenuation));

	m_Heading = MathHelper::WrapHalfPi(m_Heading);
	m_Pitch = MathHelper::Clamp(m_Pitch, c_MinPitchAngle, c_MaxPitchAngle);
	m_Dolly = MathHelper::Clamp(m_Dolly, m_DistanceMin, m_DistanceMax);

	XMMATRIX MRotation = XMMatrixMultiply(
        XMMatrixRotationX(m_Pitch),
	    XMMatrixRotationY(m_Heading) );

    XMVECTOR position = XMVector4Transform( 
        XMVectorSet( 0, 0, m_Dolly, 1 ),
        MRotation );

	m_FocusPosition += m_FocusPositionVelocity * delta;
	m_FocusPositionVelocity *= XMMax(0.0f, 1 - (delta * m_PitchVelocityAttenuation));
	 

    m_EyePosition = XMVectorAdd( m_FocusPosition, position );

    // If the eye position == focus position, set a zero matrix
    if ( XMVector3Equal( m_EyePosition, m_FocusPosition ) )
    {
        m_ViewMatrix = XMMatrixScaling(0, 0, 0);
    }
    else
    {
        m_ViewMatrix = XMMatrixLookAtLH( m_EyePosition, m_FocusPosition, XMVectorSet( 0, 1, 0, 0 ) );
    }

	XMVECTOR determinant = XMMatrixDeterminant(m_ViewMatrix);
	m_Transform = XMMatrixInverse(&determinant, m_ViewMatrix);

    return m_ViewMatrix;
}

// Find the points where ray intersects camera, given screen point near clipping plane (p1) and far clipping plane (p2)
void OrbitCamera::RayCast(int x, int y, RenderData* pData, XMVECTOR &p1, XMVECTOR &p2)
{
//#define	NEAR			10.0f
//#define FAR				4000.0f
//#define	FOV				0.8f
//#define	WIDTH			640.0f
//#define	HEIGHT			480.0f
//#define	WIDTH_DIV_2		(WIDTH*0.5f)
//#define	HEIGHT_DIV_2	(HEIGHT*0.5f)
//#define ASPECT			1.3333f

	float dx,dy;

	dx=tanf(pData->fov * 0.5f) * (x / pData->screenWidth / 2.0f - 1.0f) / (pData->screenWidth / pData->screenHeight);
	dy=tanf(pData->fov * 0.5f) * (1.0f-y / pData->screenHeight / 2.0f);

	p1 = XMVectorSet(dx * pData->nearClippingPlane, dy * pData->nearClippingPlane, pData->nearClippingPlane, 1.0f);
	p2 = XMVectorSet(dx * pData->farClippingPlane, dy * pData->farClippingPlane, pData->farClippingPlane, 1.0f);
	
	p1 = XMVector4Transform(p1, m_Transform);
	p2 = XMVector4Transform(p2, m_Transform);
}

void OrbitCamera::FocusOnBoundingBox(
    _In_reads_(numCorners) const XMFLOAT3* pBoxCorners,
    _In_ UINT numCorners )
{
	m_boundingBox[0] = pBoxCorners[0];
	m_boundingBox[1] = pBoxCorners[1];

    XMVECTOR mins = XMVectorReplicate(  FLT_MAX );
    XMVECTOR maxs = XMVectorReplicate( -FLT_MAX );

    for ( UINT i = 0; i < numCorners; ++i )
    {
        XMVECTOR corner = XMLoadFloat3( &pBoxCorners[i] );
        mins = XMVectorMin( mins, corner );
        maxs = XMVectorMax( maxs, corner ); // changed from mins, corner !!
    }

    const XMVECTOR  vTgt = ( mins + maxs ) * 0.5f;
    const XMVECTOR  vMaxDiff = XMVectorMax( maxs - vTgt, vTgt - mins );
    XMFLOAT3    tmp;
    XMStoreFloat3( &tmp, vMaxDiff );

    m_FocusPosition = vTgt;

    SetDollyDistanceLimits( std::max( tmp.x, std::max( tmp.y, tmp.z ) ), FLT_MAX );
}
