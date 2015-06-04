#include "pch.h"
#include "Geometry.h"

HRESULT Geometry::InitGraphics(RenderManager& renderManager)
{
	CleanUpDeviceObjects();

	// Create constants for per frame 
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(CBChangesPerObject);
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = 0;
	HRR(renderManager.GetDevice()->CreateBuffer(&bd, nullptr, &_pCBChangesPerObject));

	return S_OK;
}