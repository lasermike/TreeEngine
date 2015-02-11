#include "pch.h"
#include "Game.h"
#include <directxcolors.h>
#include "BitmapFont.h"
#include "StockRenderStates.h"
#include "SceneRoot.h"
#include "Tree.h"
#include "TreeModelGenerator.h"
#include "Primitive.h"
#include "ShadowMap.h"
#include <stdio.h>
#include <time.h>
#include "directxtex.h"

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
	_enableMsaa = false; // TODO
#else
	_enableMsaa = false; // TODO: disabled for windows store
#endif
	_timeStart = 0;
	_currentSeed = 0;
	_resetTree = true;
	_pCBChangeOnResize = nullptr;
	_bitmapFont = nullptr;

	XSF::SetContentFileRoot();

	// Init vertex/index buffer
	_pScene = new SceneRoot();

	// Init trees and other world objects
	_trees.push_back(new Tree());
	_pScene->AddChild((*_trees.rbegin()));

	_trees.push_back(new Tree());
	(*_trees.rbegin())->_position = XMFLOAT3(2,0,2);
	_pScene->AddChild((*_trees.rbegin()));

	_trees.push_back(new Tree());
	(*_trees.rbegin())->_position = XMFLOAT3(-2,0,2);
	_pScene->AddChild((*_trees.rbegin()));

	_pPlane = new Primitive();
	_pScene->AddChild(_pPlane);

	// Init lights
	_renderData.dirLights[0].Ambient  = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
	_renderData.dirLights[0].Diffuse  = XMFLOAT4(0.7f, 0.7f, 0.6f, 1.0f);
	_renderData.dirLights[0].Specular = XMFLOAT4(0.8f, 0.8f, 0.7f, 1.0f);
	_renderData.dirLights[0].Direction = XMFLOAT3(-0.57735f, -0.57735f, 0.57735f);
	_renderData.time = 0;

	// Initialize the view matrix
	XMVECTOR eyePos = XMVectorSet(0.0f, 2.25f, -6.0f, 0.0f);
	XMVECTOR At = XMVectorSet(0.0f, 1.75f, 0.0f, 0.0f);
	XMVECTOR Up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMStoreFloat4x4(&_renderData.view, XMMatrixLookAtLH(eyePos, At, Up));
	XMStoreFloat4(&_renderData.eyePos, eyePos);

	// Init scene bounds.
	// Estimatation.  The ground plane is the widest object at 14 x 14.  
	// Ideally would loop through all world space vertices
	_renderData.mSceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
	_renderData.mSceneBounds.Radius = sqrtf(7.0f*7.0f + 7.0f*7.0f);
}

Game::~Game()
{
	for (auto t = _trees.begin(); t != _trees.end(); t++)
	{
		if (*t)
			delete *t;
	}
	SafeDelete(&_bitmapFont);
	SafeDelete(&_pScene);
	SafeDelete(&_pPlane);
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

		HRR(device.Get()->QueryInterface( __uuidof(_pd3dDevice), reinterpret_cast<void**>(&_pd3dDevice) ) );
		HRR(d3dContext.Get()->QueryInterface( __uuidof(_pImmediateContext), reinterpret_cast<void**>(&_pImmediateContext) ) );

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
	XMStoreFloat4x4(&_renderData.world, XMMatrixIdentity());

	XSF::StockRenderStates::Initialize(_pd3dDevice);
	_bitmapFont = new XSF::BitmapFont();
    XSF_ERROR_IF_FAILED( _bitmapFont->Create( _pd3dDevice, L"Arial_16" ) );

	// Init shadow map
	_renderData.pShadowMap = new ShadowMap(_pd3dDevice, _renderData.SMapSize, _renderData.SMapSize);

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
	UINT windowWidth = (UINT) ConvertDipsToPixels(windowBounds.Width);
	UINT windowHeight = (UINT)  ConvertDipsToPixels(windowBounds.Height);
#endif
	// Initialize the projection matrix
	XMStoreFloat4x4(&_renderData.projection, XMMatrixPerspectiveFovLH(XM_PIDIV4, windowWidth / (float)windowHeight, 0.01f, 100.0f));

	CBChangeOnResize cbChangesOnResize;
	XMStoreFloat4x4(&cbChangesOnResize.mProjection, XMMatrixTranspose(XMLoadFloat4x4(&_renderData.projection)));
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
	const UINT msaaCount = 4;
	HRR(_pd3dDevice->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, msaaCount, &msaaQuality));
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
		HRR(_pd3dDevice->CreateRasterizerState(&rasterDesc, &_rasterState));
		_pImmediateContext->RSSetState(_rasterState);
	}

	// Create swap chain
	IDXGIFactory2* dxgiFactory2 = nullptr;
	HRR(dxgiFactory->QueryInterface(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgiFactory2)));

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
	//sd.Scaling = DXGI_SCALING_STRETCH;
	sd.Flags = DXGIX_SWAP_CHAIN_MATCH_OTHER_CONSOLES;
