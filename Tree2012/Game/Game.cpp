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
#include "directxtex.h"
#include "StockRenderStates.h"
#include "OrbitCamera.h"
#include "GameLoader.h"
#include "RenderManager.h"
#include "InputManager.h"
//#include "ThreadPool.h"

using namespace DirectX;

#ifndef _XBOX_ONE
#ifndef _TREE_CLASSIC
using namespace Windows::Graphics::Display;
#endif 
#endif 


#define D3D_DEBUG_INFO

//--------------------------------------------------------------------------------------
// Structures
//--------------------------------------------------------------------------------------
struct CBChangeOnResize
{
	XMFLOAT4X4 mProjection;
};

Game::Game(IInputManager* inputMgr) : m_inputMgr(inputMgr) 
{
#ifndef _TREE_CLASSIC
	_window = nullptr;
#else
	_hwnd = nullptr;
#endif
	_driverType = D3D_DRIVER_TYPE_NULL;
	_featureLevel = D3D_FEATURE_LEVEL_11_0;
	_pd3dDevice = nullptr;
	_pd3dDevice1 = nullptr;
	_pImmediateContext = nullptr;
	_pImmediateContext1 = nullptr;
	_pSwapChain = nullptr;
	_pSwapChain1 = nullptr;
	_pRenderTargetView = nullptr;
	_paused = false;
	_wireframe = false;
	_showHelp = false;
	_rasterState = nullptr;
	_displayMode = Monitor;
#ifdef ENABLE_MSAA
	_enableMsaa = true; // TODO
#else
	_enableMsaa = false; // TODO: disabled for windows store
#endif
	_timeStart = 0;
	_resetTree = true;
	_showShadowBuffer = false;
	_pCBChangeOnResize = nullptr;
	_bitmapFont = nullptr;
	_player = nullptr;
	assert(m_inputMgr);
}

void Game::UpdateView()
{
	XMStoreFloat4x4(&_renderManager.GetRenderData().view, _player->GetViewMatrix());
	_renderManager.GetRenderData().eyePos = _player->GetEyePosition();
}

Game::~Game()
{
	CleanupDevice();
	SafeDelete(&_bitmapFont);
	SafeDelete(&_pScene);
}

HRESULT Game::Initialize()
{
	XSF::SetContentFileRoot();

	_renderManager.Initialize();

	// Init vertex/index buffer
	_pScene = new SceneRoot();

	// Create player
	WorldObjectParams* playerParams = new WorldObjectParams(NullGeneratorType);
	playerParams->position = XMFLOAT3(-4.3f, 1.5f, -5.5f);
	XMStoreFloat4(&playerParams->rotation, XMQuaternionRotationAxis(XMVectorSet(0,1,0,1), XM_PIDIV4));	
	_player = new Player(playerParams);

	_renderManager.GetRenderData().frame = 0;

	_loader.Load("Basic", _pScene, &_renderManager.GetRenderData(), _player);

	// Init scene bounds.
	// Estimatation.    
	// Ideally would loop through all world space vertices
	_renderManager.GetRenderData().mSceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
	_renderManager.GetRenderData().mSceneBounds.Radius = 6; //sqrtf(5.0f*5.0f + 5.0f*5.0f);

	return S_OK;
}

#ifdef _TREE_CLASSIC
HRESULT Game::Initialize(HWND hwnd) 
{ 
	Initialize();

	_hwnd = hwnd;  
	HRESULT hr = S_OK;
	HRR(InitDevice());

	UINT windowWidth = 0; 
	UINT windowHeight = 0;
	RECT rect = {0};
	GetClientRect(_hwnd, &rect);
	windowWidth = rect.right - rect.left;
	windowHeight = rect.bottom - rect.top;

	HRR(OnResize(windowWidth, windowHeight));

	return hr;
}

#else

HRESULT Game::Initialize(Windows::UI::Core::CoreWindow^ window) 
{ 
	HRESULT hr = S_OK;

	Initialize();

	_window = window; 
	HRR(InitDevice());

	auto windowBounds = _window->Bounds;
#if defined(_XBOX_ONE)
	UINT windowWidth = 1920;
	UINT windowHeight = 1080;
#else
	UINT windowWidth = (UINT) ConvertDipsToPixels(windowBounds.Width);
	UINT windowHeight = (UINT)  ConvertDipsToPixels(windowBounds.Height);
#endif
	HRR(OnResize(windowWidth, windowHeight));
	
	return hr;
}

