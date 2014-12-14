#include "TreeModel.h"
#include "CommonStuff.h"
#include "TreeModelGenerator.h"


using namespace DirectX;

#include "Tree.h"

Tree::Tree(void) :_geometry()
{
	_model = nullptr;
}


Tree::~Tree(void)
{
	if (_model)
	{
		delete _model ;
	}
}

void Tree::Create(TreeModelGenerator* generator)
{
	_model = generator->Create();
}

HRESULT Tree::InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext)
{
	HR(CleanUpDeviceObjects());
	_geometry = new TreeGeometry(_model);
	HR(_geometry->InitGraphics(device, pImmediateContext));
	return S_OK;
}

HRESULT Tree::CleanUpDeviceObjects()
{
	if (_geometry)
	{
		delete _geometry;
		_geometry = nullptr;
	}
	return S_OK;
}

HRESULT Tree::Render(ID3D11DeviceContext* pImmediateContext, DirectX::XMMATRIX* world, XMVECTOR eyePos, float time)
{
	_geometry->Render(pImmediateContext, world, eyePos, time);
	return S_OK;
}
