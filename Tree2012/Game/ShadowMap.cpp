//***************************************************************************************
// ShadowMap.cpp by Frank Luna (C) 2011 All Rights Reserved.
//***************************************************************************************

#include "pch.h"
#include "ShadowMap.h"

#if defined(TREE3D12)

ShadowMap::ShadowMap(XSF::D3DDevice* device, D3D12_CPU_DESCRIPTOR_HANDLE shadowMapSrvCpu, D3D12_GPU_DESCRIPTOR_HANDLE shadowMapSrvGpu,
                     D3D12_CPU_DESCRIPTOR_HANDLE shadowMapDsvCpu, UINT width, UINT height)
    : mWidth(width), mHeight(height), mDepthMapSRVCpu(shadowMapSrvCpu), mDepthMapSRVGpu(shadowMapSrvGpu), mDepthMapDSV(shadowMapDsvCpu), mDepthMap(0)
#else
ShadowMap::ShadowMap(XSF::D3DDevice* device, UINT width, UINT height)
    : mWidth(width), mHeight(height), mDepthMapSRV(), mDepthMapDSV(), mDepthMap(0)
#endif
{
    mViewport.TopLeftX = 0.0f;
    mViewport.TopLeftY = 0.0f;
    mViewport.Width    = static_cast<float>(width);
    mViewport.Height   = static_cast<float>(height);
    mViewport.MinDepth = 0.0f;
    mViewport.MaxDepth = 1.0f;


	// Use typeless format because the 
    // DSV is going to interpret the bits as DXGI_FORMAT_D24_UNORM_S8_UINT whereas the 
    // SRV is going to interpret the bits as DXGI_FORMAT_R24_UNORM_X8_TYPELESS.
#if defined(TREE3D12)
	D3D12_RESOURCE_DESC texDesc = {};

	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	texDesc.DepthOrArraySize = 1;

    D3D12_CLEAR_VALUE optClear;
    optClear.Format = Format();
    optClear.DepthStencil.Depth = 1.0f;
    optClear.DepthStencil.Stencil = 0;

#else
	D3D11_TEXTURE2D_DESC texDesc;
	texDesc.ArraySize = 1;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;
#endif

    texDesc.Width     = mWidth;
    texDesc.Height    = mHeight;
    texDesc.MipLevels = 1;
    texDesc.Format    = FormatTypeless();
    texDesc.SampleDesc.Count   = 1;  
    texDesc.SampleDesc.Quality = 0;  


#if defined(TREE3D12)

	HR(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
		&optClear,
		IID_PPV_ARGS(&mDepthMap)));
#else
    HR(device->CreateTexture2D(&texDesc, 0, &mDepthMap));
#endif

	SetDebugName(mDepthMap, "ShadowMap::mDepthMap");

#if defined(TREE3D12)
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = Format();
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;
    device->CreateDepthStencilView(mDepthMap, &dsvDesc, mDepthMapDSV);
	//SetDebugName(mDepthMapDSV, "ShadowMap::mDepthMapDSV");

	// Describe and create a SRV for the texture.
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT; //FormatTypeless();
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(mDepthMap, &srvDesc, mDepthMapSRVCpu);

    mScissorRect = { 0, 0, (int)width, (int)height };

#else
	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = 0;
	dsvDesc.Format = Format(); //DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Texture2D.MipSlice = 0;
	HR(device->CreateDepthStencilView(mDepthMap, &dsvDesc, &mDepthMapDSV));
	SetDebugName(mDepthMapDSV, "ShadowMap::mDepthMapDSV");

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT; //DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;
    HR(device->CreateShaderResourceView(mDepthMap, &srvDesc, &mDepthMapSRV));
	SetDebugName(mDepthMapSRV, "ShadowMap::mDepthMapSRV");
#endif

}

ShadowMap::~ShadowMap()
{
    SafeRelease(&mDepthMap);
}


#if defined(TREE3D12)
void ShadowMap::BindDsvAndSetNullRenderTarget(ID3D12GraphicsCommandList* cmdList)
{
	cmdList->RSSetViewports(1, &mViewport);
    cmdList->RSSetScissorRects(1, &mScissorRect);

	// Set null render target because we are only going to draw to depth buffer.
	// Setting a null render target will disable color writes.
	//CD3DX12_CPU_DESCRIPTOR_HANDLE* renderTargets[1] = {0};
	//renderTargets[0] = pTestRTV;

    cmdList->OMSetRenderTargets(0, nullptr, false, &mDepthMapDSV);
    
	cmdList->ClearDepthStencilView(mDepthMapDSV, D3D12_CLEAR_FLAG_DEPTH, 1.0, 0, 0, nullptr);
}

#else
void ShadowMap::BindDsvAndSetNullRenderTarget(XSF::D3DDeviceContext* dc)
{
	dc->RSSetViewports(1, &mViewport);

	// Set null render target because we are only going to draw to depth buffer.
	// Setting a null render target will disable color writes.
	ID3D11RenderTargetView* renderTargets[1] = { 0 };
	renderTargets[0] = nullptr;

	dc->OMSetRenderTargets(1, renderTargets, mDepthMapDSV);

	dc->ClearDepthStencilView(mDepthMapDSV, D3D11_CLEAR_DEPTH, 1.0, 0);
}

#endif