#endif

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
		CComPtr<ID3D11Device> device;
	    CComPtr<ID3D11DeviceContext> d3dContext;

		_driverType = driverTypes[driverTypeIndex];
		hr = D3D11CreateDevice(nullptr, _driverType, nullptr, createDeviceFlags, featureLevels, numFeatureLevels,
			D3D11_SDK_VERSION, &device, &_featureLevel, &d3dContext);

		HRR(device->QueryInterface( __uuidof(_pd3dDevice), reinterpret_cast<void**>(&_pd3dDevice) ) );
		HRR(d3dContext->QueryInterface( __uuidof(_pImmediateContext), reinterpret_cast<void**>(&_pImmediateContext) ) );

		if (SUCCEEDED(hr))
			break;
	}
	if (FAILED(hr))
		return hr;

	// Give renderman a reference to D3D
	_renderManager.SetDXReferences(_pd3dDevice, _pImmediateContext);

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

	_pImmediateContext->VSSetConstantBuffers(1, 1, &_pCBChangeOnResize);

	// Initialize the world matrices
	XMStoreFloat4x4(&_renderManager.GetRenderData().world, XMMatrixIdentity());

	XSF::StockRenderStates::Initialize(_pd3dDevice);
	_bitmapFont = new XSF::BitmapFont();
    XSF_ERROR_IF_FAILED( _bitmapFont->Create( _pd3dDevice, L"Arial_16" ) );

	// Init shadow map
	_renderManager.GetRenderData().pShadowMap = new ShadowMap(_pd3dDevice, _renderManager.GetRenderData().SMapWidth, _renderManager.GetRenderData().SMapHeight);

	return S_OK;
}

HRESULT Game::UpdateProjection(XMFLOAT4X4* pProjMat)
{
	CBChangeOnResize cbChangesOnResize;
	XMStoreFloat4x4(&cbChangesOnResize.mProjection, XMMatrixTranspose(XMLoadFloat4x4(pProjMat)));
	_pImmediateContext->UpdateSubresource(_pCBChangeOnResize, 0, nullptr, &cbChangesOnResize, 0, 0);

	return S_OK;
}

HRESULT Game::OnResize(UINT windowWidth, UINT windowHeight)
{
	HRESULT hr = S_OK;

	if (!_pImmediateContext)
	{
		return S_FALSE;
	}

    // Resize logic

	// Create width/height dependent objects
	_pDepthStencilView.Release();
	_pDepthStencil.Release();
	_pDepthStencilView.Release();
	
	SafeRelease(&_pRenderTargetView);
	SafeRelease(&_pSwapChain1);
	SafeRelease(&_pSwapChain);

	// Calculate the necessary swap chain and render target size in pixels.

	// Initialize the projection matrix
	_renderManager.GetRenderData().projectionData.screenWidth = windowWidth;
	_renderManager.GetRenderData().projectionData.screenHeight = windowHeight;
	_renderManager.GetRenderData().projectionData.fov = XM_PIDIV4;
	_renderManager.GetRenderData().projectionData.nearClippingPlane = 1.0f;
	_renderManager.GetRenderData().projectionData.farClippingPlane = 30.0f;

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

#if !defined(_TREE_CLASSIC)
	sd.Width = windowWidth;
	sd.Height = windowHeight;
#endif

#ifdef _XBOX_ONE
	sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	//sd.Scaling = DXGI_SCALING_STRETCH;
	sd.Flags |= DXGIX_SWAP_CHAIN_MATCH_OTHER_CONSOLES;
#else //#elif !defined(_TREE_CLASSIC)
	sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
#endif
	sd.SampleDesc.Count = _enableMsaa ? msaaCount : 1;
	sd.SampleDesc.Quality = _enableMsaa ? msaaQuality - 1 : 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = 2;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

#if defined(_TREE_CLASSIC)
	HRR(dxgiFactory2->CreateSwapChainForHwnd(_pd3dDevice, _hwnd, &sd, nullptr, nullptr, &_pSwapChain1));
	HRR(_pSwapChain1->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(&_pSwapChain)));
#else
	HRR(dxgiFactory2->CreateSwapChainForCoreWindow(_pd3dDevice, reinterpret_cast<IUnknown*>(_window.Get()), &sd, nullptr, &_pSwapChain1));
	HRR(_pSwapChain1->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(&_pSwapChain)));
