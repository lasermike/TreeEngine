#include "pch.h"
#include "SceneRoot.h"
#include "GeometryGenerator.h"
#include "ShadowMap.h"

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

HRESULT SceneRoot::InitGraphics(XSF::D3DDevice* device, XSF::D3DDeviceContext* pImmediateContext)
{
	for (auto i : _children)
	{
		HRR(i->InitGraphics(device, pImmediateContext));
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


