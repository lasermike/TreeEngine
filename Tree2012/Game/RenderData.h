#pragma once

struct BoundingSphere
{
	BoundingSphere() : Center(0.0f, 0.0f, 0.0f), Radius(0.0f) {}
	XMFLOAT3 Center;
	float Radius;
};

struct RenderData
{
	XMFLOAT4X4			world;
	XMFLOAT4X4          projection;
	XMFLOAT4X4			view;
	XMFLOAT4			eyePos;


	DirectionalLight	dirLights[1];
	float				time;
	BoundingSphere		mSceneBounds;

	//static const int SMapSize = 2048;
	//ShadowMap* mSmap;
	XMFLOAT4X4			lightView;
	XMFLOAT4X4			lightProj;
	XMFLOAT4X4			shadowTransform;
};