#endif 

	dxgiFactory2->Release();
	dxgiFactory->Release();

	// Create a render target view
	ID3D11Texture2D* pBackBuffer = nullptr;
	HRR(_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer)));

	HRR(hr = _pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &_pRenderTargetView));
	pBackBuffer->Release();

	// 
	// Create depth stencil texture
	//
	D3D11_TEXTURE2D_DESC descDepth;
	ZeroMemory(&descDepth, sizeof(descDepth));
	descDepth.Width = windowWidth;
	descDepth.Height = windowHeight;
	descDepth.MipLevels = 1;
	descDepth.ArraySize = 1;
#ifdef _XBOX_ONE
	descDepth.Format = DXGI_FORMAT_D32_FLOAT;
#else
	descDepth.Format = DXGI_FORMAT_R24G8_TYPELESS;
#endif
	descDepth.SampleDesc.Count = _enableMsaa ? msaaCount : 1;
	descDepth.SampleDesc.Quality = _enableMsaa ? msaaQuality - 1 : 0;
	descDepth.Usage = D3D11_USAGE_DEFAULT;
	descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	descDepth.CPUAccessFlags = 0;
	descDepth.MiscFlags = 0;
	HRR(_pd3dDevice->CreateTexture2D(&descDepth, nullptr, &_pDepthStencil));

	// Create the depth stencil view
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = 0;
#ifdef _XBOX_ONE
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
#else
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
#endif
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;
    HRR(_pd3dDevice->CreateDepthStencilView(_pDepthStencil, &dsvDesc, &_pDepthStencilView));

	//
	// Setup the viewport
	//
	_viewPort.Width = (FLOAT) windowWidth;
	_viewPort.Height = (FLOAT) windowHeight;
	_viewPort.MinDepth = 0.0f;
	_viewPort.MaxDepth = 1.0f;
	_viewPort.TopLeftX = 0;
	_viewPort.TopLeftY = 0;
	_pImmediateContext->RSSetViewports(1, &_viewPort);

    // Validation
    ASSERT(_pRenderTargetView);
    ASSERT(_pSwapChain1);

    ASSERT(_renderManager.GetRenderData().projectionData.nearClippingPlane != 0);
    ASSERT(_renderManager.GetRenderData().projectionData.farClippingPlane != 0);
    ASSERT(_renderManager.GetRenderData().projectionData.screenWidth != 0);
    ASSERT(_renderManager.GetRenderData().projectionData.screenHeight != 0);
    ASSERT(_renderManager.GetRenderData().projectionData.fov != 0);

    ASSERT(_viewPort.Width != 0);
    ASSERT(_viewPort.Height != 0);

	XMStoreFloat4x4(&_renderManager.GetRenderData().projection, XMMatrixPerspectiveFovLH(_renderManager.GetRenderData().projectionData.fov, 
                    _renderManager.GetRenderData().projectionData.screenWidth / (float)_renderManager.GetRenderData().projectionData.screenHeight, 
                    _renderManager.GetRenderData().projectionData.nearClippingPlane, _renderManager.GetRenderData().projectionData.farClippingPlane));

	UpdateProjection(&_renderManager.GetRenderData().projection);

    ASSERT(!XMMatrixIsIdentity(XMLoadFloat4x4(&_renderManager.GetRenderData().projection)));

	return S_OK;
}

//--------------------------------------------------------------------------------------
// Clean up the objects we've created
//--------------------------------------------------------------------------------------
void Game::CleanupDevice()
{
    if (_pScene)
    {
        _pScene->CleanUpDeviceObjects();
    }

    _renderManager.CleanUpDeviceObjects();

	XSF::StockRenderStates::Shutdown();
	_renderManager.CleanUpDeviceObjects();

	SafeDelete(&_renderManager.GetRenderData().pShadowMap);

	SafeRelease(&_pImmediateContext);
	SafeRelease(&_pCBChangeOnResize);
	SafeRelease(&_rasterState);
	_pDepthStencil.Release();
	_pDepthStencilView.Release();
	SafeRelease(&_pRenderTargetView);
	SafeRelease(&_pSwapChain1);
	SafeRelease(&_pSwapChain);
	SafeRelease(&_pImmediateContext1);
	SafeRelease(&_pImmediateContext);
	SafeRelease(&_pd3dDevice1);
	SafeRelease(&_pd3dDevice);
}

