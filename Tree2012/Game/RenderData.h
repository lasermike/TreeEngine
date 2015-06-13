#pragma once
#include "Materials.h"

class ShadowMap;

struct BoundingSphere
{
	BoundingSphere() : Center(0.0f, 0.0f, 0.0f), Radius(0.0f) {}
	XMFLOAT3 Center;
	float Radius;
};

enum RenderPass
{
	RegularPass,
	ShadowMapPass
};

struct ProjectionData
{
	int					screenWidth;
	int					screenHeight;
	float				fov;
	float				nearClippingPlane;
	float				farClippingPlane;
};

__declspec(align(16)) 
struct RenderData
{
	RenderPass			pass;
	float				time;
	UINT				frame;

    ProjectionData      projectionData;

	XMFLOAT4X4			world;          // Needed?
	XMFLOAT4X4          projection;
	XMFLOAT4X4			view;           
	XMVECTOR			eyePos;

	DirectionalLight	dirLights[1];
	BoundingSphere		mSceneBounds;

	static const int	SMapWidth = 2048;
	static const int	SMapHeight = 2048;
	ShadowMap*			pShadowMap;
	XMFLOAT4X4			lightView;
	XMFLOAT4X4			lightProj;
	XMFLOAT4X4			shadowTransform;

	RenderData() : pass(RegularPass), time(0.0f), frame(0), pShadowMap(nullptr)
	{
		XMStoreFloat4x4(&world, XMMatrixIdentity());
		XMStoreFloat4x4(&view, XMMatrixIdentity());
		XMStoreFloat4x4(&projection, XMMatrixIdentity());
		XMStoreFloat4x4(&lightView, XMMatrixIdentity());
		XMStoreFloat4x4(&lightProj, XMMatrixIdentity());
		XMStoreFloat4x4(&shadowTransform, XMMatrixIdentity());
		memset(&dirLights, 0, sizeof(DirectionalLight) * _countof(dirLights));
	}
};