#endif 
	HRR(dxgiFactory2->CreateSwapChainForCoreWindow(_pd3dDevice, reinterpret_cast<IUnknown*>(_window.Get()), &sd, nullptr, &_pSwapChain1));
	HRR(_pSwapChain1->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(&_pSwapChain)));

	dxgiFactory2->Release();
	dxgiFactory->Release();

	// Create a render target view
	ID3D11Texture2D* pBackBuffer = nullptr;
	HRR(_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer)));

	HRR(hr = _pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &_pRenderTargetView));
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
	HRR(_pd3dDevice->CreateTexture2D(&descDepth, nullptr, &_pDepthStencil));

	// Create the depth stencil view
	HRR(_pd3dDevice->CreateDepthStencilView(_pDepthStencil, 0, &_pDepthStencilView));

	_pImmediateContext->OMSetRenderTargets(1, &_pRenderTargetView, _pDepthStencilView);

	// Setup the viewport
	_viewPort.Width = (FLOAT) windowWidth;
	_viewPort.Height = (FLOAT) windowHeight;
	_viewPort.MinDepth = 0.0f;
	_viewPort.MaxDepth = 1.0f;
	_viewPort.TopLeftX = 0;
	_viewPort.TopLeftY = 0;
	_pImmediateContext->RSSetViewports(1, &_viewPort);

	return S_OK;
}