void Game::Regenerate()
{
	HRESULT hr = S_OK;

	// Clear old stuff
	_pScene->CleanUpDeviceObjects();
	_renderManager.CleanUpDeviceObjects();

	_loader.Regenerate(_pScene);

    // Init render manager
    hr = _renderManager.InitGraphics(_pScene->GetMaxInstances());
	assert(SUCCEEDED(hr));		

	// Init new stuff
	hr = _pScene->InitGraphics(_renderManager);
    assert(SUCCEEDED(hr));
}

void Game::Update(DX::StepTimer const& timer)
{
	_renderManager.GetRenderData().frame++;

	// Rebuild tree if necessary
	if (_resetTree)
	{
		Regenerate();
		_resetTree = false;
	}

	// Update our time
	if (_driverType == D3D_DRIVER_TYPE_REFERENCE)
	{
		_renderManager.GetRenderData().time += (float)XM_PI * 0.0125f;
	}
	else
	{
		if (_timeStart == 0)
		{
			_timeStart = timer.GetTotalSeconds();
			_timeCurrent = 0;
		}
		else if (!_paused)
		{
			_timeCurrent += timer.GetElapsedSeconds();
		}
		_renderManager.GetRenderData().time = (float) _timeCurrent;
	}


	// Compute per-frame values
	HR(_pScene->Update(_renderManager));

    _player->Update(timer, &_renderManager.GetRenderData());

	// Render shadow map
	BuildShadowTransform();
	_renderManager.GetRenderData().pShadowMap->BindDsvAndSetNullRenderTarget(_pImmediateContext, nullptr);
	DrawSceneToShadowMap();
}


//--------------------------------------------------------------------------------------
// Once per frame processing
//--------------------------------------------------------------------------------------
void Game::ComputeCPU()
{
	FrameInputData& inputData = m_inputMgr->GetFrameInput(0);
	HandleInput(inputData.key);

    _timer.Tick([&]()
    {
        Update(_timer);
    });
}

void Game::ComputeGPU()
{
	// Render shadow map
	_renderManager.GetRenderData().pShadowMap->BindDsvAndSetNullRenderTarget(_pImmediateContext, nullptr);
	DrawSceneToShadowMap();

	// Restore state after shadow
	_pImmediateContext->RSSetState(0);
	_pImmediateContext->RSSetViewports(1, &_viewPort);
}

