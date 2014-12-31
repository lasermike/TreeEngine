#include "pch.h"
#include "Game.h"
#include <directxcolors.h>
#include "SceneRoot.h"
#include "Tree.h"
#include "TreeModelGenerator.h"
#include "Primitive.h"
#include <stdio.h>
#include <time.h>

using namespace DirectX;

#ifndef _XBOX_ONE
using namespace Windows::Graphics::Display;
#endif 

#define D3D_DEBUG_INFO

//--------------------------------------------------------------------------------------
// Structures
//--------------------------------------------------------------------------------------
struct CBChangeOnResize
{
	XMFLOAT4X4 mProjection;
};

Game::Game()
{
	_window = nullptr;
	_driverType = D3D_DRIVER_TYPE_NULL;
	_featureLevel = D3D_FEATURE_LEVEL_11_0;
	_pd3dDevice = nullptr;
	_pd3dDevice1 = nullptr;
	_pImmediateContext = nullptr;
	_pImmediateContext1 = nullptr;
	_pSwapChain = nullptr;
	_pSwapChain1 = nullptr;
	_pRenderTargetView = nullptr;
	_pDepthStencil = nullptr;
	_pDepthStencilView = nullptr;
	_rasterState = nullptr;
#ifdef _XBOX_ONE
	_enableMsaa = true; 
#else
	_enableMsaa = false; // TODO: disabled for windows store
#endif
	_timeStart = 0;
	_currentSeed = 0;
	_resetTree = true;
	_pCBChangeOnResize = nullptr;

	XSF::SetContentFileRoot();

	_pTree = new Tree();
	_pScene = new SceneRoot();
	_pPlane = new Primitive();

	_pScene->AddChild(_pTree);
	_pScene->AddChild(_pPlane);
}

Game::~Game()
{
	if (_pTree)
		delete _pTree;
	if (_pScene)
		delete _pScene;
	if (_pPlane)
		delete _pPlane;
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

#if defined(_XBOX_ONE) && defined(PROFILE) 
    createDeviceFlags |= D3D11_CREATE_DEVICE_INSTRUMENTED;
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
		Microsoft::WRL::ComPtr<ID3D11Device> device;
	    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext;

		_driverType = driverTypes[driverTypeIndex];
		hr = D3D11CreateDevice(nullptr, _driverType, nullptr, createDeviceFlags, featureLevels, numFeatureLevels,
			D3D11_SDK_VERSION, &device, &_featureLevel, &d3dContext);

		HR(device.Get()->QueryInterface( __uuidof(_pd3dDevice), reinterpret_cast<void**>(&_pd3dDevice) ) );
		HR(d3dContext.Get()->QueryInterface( __uuidof(_pImmediateContext), reinterpret_cast<void**>(&_pImmediateContext) ) );

		/*if (hr == E_INVALIDARG)
		{
			// DirectX 11.0 platforms will not recognize D3D_FEATURE_LEVEL_11_1 so we need to retry without it
			hr = D3D11CreateDevice(nullptr, _driverType, nullptr, createDeviceFlags, &featureLevels[1], numFeatureLevels - 1,
				D3D11_SDK_VERSION, &_pd3dDevice, &_featureLevel, &_pImmediateContext);
		}*/

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
	hr = _pd3dDevice->CreateBuffer(&bd, nullptr, &_pCBChangeOnResize);
	if (FAILED(hr))
		return hr;

	// Initialize the world matrices
	XMStoreFloat4x4(&_World, XMMatrixIdentity());

	OnResize();

	return S_OK;
}


