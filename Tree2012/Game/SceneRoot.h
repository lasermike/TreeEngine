#pragma once
#include "pch.h"
#include "Geometry.h"
#include "GeometryGenerator.h"
#include "WorldObject.h"
#include <list>

class RenderManager;
class WorldObject;

class SceneRoot
{
	list<WorldObject*>				_children;

	XMFLOAT3	_boundingBox[2]; // Move to Scene!

public:

	SceneRoot() { };
	~SceneRoot(); 

	HRESULT InitGraphics(RenderManager& renderManager); 
	HRESULT CleanUpDeviceObjects();

	HRESULT Update(RenderManager& renderManager);
	HRESULT Render(RenderManager& renderManager);

	void AddChild(WorldObject* obj)
	{
		_children.push_back(obj);
	}

	const list<WorldObject*>& Children() { return _children; }

	XMVECTOR GetExtents(Extent extent);
	XMFLOAT3* GetBoundingBox() { return _boundingBox; }

    UINT32 GetMaxInstances();
};

