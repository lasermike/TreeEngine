#include "PrimitiveGeometry.h"
#include "DDSTextureLoader.h"



PrimitiveGeometry::PrimitiveGeometry(PrimitiveModel* model) : Geometry()
{
	_model = model;
	_pTextureRV = nullptr;
	_pSamplerLinear = nullptr;

	_groundMaterial.Ambient = XMFLOAT4(.5, .5, .5, 1);
	_groundMaterial.Diffuse = XMFLOAT4(.1, .7, .1, 1);
	_groundMaterial.Specular = XMFLOAT4(.8, .8, .8, 16.0f);
	_groundMaterial.Reflect = XMFLOAT4(0, 0, 0, 1);
}

PrimitiveGeometry::~PrimitiveGeometry()
{
}

HRESULT PrimitiveGeometry::InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext)
{	
	HR(CleanUpDeviceObjects());
	Geometry::InitGraphics(device, pImmediateContext);

	// Load the Texture
	HR(CreateDDSTextureFromFile(device, L"snow.dds", nullptr, &_pTextureRV));

	// Create the sample state
	D3D11_SAMPLER_DESC sampDesc;
	ZeroMemory(&sampDesc, sizeof(sampDesc));
	sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampDesc.MinLOD = 0;
	sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	HRESULT hr = device->CreateSamplerState(&sampDesc, &_pSamplerLinear);
	if (FAILED(hr))
		return hr;

	return S_OK;
}
HRESULT PrimitiveGeometry::CleanUpDeviceObjects()
{
	HR(Geometry::CleanUpDeviceObjects());
	SafeRelease(&_pTextureRV);
	SafeRelease(&_pSamplerLinear);

	return S_OK;
}
