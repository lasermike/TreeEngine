#include "stdafx.h"
#include "Game.h"
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <directxmath.h>
#include <directxcolors.h>
#include "SceneRoot.h"
#include "Tree.h"
#include "TreeModelGenerator.h"
#include "Primitive.h"
#include <stdio.h>
#include <time.h>

using namespace DirectX;

#define D3D_DEBUG_INFO

//--------------------------------------------------------------------------------------
// Structures
//--------------------------------------------------------------------------------------
struct CBChangeOnResize
{
	XMMATRIX mProjection;
};

Game::Game()
{
	g_pTree = new Tree();
	g_pScene = new SceneRoot();
	g_pPlane = new Primitive();

	g_pScene->AddChild(g_pTree);
	g_pScene->AddChild(g_pPlane);
}


Game::~Game()
{
	/*if (g_pTree)
		delete g_pTree;
	if (g_pScene)
		delete g_pScene;
	if (g_pPlane)
		delete g_pPlane;
		*/
}

//--------------------------------------------------------------------------------------
// Create Direct3D device and swap chain
//--------------------------------------------------------------------------------------
HRESULT Game::InitDevice()
{
	HRESULT hr = S_OK;

	UINT createDeviceFlags = 0;
#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_DRIVER_TYPE driverTypes[] =
	{
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP,
		D3D_DRIVER_TYPE_REFERENCE,
	};
	UINT numDriverTypes = ARRAYSIZE(driverTypes);

	D3D_FEATURE_LEVEL featureLevels[] =
	{
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
	};
	UINT numFeatureLevels = ARRAYSIZE(featureLevels);

	for (UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++)
	{
		g_driverType = driverTypes[driverTypeIndex];
		hr = D3D11CreateDevice(nullptr, g_driverType, nullptr, createDeviceFlags, featureLevels, numFeatureLevels,
			D3D11_SDK_VERSION, &g_pd3dDevice, &g_featureLevel, &g_pImmediateContext);

		if (hr == E_INVALIDARG)
		{
			// DirectX 11.0 platforms will not recognize D3D_FEATURE_LEVEL_11_1 so we need to retry without it
			hr = D3D11CreateDevice(nullptr, g_driverType, nullptr, createDeviceFlags, &featureLevels[1], numFeatureLevels - 1,
				D3D11_SDK_VERSION, &g_pd3dDevice, &g_featureLevel, &g_pImmediateContext);
		}

		if (SUCCEEDED(hr))
			break;
	}
	if (FAILED(hr))
		return hr;

	// Create constant buffer
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(CBChangeOnResize);
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = 0;
	hr = g_pd3dDevice->CreateBuffer(&bd, nullptr, &g_pCBChangeOnResize);
	if (FAILED(hr))
		return hr;

	// Initialize the world matrices
	XMStoreFloat4x4(&g_World, XMMatrixIdentity());

	OnResize();

	return S_OK;
}


