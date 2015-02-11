#pragma once

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

struct RenderData
{
	RenderPass			pass;
	XMFLOAT4X4			world;
	XMFLOAT4X4          projection;
	XMFLOAT4X4			view;
	XMFLOAT4			eyePos;

	DirectionalLight	dirLights[1];
	float				time;
	BoundingSphere		mSceneBounds;

	static const int SMapSize = 2048;
	ShadowMap*			pShadowMap;
	XMFLOAT4X4			lightView;
	XMFLOAT4X4			lightProj;
	XMFLOAT4X4			shadowTransform;

	RenderData() : pass(RegularPass), time(0.0f), pShadowMap(nullptr)
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