HRESULT Game::OnResize()
{
	HRESULT hr = S_OK;

	if (!_pImmediateContext)
	{
		return S_FALSE;
	}

	// Create width/height dependent objects
	SafeRelease(&_pDepthStencil);
	SafeRelease(&_pDepthStencilView);
	SafeRelease(&_pRenderTargetView);
	SafeRelease(&_pSwapChain1);
	SafeRelease(&_pSwapChain);


	// Calculate the necessary swap chain and render target size in pixels.
#ifdef _XBOX_ONE
	UINT windowWidth = 1920;
	UINT windowHeight = 1080;
#else
	auto windowBounds = _window->Bounds;
	UINT windowWidth = ConvertDipsToPixels(windowBounds.Width);
	UINT windowHeight = ConvertDipsToPixels(windowBounds.Height);
#endif
	// Initialize the projection matrix
	XMStoreFloat4x4(&_Projection, XMMatrixPerspectiveFovLH(XM_PIDIV4, windowWidth / (float)windowHeight, 0.01f, 100.0f));

	CBChangeOnResize cbChangesOnResize;
	XMStoreFloat4x4(&cbChangesOnResize.mProjection, XMMatrixTranspose(XMLoadFloat4x4(&_Projection)));
	_pImmediateContext->UpdateSubresource(_pCBChangeOnResize, 0, nullptr, &cbChangesOnResize, 0, 0);

	// Obtain DXGI factory from device (since we used nullptr for pAdapter above)
	IDXGIFactory1* dxgiFactory = nullptr;
	{
		IDXGIDevice* dxgiDevice = nullptr;
		hr = _pd3dDevice->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
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
	const UINT msaaCount = 2;
	HR(_pd3dDevice->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, msaaCount, &msaaQuality));
	if (msaaQuality == 0)
	{
		_enableMsaa = false;
	}

	// Enable MSAA
	if (_enableMsaa)
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
		HR(_pd3dDevice->CreateRasterizerState(&rasterDesc, &_rasterState));
		_pImmediateContext->RSSetState(_rasterState);
	}

	// Create swap chain
	IDXGIFactory2* dxgiFactory2 = nullptr;
	HR(dxgiFactory->QueryInterface(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgiFactory2)));

	// DirectX 11.1 or later
	hr = _pd3dDevice->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&_pd3dDevice1));
	if (SUCCEEDED(hr))
	{
		(void)_pImmediateContext->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&_pImmediateContext1));
	}

	DXGI_SWAP_CHAIN_DESC1 sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.Width = windowWidth;
	sd.Height = windowHeight;
#ifdef _XBOX_ONE
	sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
#else
	sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
#endif
	sd.SampleDesc.Count = _enableMsaa ? msaaCount : 1;
	sd.SampleDesc.Quality = _enableMsaa ? msaaQuality - 1 : 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = 2;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
#if defined (_XBOX_ONE)
	sd.Scaling = DXGI_SCALING_STRETCH;
	sd.Flags = DXGIX_SWAP_CHAIN_MATCH_XBOX360_AND_PC;
#endif 
	HR(dxgiFactory2->CreateSwapChainForCoreWindow(_pd3dDevice, reinterpret_cast<IUnknown*>(_window.Get()), &sd, nullptr, &_pSwapChain1));
	HR(_pSwapChain1->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(&_pSwapChain)));

	dxgiFactory2->Release();
	dxgiFactory->Release();

	// Create a render target view
	ID3D11Texture2D* pBackBuffer = nullptr;
	HR(_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer)));

	HR(hr = _pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &_pRenderTargetView));
	pBackBuffer->Release();

	// Create depth stencil texture
	D3D11_TEXTURE2D_DESC descDepth;
	ZeroMemory(&descDepth, sizeof(descDepth));
	descDepth.Width = windowWidth;
	descDepth.Height = windowHeight;
	descDepth.MipLevels = 1;
	descDepth.ArraySize = 1;
#ifdef _XBOX_ONE
	descDepth.Format = DXGI_FORMAT_D32_FLOAT;
#else
	descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
#endif
	descDepth.SampleDesc.Count = _enableMsaa ? msaaCount : 1;
	descDepth.SampleDesc.Quality = _enableMsaa ? msaaQuality - 1 : 0;
	descDepth.Usage = D3D11_USAGE_DEFAULT;
	descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	descDepth.CPUAccessFlags = 0;
	descDepth.MiscFlags = 0;
	HR(_pd3dDevice->CreateTexture2D(&descDepth, nullptr, &_pDepthStencil));

	// Create the depth stencil view
	HR(_pd3dDevice->CreateDepthStencilView(_pDepthStencil, 0, &_pDepthStencilView));

	_pImmediateContext->OMSetRenderTargets(1, &_pRenderTargetView, _pDepthStencilView);

	// Setup the viewport
	D3D11_VIEWPORT vp;
	vp.Width = windowWidth;
	vp.Height = windowHeight;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	_pImmediateContext->RSSetViewports(1, &vp);

	return S_OK;
}