HRESULT Game::OnResize()
{
	HRESULT hr = S_OK;

	if (!g_pImmediateContext)
	{
		return S_FALSE;
	}

	// Create width/height dependent objects
	SafeRelease(&g_pDepthStencil);
	SafeRelease(&g_pDepthStencilView);
	SafeRelease(&g_pRenderTargetView);
	SafeRelease(&g_pSwapChain1);
	SafeRelease(&g_pSwapChain);

	RECT rc;
	GetClientRect(g_hWnd, &rc);
	UINT width = rc.right - rc.left;
	UINT height = rc.bottom - rc.top;

	// Initialize the projection matrix
	XMStoreFloat4x4(&g_Projection, XMMatrixPerspectiveFovLH(XM_PIDIV4, width / (FLOAT)height, 0.01f, 100.0f));

	CBChangeOnResize cbChangesOnResize;
	cbChangesOnResize.mProjection = XMMatrixTranspose(XMLoadFloat4x4(&g_Projection));
	g_pImmediateContext->UpdateSubresource(g_pCBChangeOnResize, 0, nullptr, &cbChangesOnResize, 0, 0);

	// Obtain DXGI factory from device (since we used nullptr for pAdapter above)
	IDXGIFactory1* dxgiFactory = nullptr;
	{
		IDXGIDevice* dxgiDevice = nullptr;
		hr = g_pd3dDevice->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
		if (SUCCEEDED(hr))
		{
			IDXGIAdapter* adapter = nullptr;
			hr = dxgiDevice->GetAdapter(&adapter);
			if (SUCCEEDED(hr))
			{
				hr = adapter->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&dxgiFactory));
				adapter->Release();
			}
			dxgiDevice->Release();
		}
	}
	if (FAILED(hr))
		return hr;

	// Check MSAA support
	UINT msaaQuality;
	const UINT msaaCount = 4;
	HR(g_pd3dDevice->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, msaaCount, &msaaQuality));
	if (msaaQuality == 0)
	{
		g_enableMsaa = false;
	}

	// Enable MSAA
	if (g_enableMsaa)
	{
		D3D11_RASTERIZER_DESC rasterDesc;
		rasterDesc.AntialiasedLineEnable = true; // MSA
		rasterDesc.CullMode = D3D11_CULL_BACK;
		rasterDesc.DepthBias = 0;
		rasterDesc.DepthBiasClamp = 0.0f;
		rasterDesc.DepthClipEnable = true;
		rasterDesc.FillMode = D3D11_FILL_SOLID;
		rasterDesc.FrontCounterClockwise = false;
		rasterDesc.MultisampleEnable = true; // MSAA
		rasterDesc.ScissorEnable = false;
		rasterDesc.SlopeScaledDepthBias = 0.0f;
		HR(g_pd3dDevice->CreateRasterizerState(&rasterDesc, &g_rasterState));
		g_pImmediateContext->RSSetState(g_rasterState);
	}

	// Create swap chain
	IDXGIFactory2* dxgiFactory2 = nullptr;
	hr = dxgiFactory->QueryInterface(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgiFactory2));
	if (dxgiFactory2)
	{
		// DirectX 11.1 or later
		hr = g_pd3dDevice->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&g_pd3dDevice1));
		if (SUCCEEDED(hr))
		{
			(void)g_pImmediateContext->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&g_pImmediateContext1));
		}

		DXGI_SWAP_CHAIN_DESC1 sd;
		ZeroMemory(&sd, sizeof(sd));
		sd.Width = width;
		sd.Height = height;
		sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.SampleDesc.Count = g_enableMsaa ? msaaCount : 1;
		sd.SampleDesc.Quality = g_enableMsaa ? msaaQuality - 1 : 0;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.BufferCount = 1;

		hr = dxgiFactory2->CreateSwapChainForHwnd(g_pd3dDevice, g_hWnd, &sd, nullptr, nullptr, &g_pSwapChain1);
		if (SUCCEEDED(hr))
		{
			hr = g_pSwapChain1->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(&g_pSwapChain));
		}

		dxgiFactory2->Release();
	}
	else
	{
		// DirectX 11.0 systems
		DXGI_SWAP_CHAIN_DESC sd;
		ZeroMemory(&sd, sizeof(sd));
		sd.BufferCount = 1;
		sd.BufferDesc.Width = width;
		sd.BufferDesc.Height = height;
		sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.BufferDesc.RefreshRate.Numerator = 60;
		sd.BufferDesc.RefreshRate.Denominator = 1;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.OutputWindow = g_hWnd;
		sd.SampleDesc.Count = g_enableMsaa ? msaaCount : 1;
		sd.SampleDesc.Quality = g_enableMsaa ? msaaQuality - 1 : 0;
		sd.Windowed = TRUE;

		hr = dxgiFactory->CreateSwapChain(g_pd3dDevice, &sd, &g_pSwapChain);
	}

	dxgiFactory->Release();

	if (FAILED(hr))
		return hr;

	// Create a render target view
	ID3D11Texture2D* pBackBuffer = nullptr;
	hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer));
	if (FAILED(hr))
		return hr;

	hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
	pBackBuffer->Release();
	if (FAILED(hr))
		return hr;

	// Create depth stencil texture
	D3D11_TEXTURE2D_DESC descDepth;
	ZeroMemory(&descDepth, sizeof(descDepth));
	descDepth.Width = width;
	descDepth.Height = height;
	descDepth.MipLevels = 1;
	descDepth.ArraySize = 1;
	descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	descDepth.SampleDesc.Count = g_enableMsaa ? msaaCount : 1;
	descDepth.SampleDesc.Quality = g_enableMsaa ? msaaQuality - 1 : 0;
	descDepth.Usage = D3D11_USAGE_DEFAULT;
	descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	descDepth.CPUAccessFlags = 0;
	descDepth.MiscFlags = 0;
	hr = g_pd3dDevice->CreateTexture2D(&descDepth, nullptr, &g_pDepthStencil);
	if (FAILED(hr))
		return hr;

	// Create the depth stencil view
	hr = g_pd3dDevice->CreateDepthStencilView(g_pDepthStencil, 0, &g_pDepthStencilView);
	if (FAILED(hr))
		return hr;

	g_pImmediateContext->OMSetRenderTargets(1, &g_pRenderTargetView, g_pDepthStencilView);

	// Setup the viewport
	D3D11_VIEWPORT vp;
	vp.Width = (FLOAT)width;
	vp.Height = (FLOAT)height;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	g_pImmediateContext->RSSetViewports(1, &vp);

	return S_OK;
}

