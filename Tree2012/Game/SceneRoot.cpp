#include "pch.h"
#include "SceneRoot.h"
#include "GeometryGenerator.h"
#include "ShadowMap.h"
#include "RenderManager.h"
#include "ThreadPool.h"

SceneRoot::~SceneRoot()
{
	for (auto i = _children.begin(); i != _children.end(); i++)
	{
		SafeDelete(&(*i));
	}
}

XMVECTOR SceneRoot::GetExtents(Extent extent)
{
	XSF_ASSERT(extent == TOP);

	auto i = _children.begin() ;
	XMVECTOR retval = (*i)->GetExtents(extent);
	i++;
	for (; i != _children.end(); i++)
	{
		XMVECTOR cur = (*i)->GetExtents(extent);
		if (XMVectorGetY(cur) > XMVectorGetY(retval)) 
		{
			retval = cur;
		}
	}

	return retval; 
}

UINT32 SceneRoot::GetMaxInstances()
{
    // Determine number of instances
	unsigned int numInstances = 0;
	for (auto j : _children)
	{
		numInstances += j->GetMaxInstances();
	}

    return numInstances;
}

HRESULT SceneRoot::InitGraphics(RenderManager& renderManager)
{
	for (auto i : _children)
	{
		HRR(i->InitGraphics(renderManager));
	}

	return S_OK;
}

HRESULT SceneRoot::CleanUpDeviceObjects()
{
    for (auto child : _children)
    {
        child->CleanUpDeviceObjects();
    }

    return S_OK;
}

void SceneRoot::DeleteAllChildren()
{
	CleanUpDeviceObjects();
	for (auto child : _children)
	{
		delete child;
	}
	_children.clear();
}


HRESULT SceneRoot::Update(IRenderFrame& renderFrame, ThreadPool& threadPool)
{
	HRESULT hr = S_OK;

	UINT numObjs = (UINT) _children.size();
	UINT objsPerThread = std::max(1U, numObjs / threadPool.GetNumWorkers());

	WorkData threadData = { 0, 0 /*first*/, objsPerThread /*end*/, &renderFrame }; 

	std::list<WorkData> workData;

	for (UINT i = 0; i < threadPool.GetNumWorkers() && threadData.end <= numObjs; i++)
	{
		if (i + 1 == threadPool.GetNumWorkers()) // Make sure the last iteration has all remaining objs
		{
			threadData.end = numObjs;
		}

		workData.push_back(threadData);
		threadPool.enqueue([this](WorkData* data)
						   {
							   UINT num = 0;
							   for (auto c : _children)
							   {
								   if (num >= data->start && num < data->end)
								   {
									   c->ComputeConstants((IRenderFrame*) data->param1);
								   }
								   num++;
							   }
						   }, &*workData.rbegin());	
		threadData.start  += objsPerThread;
		threadData.end  += objsPerThread;
	}

	threadPool.WaitTilDone();

	_boundingBox[0] = _boundingBox[1] = XMFLOAT3(0,0,0);
	XMVECTOR bbmin = XMLoadFloat3(&_boundingBox[0]);
	XMVECTOR bbmax = XMLoadFloat3(&_boundingBox[1]);
	
	for (WorldObject* c : _children)
	{
		bbmin = XMVectorMin(bbmin, XMLoadFloat3(&c->GetBoundingBox()[0]));
		bbmax = XMVectorMax(bbmax, XMLoadFloat3(&c->GetBoundingBox()[1]));
	}

	XMStoreFloat3(&_boundingBox[0], bbmin);
	XMStoreFloat3(&_boundingBox[1], bbmax);

	return hr;
}
