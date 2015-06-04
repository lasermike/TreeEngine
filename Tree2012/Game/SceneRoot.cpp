#include "pch.h"
#include "SceneRoot.h"
#include "GeometryGenerator.h"
#include "ShadowMap.h"
#include "RenderManager.h"

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
    for (auto i : _children)
    {
        i->CleanUpDeviceObjects();
    }

    return S_OK;
}

HRESULT SceneRoot::Update(RenderManager& renderManager)
{
	_boundingBox[0] = _boundingBox[1] = XMFLOAT3(0,0,0);
	XMVECTOR bbmin = XMLoadFloat3(&_boundingBox[0]);
	XMVECTOR bbmax = XMLoadFloat3(&_boundingBox[1]);
	
	HRESULT hr = renderManager.Update(_children);

	for (auto i : _children)
	{
		bbmin = XMVectorMin(bbmin, XMLoadFloat3(&i->GetBoundingBox()[0]));
		bbmax = XMVectorMax(bbmax, XMLoadFloat3(&i->GetBoundingBox()[1]));
	}

	XMStoreFloat3(&_boundingBox[0], bbmin);
	XMStoreFloat3(&_boundingBox[1], bbmax);

	return hr;
}

HRESULT SceneRoot::Render(RenderManager& renderManager)
{
	return renderManager.Render(_children);
}