//--------------------------------------------------------------------------------------
// Clean up the objects we've created
//--------------------------------------------------------------------------------------
void Game::CleanupDevice()
{
	_pScene->CleanUpDeviceObjects();

	SafeRelease(&_pImmediateContext);
	SafeRelease(&_pCBChangeOnResize);
	SafeRelease(&_rasterState);
	SafeRelease(&_pDepthStencil);
	SafeRelease(&_pDepthStencilView);
	SafeRelease(&_pRenderTargetView);
	SafeRelease(&_pSwapChain1);
	SafeRelease(&_pSwapChain);
	SafeRelease(&_pImmediateContext1);
	SafeRelease(&_pImmediateContext);
	SafeRelease(&_pd3dDevice1);
	SafeRelease(&_pd3dDevice);
}

//--------------------------------------------------------------------------------------
// Render a frame
//--------------------------------------------------------------------------------------
void Game::Render()
{
	HRESULT hr = S_OK;

	// Moved to tree
	if (_resetTree)
	{
		if ((unsigned int)_currentSeed + 1 > _seeds.size())
		{
			_seeds.push_back((unsigned int)time(NULL));
		}

		TreeModelGenerator generator(_seeds[_currentSeed]);
		_pTree->Create(&generator);

		PrimitiveModelGenerator planeGen(PrimitiveType_Box);
		_pPlane->Create(&planeGen);

		HRESULT hr = _pScene->InitGraphics(_pd3dDevice, _pImmediateContext);
		assert(SUCCEEDED(hr));
		
		_resetTree = false;
	}

	// Update our time
	static float t = 0.0f;
	if (_driverType == D3D_DRIVER_TYPE_REFERENCE)
	{
		t += (float)XM_PI * 0.0125f;
	}
	else
	{
		ULONGLONG timeCur = GetTickCount64();
		if (_timeStart == 0)
			_timeStart = timeCur;
		t = (timeCur - _timeStart) / 1000.0f;
	}

	// Rotate cube around the origin
	XMStoreFloat4x4(&_World, XMMatrixRotationY( t ));  //TODO: Uncomment after shadows are working

	// Bind render target
	_pImmediateContext->OMSetRenderTargets(1, &_pRenderTargetView, _pDepthStencilView);

	//
	// Clear the back buffer
	//
	_pImmediateContext->ClearRenderTargetView(_pRenderTargetView, Colors::MidnightBlue);

	//
	// Clear the depth buffer to 1.0 (max depth)
	//
	_pImmediateContext->ClearDepthStencilView(_pDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	_pImmediateContext->VSSetConstantBuffers(1, 1, &_pCBChangeOnResize);

	// Draw everything
	HRC(_pScene->Render(_pImmediateContext, &_World, t));

	//
	// Present our back buffer to our front buffer
	//
	HRC(_pSwapChain->Present(1, 0));

Cleanup:
	return;
}

void Game::OnKeydown(UINT key)  // WM_KEYDOWN
{
	switch (key)
	{
	case VK_SPACE:
	case VK_RIGHT:
		_resetTree = true;
		_currentSeed++;
		break;
	case VK_LEFT:
		if (_currentSeed > 0)
		{
			_resetTree = true;
			_currentSeed--;
		}
		break;
	case '0':
		_timeStart = 0;
		break;
	}
}

#ifndef _XBOX_ONE
// Method to convert a length in device-independent pixels (DIPs) to a length in physical pixels.
float Game::ConvertDipsToPixels(float dips)
{
	static const float dipsPerInch = 96.0f;
	return floor(dips * DisplayProperties::LogicalDpi / dipsPerInch + 0.5f); // Round to nearest integer.
}

#endif

