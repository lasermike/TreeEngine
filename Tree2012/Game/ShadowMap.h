//***************************************************************************************
// ShadowMap.h by Frank Luna (C) 2011 All Rights Reserved.
//
// Helper class for implementing shadows via shadow mapping algorithm.
//***************************************************************************************

#ifndef SHADOW_MAPPER_H
#define SHADOW_MAPPER_H

//#include "d3dUtil.h"
//#include "Camera.h"

class ShadowMap
{
public:
	ShadowMap(XSF::D3DDevice* device, UINT width, UINT height);
	~ShadowMap();

	ID3D11ShaderResourceView* DepthMapSRV();
	ID3D11Texture2D* DepthMapBuffer();

	void BindDsvAndSetNullRenderTarget(XSF::D3DDeviceContext* dc, ID3D11RenderTargetView* pTestRTV);

private:
	ShadowMap(const ShadowMap& rhs);
	ShadowMap& operator=(const ShadowMap& rhs);

private:
	UINT mWidth;
	UINT mHeight;

	ID3D11ShaderResourceView* mDepthMapSRV;
	ID3D11DepthStencilView* mDepthMapDSV;
    ID3D11Texture2D* mDepthMap;

	D3D11_VIEWPORT mViewport;
};

#endif // SHADOW_MAPPER_H