//--------------------------------------------------------------------------------------
// Clean up the objects we've created
//--------------------------------------------------------------------------------------
void Game::CleanupDevice()
{
	XSF::StockRenderStates::Shutdown();
	_pScene->CleanUpDeviceObjects();

	SafeDelete(&_renderData.pShadowMap);

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

void Game::Update(DX::StepTimer const& timer)
{
	// Rebuild tree if necessary
	if (_resetTree)
	{
		if ((unsigned int)_currentSeed + 1 > _seeds.size())
		{
			_seeds.push_back((unsigned int)time(NULL));
		}

		int treeNum = 1;
		for (auto t = _trees.begin(); t != _trees.end(); t++)
		{
			TreeModelGenerator generator(_seeds[_currentSeed] * treeNum);
			(*t)->Create(&generator);
			treeNum++;
		}

		PrimitiveModelGenerator planeGen(PrimitiveType_Box);
		_pPlane->Create(&planeGen);

		HRESULT hr = _pScene->InitGraphics(_pd3dDevice, _pImmediateContext);
		assert(SUCCEEDED(hr));
		
		_resetTree = false;
	}

	// Update our time
	m_fps = (float) timer.GetFramesPerSecond();

	if (_driverType == D3D_DRIVER_TYPE_REFERENCE)
	{
		_renderData.time += (float)XM_PI * 0.0125f;
	}
	else
	{
		ULONGLONG timeCur = GetTickCount64();
		if (_timeStart == 0)
			_timeStart = timeCur;
		_renderData.time = (timeCur - _timeStart) / 1000.0f;
	}

	// Rotate camera around the origin
	//XMStoreFloat4x4(&_renderData.world, XMMatrixRotationY( _renderData.time ));  

	BuildShadowTransform();

	//_renderData.view = _renderData.lightView;
	//_renderData.projection = _renderData.lightProj;

}

//--------------------------------------------------------------------------------------
// Render a frame
//--------------------------------------------------------------------------------------
void Game::Render()
{
	HRESULT hr = S_OK;

    m_timer.Tick([&]()
    {
        Update(m_timer);
    });

	// Render shadow map
	_renderData.pShadowMap->BindDsvAndSetNullRenderTarget(_pImmediateContext);
	DrawSceneToShadowMap();

	// Restore state after shadow
	_pImmediateContext->RSSetState(0);
	_pImmediateContext->RSSetViewports(1, &_viewPort);

	// Bind render target and depth
	_pImmediateContext->OMSetRenderTargets(1, &_pRenderTargetView, _pDepthStencilView);

#ifdef SAVEDEPTHIMAGE
	// Save shadow mapt to disk
	ScratchImage resultImage, convertedImage;
	HR(CaptureTexture(_pd3dDevice, _pImmediateContext, _renderData.pShadowMap->DepthMapBuffer(), resultImage));
	const Image* img = resultImage.GetImage(0,0,0);
	HR(Convert(*img, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 0.5f, convertedImage));
	img = convertedImage.GetImage(0,0,0);
	HR(SaveToTGAFile(*img, L"c:\\temp\\smap.tga"));
#endif

	// Clear the back buffer
	_pImmediateContext->ClearRenderTargetView(_pRenderTargetView, Colors::AliceBlue);

	// Clear the depth buffer to 1.0 (max depth)
	_pImmediateContext->ClearDepthStencilView(_pDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	_pImmediateContext->VSSetConstantBuffers(1, 1, &_pCBChangeOnResize);

	// Draw everything
	HRC(_pScene->Render(_pImmediateContext, &_renderData));

	if (_bitmapFont)
	{
	    _bitmapFont->Begin(_pImmediateContext, &_viewPort, false /*(SampleSettings().FastSemanticsEnabled()*/ );
		wchar_t text[128];
		swprintf(text, 128, L"FPS %d", m_timer.GetFramesPerSecond());
		_bitmapFont->DrawText(0, 10, 0x33444444, text);
		_bitmapFont->End();
	}

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

void Game::BuildShadowTransform()
{
	// Only the first "main" light casts a shadow.
	XMVECTOR lightDir = XMLoadFloat3(&_renderData.dirLights[0].Direction);
	XMVECTOR lightPos = -2.0f * _renderData.mSceneBounds.Radius * lightDir;
	XMVECTOR targetPos = XMLoadFloat3(&_renderData.mSceneBounds.Center);
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX V = XMMatrixLookAtLH(lightPos, targetPos, up);

	// Transform bounding sphere to light space.
	XMFLOAT3 sphereCenterLS;
	XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, V));

	// Ortho frustum in light space encloses scene.
	float l = sphereCenterLS.x - _renderData.mSceneBounds.Radius;
	float b = sphereCenterLS.y - _renderData.mSceneBounds.Radius;
	float n = sphereCenterLS.z - _renderData.mSceneBounds.Radius;
	float r = sphereCenterLS.x + _renderData.mSceneBounds.Radius;
	float t = sphereCenterLS.y + _renderData.mSceneBounds.Radius;
	float f = sphereCenterLS.z + _renderData.mSceneBounds.Radius;
	XMMATRIX P = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	XMMATRIX S = V*P*T;

	XMStoreFloat4x4(&_renderData.lightView, V);
	XMStoreFloat4x4(&_renderData.lightProj, P);
	XMStoreFloat4x4(&_renderData.shadowTransform, S);
}

void Game::DrawSceneToShadowMap()
{
	XMMATRIX view     = XMLoadFloat4x4(&_renderData.lightView);
	XMMATRIX proj     = XMLoadFloat4x4(&_renderData.lightProj);
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);

	RenderData prevRenderData(_renderData);
	_renderData.view = _renderData.lightView;
	_renderData.projection = _renderData.projection;
	_renderData.pass = ShadowMapPass;

	// Draw everything
	HR(_pScene->Render(_pImmediateContext, &_renderData));

	_renderData = prevRenderData;
}


#ifndef _XBOX_ONE
// Method to convert a length in device-independent pixels (DIPs) to a length in physical pixels.
float Game::ConvertDipsToPixels(float dips)
{
	static const float dipsPerInch = 96.0f;
	return floor(dips * DisplayProperties::LogicalDpi / dipsPerInch + 0.5f); // Round to nearest integer.
}

#endif

