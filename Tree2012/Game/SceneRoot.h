#pragma once
#include "pch.h"
#include "Geometry.h"
#include "GeometryGenerator.h"
#include "WorldObject.h"
#include <list>

class RenderManager;

class SceneRoot
{
	RenderManager* m_renderManager; // Weak

	list<WorldObject*>				_children;

	XMFLOAT3	_boundingBox[2]; // Move to Scene!


public:

	SceneRoot() : m_renderManager(nullptr) { };
	SceneRoot(RenderManager* renderManager) : m_renderManager(renderManager) { }
	~SceneRoot(); 

	HRESULT InitGraphics(XSF::D3DDevice* device, XSF::D3DDeviceContext* pImmediateContext); // Long term should go away
	HRESULT CleanUpDeviceObjects();


	HRESULT Update(XSF::D3DDeviceContext* pImmediateContext, RenderData* pRenderData);
	HRESULT Render(XSF::D3DDeviceContext* pImmediateContext, RenderData* pRenderData);

	void AddChild(WorldObject* obj)
	{
		_children.push_back(obj);
	}

	const list<WorldObject*>& Children() { return _children; }

	XMVECTOR GetExtents(Extent extent);
	XMFLOAT3* GetBoundingBox() { return _boundingBox; }

    UINT32 GetMaxInstances();
};