//--------------------------------------------------------------------------------------
// Clean up the objects we've created
//--------------------------------------------------------------------------------------
void Game::CleanupDevice()
{
	g_pScene->CleanUpDeviceObjects();

	SafeRelease(&g_pImmediateContext);
	SafeRelease(&g_pCBChangeOnResize);
	SafeRelease(&g_rasterState);
	SafeRelease(&g_pDepthStencil);
	SafeRelease(&g_pDepthStencilView);
	SafeRelease(&g_pRenderTargetView);
	SafeRelease(&g_pSwapChain1);
	SafeRelease(&g_pSwapChain);
	SafeRelease(&g_pImmediateContext1);
	SafeRelease(&g_pImmediateContext);
	SafeRelease(&g_pd3dDevice1);
	SafeRelease(&g_pd3dDevice);
}

//--------------------------------------------------------------------------------------
// Render a frame
//--------------------------------------------------------------------------------------
void Game::Render()
{
	// Moved to tree
	if (g_resetTree)
	{
		if (g_currentSeed + 1 > g_seeds.size())
		{
			g_seeds.push_back((unsigned int)time(NULL));
		}

		TreeModelGenerator generator(g_seeds[g_currentSeed]);
		g_pTree->Create(&generator);

		PrimitiveModelGenerator planeGen(PrimitiveType_Box);
		g_pPlane->Create(&planeGen);

		g_pScene->InitGraphics(g_pd3dDevice, g_pImmediateContext);
		
		g_resetTree = false;
	}

	// Update our time
	static float t = 0.0f;
	if (g_driverType == D3D_DRIVER_TYPE_REFERENCE)
	{
		t += (float)XM_PI * 0.0125f;
	}
	else
	{
		ULONGLONG timeCur = GetTickCount64();
		if (g_timeStart == 0)
			g_timeStart = timeCur;
		t = (timeCur - g_timeStart) / 1000.0f;
	}

	// Rotate cube around the origin
	XMStoreFloat4x4(&g_World, XMMatrixRotationY( t ));  //TODO: Uncomment after shadows are working

	//
	// Clear the back buffer
	//
	g_pImmediateContext->ClearRenderTargetView(g_pRenderTargetView, Colors::MidnightBlue);

	//
	// Clear the depth buffer to 1.0 (max depth)
	//
	g_pImmediateContext->ClearDepthStencilView(g_pDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	g_pImmediateContext->VSSetConstantBuffers(1, 1, &g_pCBChangeOnResize);

	// Draw everything
	g_pScene->Render(g_pImmediateContext, XMLoadFloat4x4(&g_World), t);

	//
	// Present our back buffer to our front buffer
	//
	g_pSwapChain->Present(0, 0);
}

void Game::OnKeydown(UINT key)  // WM_KEYDOWN
{
	switch (key)
	{
	case VK_SPACE:
	case VK_RIGHT:
		g_resetTree = true;
		g_currentSeed++;
		break;
	case VK_LEFT:
		if (g_currentSeed > 0)
		{
			g_resetTree = true;
			g_currentSeed--;
		}
		break;
	case '0':
		g_timeStart = 0;
		break;
	}
}