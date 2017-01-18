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
#if defined(TREE3D12)
    ShadowMap(XSF::D3DDevice* device, D3D12_CPU_DESCRIPTOR_HANDLE shadowMapSrvCpu, D3D12_GPU_DESCRIPTOR_HANDLE shadowMapSrvGpu,
              D3D12_CPU_DESCRIPTOR_HANDLE shadowMapDsvCpu, UINT width, UINT height);
#else
	ShadowMap(XSF::D3DDevice* device, UINT width, UINT height);
#endif

	~ShadowMap();

#if defined(TREE3D12)

    D3D12_CPU_DESCRIPTOR_HANDLE DepthMapSRV() { return mDepthMapSRVCpu; }
    D3D12_GPU_DESCRIPTOR_HANDLE DepthMapSRVGpu() { return mDepthMapSRVGpu; }
    ID3D12Resource* DepthMapBuffer() { return mDepthMap; }

	void BindDsvAndSetNullRenderTarget(ID3D12GraphicsCommandList* cmdList);
#else
	ID3D11ShaderResourceView* DepthMapSRV() { return mDepthMapSRV; }
	ID3D11Texture2D* DepthMapBuffer() { return mDepthMap; }

	void BindDsvAndSetNullRenderTarget(XSF::D3DDeviceContext* dc);
#endif

    UINT Width() { return mWidth; }
    UINT Height() { return mHeight; }
    
    static DXGI_FORMAT Format() { return DXGI_FORMAT_D32_FLOAT; }
    static DXGI_FORMAT FormatTypeless() { return DXGI_FORMAT_R32_TYPELESS; }

    //static DXGI_FORMAT Format() { return DXGI_FORMAT_D24_UNORM_S8_UINT; }
    //static DXGI_FORMAT FormatTypeless() { return DXGI_FORMAT_R24G8_TYPELESS; }

private:
	ShadowMap(const ShadowMap& rhs);
	ShadowMap& operator=(const ShadowMap& rhs);

private:
	UINT mWidth;
	UINT mHeight;

#if defined(TREE3D12)
    D3D12_CPU_DESCRIPTOR_HANDLE mDepthMapSRVCpu;
    D3D12_GPU_DESCRIPTOR_HANDLE mDepthMapSRVGpu;
    D3D12_CPU_DESCRIPTOR_HANDLE mDepthMapDSV;
	ID3D12Resource* mDepthMap;

	D3D12_VIEWPORT mViewport;
    D3D12_RECT mScissorRect;
#else
	ID3D11ShaderResourceView* mDepthMapSRV;
	ID3D11DepthStencilView* mDepthMapDSV;
	ID3D11Texture2D* mDepthMap;

	D3D11_VIEWPORT mViewport;
#endif

};

#endif // SHADOW_MAPPER_H