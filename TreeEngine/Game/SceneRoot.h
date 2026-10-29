#pragma once
#include "pch.h"
#include "GeometryGenerator.h"
#include "WorldObject.h"
#include <list>
#include <vector>

class ThreadPool;
class RenderManager;
class WorldObject;

class SceneRoot
{
	list<WorldObject*>				_children;

	XMFLOAT3	_boundingBox[2]; // Move to Scene!

	// Update order: sources first, then instances, which read their source's per-frame results.
	std::vector<WorldObject*>		_updatePhases[2];

	HRESULT ComputeConstants(const std::vector<WorldObject*>& objects, IRenderFrame& renderFrame, ThreadPool& threadPool, bool synchronous);

public:

	SceneRoot() { };
	~SceneRoot(); 

	void AppendGeometry(GeometryBufferData& geometryData);
	HRESULT InitGraphics(RenderManager& renderManager); 
	HRESULT CleanUpDeviceObjects();

	HRESULT Update(IRenderFrame& renderFrame, ThreadPool& threadPool, bool synchronous = false);

	void AddChild(WorldObject* obj)
	{
		_children.push_back(obj);
	}

	void DeleteAllChildren();

	const list<WorldObject*>& Children() { return _children; }

	XMVECTOR GetExtents(Extent extent);
	XMFLOAT3* GetBoundingBox() { return _boundingBox; }

    UINT32 GetMaxInstances();
    UINT32 GetMaxDrawnInstances();
};