//--------------------------------------------------------------------------------------
// Render a frame.  May be called twice for stereo rendering
//--------------------------------------------------------------------------------------
void Game::Render(bool oculus)
{
	HRESULT hr = S_OK;

    UpdateView();

    if (!oculus)
    {
	    // Bind render target and depth
	    _pImmediateContext->OMSetRenderTargets(1, &_pRenderTargetView, _pDepthStencilView);
    }

	const XSF::StockRenderStates& stockStates = XSF::StockRenderStates::GetStates();
	if (_wireframe)
	{
		stockStates.ApplyRasterizerState( _pImmediateContext, XSF::StockRasterizerStates::Wireframe);
	}

    if (!oculus)
    {
	    // Clear the back buffer
	    _pImmediateContext->ClearRenderTargetView(_pRenderTargetView, Colors::AliceBlue);

	    // Clear the depth buffer to 1.0 (max depth)
	    _pImmediateContext->ClearDepthStencilView(_pDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }

	// Make shadow map avaiable to shaders
	ID3D11ShaderResourceView* depthTexture = _renderManager.GetRenderData().pShadowMap->DepthMapSRV();
	_pImmediateContext->PSSetShaderResources(1, 1, &depthTexture);

	// Draw everything
	HRC(_renderManager.Render());

	// Unbind shadow texture so we can render to it next frame
	depthTexture = nullptr;
	_pImmediateContext->PSSetShaderResources(1, 1, &depthTexture);

	if (_showHelp && _bitmapFont)
	{
	    _bitmapFont->Begin(_pImmediateContext, &_viewPort, false );
		wchar_t text[128];
		swprintf(text, 128, L"FPS %d", _timer.GetFramesPerSecond());
		_bitmapFont->DrawText(0, 10, 0x33444444, text);
		_bitmapFont->End();
	}

	if(_showShadowBuffer)
	{
		HRC(_renderManager.DrawScreenQuad(_pImmediateContext, _renderManager.GetRenderData().pShadowMap->DepthMapSRV()));
	}

    if (!oculus)
    {
	    // Present our back buffer to our front buffer
	    HRC(_pSwapChain->Present(0, 0));
    }

Cleanup:
	return;
}

void Game::HandleInput(bool key[256])  // WM_KEYDOWN
{
    _player->HandleInput(key);

    const char availableKeys[] = { '0', 'Z', 'P', '#' , 'H' };
	for (char k : availableKeys)
	{
		if (key[k])
		{
			switch (k)
			{
			case ']':
				_resetTree = true;
				_loader._currentSeed++;
				break;
			case '[':
				if (_loader._currentSeed > 0)
				{
					_resetTree = true;
					_loader._currentSeed--;
				}
				break;
			case '0':
				_timeStart = 0;
				break;
			case 'Z':
				_showShadowBuffer = !_showShadowBuffer;
				break;
			case 'P':
				_paused = !_paused;
				break;
			case '#':
				_wireframe = !_wireframe;
				break;
			case 'H':
				_showHelp = !_showHelp;
				break;
			//case '1':
			//case '2':
			//case '3':
			//case '4':
			//case '5':
			//case '6':
			//	Select(k - '0');
			//	break;
			}
		}
	}
}

void Game::BuildShadowTransform()
{
	// Only the first "main" light casts a shadow.
	XMVECTOR lightDir = XMLoadFloat3(&_renderManager.GetRenderData().dirLights[0].Direction);
	XMVECTOR lightPos = -2.0f * _renderManager.GetRenderData().mSceneBounds.Radius * lightDir;
	XMVECTOR targetPos = XMLoadFloat3(&_renderManager.GetRenderData().mSceneBounds.Center);
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX V = XMMatrixLookAtLH(lightPos, targetPos, up);

	// Transform bounding sphere to light space.
	XMFLOAT3 sphereCenterLS;
	XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, V));

	// Ortho frustum in light space encloses scene.
	float l = sphereCenterLS.x - _renderManager.GetRenderData().mSceneBounds.Radius;
	float b = sphereCenterLS.y - _renderManager.GetRenderData().mSceneBounds.Radius;
	float n = sphereCenterLS.z - _renderManager.GetRenderData().mSceneBounds.Radius;
	float r = sphereCenterLS.x + _renderManager.GetRenderData().mSceneBounds.Radius;
	float t = sphereCenterLS.y + _renderManager.GetRenderData().mSceneBounds.Radius;
	float f = sphereCenterLS.z + _renderManager.GetRenderData().mSceneBounds.Radius;
	XMMATRIX P = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	XMMATRIX S = V*P*T;

	XMStoreFloat4x4(&_renderManager.GetRenderData().lightView, V);
	XMStoreFloat4x4(&_renderManager.GetRenderData().lightProj, P);
	XMStoreFloat4x4(&_renderManager.GetRenderData().shadowTransform, S);
}

void Game::DrawSceneToShadowMap()
{
	XMMATRIX view     = XMLoadFloat4x4(&_renderManager.GetRenderData().lightView);
	XMMATRIX proj     = XMLoadFloat4x4(&_renderManager.GetRenderData().lightProj);
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);

	RenderData prevRenderData(_renderManager.GetRenderData());
	_renderManager.GetRenderData().view = _renderManager.GetRenderData().lightView;
	_renderManager.GetRenderData().projection = _renderManager.GetRenderData().lightProj;
	_renderManager.GetRenderData().pass = ShadowMapPass;

	UpdateProjection(&_renderManager.GetRenderData().projection);

    const XSF::StockRenderStates& stockStates = XSF::StockRenderStates::GetStates();
	stockStates.ApplyRasterizerState( _pImmediateContext, XSF::StockRasterizerStates::BuildShadowMap );

	// Draw everything
	HR(_renderManager.Render());

	_renderManager.GetRenderData() = prevRenderData;

	UpdateProjection(&_renderManager.GetRenderData().projection);

	stockStates.ApplyRasterizerState( _pImmediateContext, XSF::StockRasterizerStates::Solid);
}

#if !defined(_TREE_CLASSIC) && !defined(_XBOX_ONE)
// Method to convert a length in device-independent pixels (DIPs) to a length in physical pixels.
float Game::ConvertDipsToPixels(float dips)
{
	static const float dipsPerInch = 96.0f;
	return floor(dips * DisplayProperties::LogicalDpi / dipsPerInch + 0.5f); // Round to nearest integer.
}
#endif


