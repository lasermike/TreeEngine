#include "pch.h"
#include "PrimitiveGeometry.h"
#include "DDSTextureLoader.h"

PrimitiveGeometry::PrimitiveGeometry(PrimitiveModel* model) : Geometry()
{
	_model = model;
	_pTextureRV = nullptr;

	_groundMaterial.Ambient = XMFLOAT4(.5, .5, .5, 1);
	_groundMaterial.Diffuse = XMFLOAT4(0, .6f, 0, 1);
	_groundMaterial.Specular = XMFLOAT4(.3f, .3f, .3f, 4.0f);
	_groundMaterial.Reflect = XMFLOAT4(0, 0, 0, 1);
	_groundMaterial.flags.y = 0; //1 for textured; 
}

PrimitiveGeometry::~PrimitiveGeometry()
{
}

HRESULT PrimitiveGeometry::InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext)
{	
	HRR(CleanUpDeviceObjects());
	Geometry::InitGraphics(device, pImmediateContext);

	// Load the Texture
	HRR(CreateDDSTextureFromFile(device, L"snow.dds", nullptr, &_pTextureRV));

	return S_OK;
}
HRESULT PrimitiveGeometry::CleanUpDeviceObjects()
{
	HRR(Geometry::CleanUpDeviceObjects());
	SafeRelease(&_pTextureRV);

	return S_OK;
}
