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
#include "ThreadPool.h"

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
	m_window = nullptr;
#else
	m_hwnd = nullptr;
#endif
	m_driverType = D3D_DRIVER_TYPE_NULL;
	m_featureLevel = D3D_FEATURE_LEVEL_11_0;
	m_paused = false;
	m_wireframe = false;
	m_showHelp = false;
	m_displayMode = Monitor;
	m_timeStart = 0;
	m_resetTree = true;
	m_showShadowBuffer = false;

#ifdef ENABLE_MSAA
	m_enableMsaa = true; // TODO
#else
	m_enableMsaa = false; // TODO: disabled for windows store
#endif

	m_bitmapFont = nullptr;
	m_player = nullptr;
	m_threadPool = nullptr;
	assert(m_inputMgr);
}

void Game::UpdateView()
{
	XMStoreFloat4x4(&m_renderManager.GetRenderData().view, m_player->GetViewMatrix());
	m_renderManager.GetRenderData().eyePos = m_player->GetEyePosition();
}

Game::~Game()
{
	SafeDelete(&m_bitmapFont);
	SafeDelete(&m_pScene);
	SafeDelete(&m_threadPool);
	SafeDelete(&m_player);

	CleanupDevice();
}

HRESULT Game::Initialize()
{
	XSF::SetContentFileRoot();

	m_renderManager.Initialize();

	// Init vertex/index buffer
	m_pScene = new SceneRoot();

	// Create player
	WorldObjectParams* playerParams = new WorldObjectParams(NullGeneratorType);
	playerParams->position = XMFLOAT3(-4.3f, 1.5f, -5.5f);
	XMStoreFloat4(&playerParams->rotation, XMQuaternionRotationAxis(XMVectorSet(0,1,0,1), XM_PIDIV4));	
	m_player = new Player(playerParams);

	m_renderManager.GetRenderData().frame = 0;

	m_loader.Load("Basic", m_pScene, &m_renderManager.GetRenderData(), m_player);

	// Init scene bounds.
	// Estimatation.    
	// Ideally would loop through all world space vertices
	m_renderManager.GetRenderData().mSceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
	m_renderManager.GetRenderData().mSceneBounds.Radius = 6; //sqrtf(5.0f*5.0f + 5.0f*5.0f);

	// Create thread pool
	//may return 0 when not able to detect
	unsigned concurentThreadsSupported = std::thread::hardware_concurrency();

	m_threadPool = new ThreadPool(concurentThreadsSupported ? concurentThreadsSupported : 1 );

	return S_OK;
}

#ifdef _TREE_CLASSIC
HRESULT Game::Initialize(HWND hwnd) 
{ 
	Initialize();

	m_hwnd = hwnd;  
	HRESULT hr = S_OK;
	HRR(InitDevice());

	UINT windowWidth = 0; 
	UINT windowHeight = 0;
	RECT rect = {0};
	GetClientRect(m_hwnd, &rect);
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

	m_window = window; 
	HRR(InitDevice());

	auto windowBounds = m_window->Bounds;
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

		m_driverType = driverTypes[driverTypeIndex];
		hr = D3D11CreateDevice(nullptr, m_driverType, nullptr, createDeviceFlags, featureLevels, numFeatureLevels,
			D3D11_SDK_VERSION, &device, &m_featureLevel, &d3dContext);

		if (SUCCEEDED(hr))
        {
        	HRR(device->QueryInterface( __uuidof(m_pd3dDevice), reinterpret_cast<void**>(&m_pd3dDevice) ) );
    		HRR(d3dContext->QueryInterface( __uuidof(m_pImmediateContext), reinterpret_cast<void**>(&m_pImmediateContext) ) );
			break;
        }
	}
	if (FAILED(hr))
		return hr;

#if defined(_DEBUG) && !defined(_XBOX_ONE)
	{
		// Debug layers
		CComPtr<ID3D11Debug> d3dDebug;
		HR(m_pd3dDevice->QueryInterface( __uuidof(ID3D11Debug), (void**)&d3dDebug));
	
		CComPtr<ID3D11InfoQueue> d3dInfoQueue;
		HR(d3dDebug->QueryInterface( __uuidof(ID3D11InfoQueue), (void**)&d3dInfoQueue ))
		//#ifdef _DEBUG
		d3dInfoQueue->SetBreakOnSeverity( D3D11_MESSAGE_SEVERITY_CORRUPTION, true );
		d3dInfoQueue->SetBreakOnSeverity( D3D11_MESSAGE_SEVERITY_ERROR, true );
		//#endif
 
		D3D11_MESSAGE_ID hide [] =
		{
			D3D11_MESSAGE_ID_SETPRIVATEDATA_CHANGINGPARAMS,
			// Add more message IDs here as needed
		};
 
		D3D11_INFO_QUEUE_FILTER filter;
		ZeroMemory(&filter, sizeof(filter));
		filter.DenyList.NumIDs = _countof(hide);
		filter.DenyList.pIDList = hide;
		d3dInfoQueue->AddStorageFilterEntries( &filter );
		d3dInfoQueue->Release();
	}
#endif

	//
	// Give renderman a reference to D3D
	m_renderManager.SetDXReferences(m_pd3dDevice, m_pImmediateContext);

	// 
	// Create constant buffer
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(CBChangeOnResize);
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = 0;
	hr = m_pd3dDevice->CreateBuffer(&bd, nullptr, &m_pCBChangeOnResize);
	if (FAILED(hr))
		return hr;

	m_pImmediateContext->VSSetConstantBuffers(1, 1, &m_pCBChangeOnResize);

	// Initialize the world matrices
	XMStoreFloat4x4(&m_renderManager.GetRenderData().world, XMMatrixIdentity());

	XSF::StockRenderStates::Initialize(m_pd3dDevice);
	m_bitmapFont = new XSF::BitmapFont();
    XSF_ERROR_IF_FAILED( m_bitmapFont->Create( m_pd3dDevice, L"Arial_16" ) );

	// Init shadow map
	m_renderManager.GetRenderData().pShadowMap = new ShadowMap(m_pd3dDevice, m_renderManager.GetRenderData().SMapWidth, m_renderManager.GetRenderData().SMapHeight);

	return S_OK;
}

HRESULT Game::UpdateProjection(XMFLOAT4X4* pProjMat)
{
	CBChangeOnResize cbChangesOnResize;
	XMStoreFloat4x4(&cbChangesOnResize.mProjection, XMMatrixTranspose(XMLoadFloat4x4(pProjMat)));
	m_pImmediateContext->UpdateSubresource(m_pCBChangeOnResize, 0, nullptr, &cbChangesOnResize, 0, 0);

	return S_OK;
}

HRESULT Game::OnResize(UINT windowWidth, UINT windowHeight)
{
	HRESULT hr = S_OK;

	if (!m_pImmediateContext)
	{
		return S_FALSE;
	}

    // Resize logic

	// Create width/height dependent objects
	m_pDepthStencilView.Release();
	m_pDepthStencil.Release();
	
	m_pRenderTargetView.Release();
	m_pSwapChain1.Release();
	m_pSwapChain.Release();

	// Calculate the necessary swap chain and render target size in pixels.

	// Initialize the projection matrix
	m_renderManager.GetRenderData().projectionData.screenWidth = windowWidth;
	m_renderManager.GetRenderData().projectionData.screenHeight = windowHeight;
	m_renderManager.GetRenderData().projectionData.fov = XM_PIDIV4;
	m_renderManager.GetRenderData().projectionData.nearClippingPlane = .2f;
	m_renderManager.GetRenderData().projectionData.farClippingPlane = 30.0f;

	// Obtain DXGI factory from device (since we used nullptr for pAdapter above)
    CComPtr<IDXGIFactory1> dxgiFactory;
	{
		CComPtr<IDXGIDevice> dxgiDevice;
		hr = m_pd3dDevice->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
		if (SUCCEEDED(hr))
		{
			CComPtr<IDXGIAdapter> adapter;
			hr = dxgiDevice->GetAdapter(&adapter);
			if (SUCCEEDED(hr))
			{
				hr = adapter->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&dxgiFactory));
			}
		}
	}
	if (FAILED(hr))
		return hr;

	// Check MSAA support
	UINT msaaQuality;
	const UINT msaaCount = 4;
	HRR(m_pd3dDevice->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, msaaCount, &msaaQuality));
	if (msaaQuality == 0)
	{
		m_enableMsaa = false;
	}

	// Enable MSAA
	if (m_enableMsaa)
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
		HRR(m_pd3dDevice->CreateRasterizerState(&rasterDesc, &m_rasterState));
		SetDebugName(m_rasterState, "Game::m_rasterState");
		m_pImmediateContext->RSSetState(m_rasterState);
	}

	// Create swap chain
	CComPtr<IDXGIFactory2> dxgiFactory2;
	HRR(dxgiFactory->QueryInterface(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgiFactory2)));

	// DirectX 11.1 or later
	hr = m_pd3dDevice->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&m_pd3dDevice1));
	if (SUCCEEDED(hr))
	{
		(void)m_pImmediateContext->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&m_pImmediateContext1));
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
	sd.SampleDesc.Count = m_enableMsaa ? msaaCount : 1;
	sd.SampleDesc.Quality = m_enableMsaa ? msaaQuality - 1 : 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = 2;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

#if defined(_TREE_CLASSIC)
	HRR(dxgiFactory2->CreateSwapChainForHwnd(m_pd3dDevice, m_hwnd, &sd, nullptr, nullptr, &m_pSwapChain1));
	HRR(m_pSwapChain1->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(&m_pSwapChain)));
#else
	HRR(dxgiFactory2->CreateSwapChainForCoreWindow(m_pd3dDevice, reinterpret_cast<IUnknown*>(m_window.Get()), &sd, nullptr, &m_pSwapChain1));
	HRR(m_pSwapChain1->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(&m_pSwapChain)));
#endif 

	// Create a render target view
	CComPtr<ID3D11Texture2D> pBackBuffer;
	HRR(m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer)));

	HRR(hr = m_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_pRenderTargetView));
	SetDebugName(m_pRenderTargetView, "Game::m_pRenderTargetView");
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
	descDepth.SampleDesc.Count = m_enableMsaa ? msaaCount : 1;
	descDepth.SampleDesc.Quality = m_enableMsaa ? msaaQuality - 1 : 0;
	descDepth.Usage = D3D11_USAGE_DEFAULT;
	descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	descDepth.CPUAccessFlags = 0;
	descDepth.MiscFlags = 0;
	HRR(m_pd3dDevice->CreateTexture2D(&descDepth, nullptr, &m_pDepthStencil));
	SetDebugName(m_pDepthStencil, "Game::m_pDepthStencil");

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
    HRR(m_pd3dDevice->CreateDepthStencilView(m_pDepthStencil, &dsvDesc, &m_pDepthStencilView));
	SetDebugName(m_pDepthStencilView, "Game::m_pDepthStencilView");

	//
	// Setup the viewport
	//
	m_viewPort.Width = (FLOAT) windowWidth;
	m_viewPort.Height = (FLOAT) windowHeight;
	m_viewPort.MinDepth = 0.0f;
	m_viewPort.MaxDepth = 1.0f;
	m_viewPort.TopLeftX = 0;
	m_viewPort.TopLeftY = 0;
	m_pImmediateContext->RSSetViewports(1, &m_viewPort);

    // Validation
    ASSERT(m_pRenderTargetView);
    ASSERT(m_pSwapChain1);

    ASSERT(m_renderManager.GetRenderData().projectionData.nearClippingPlane != 0);
    ASSERT(m_renderManager.GetRenderData().projectionData.farClippingPlane != 0);
    ASSERT(m_renderManager.GetRenderData().projectionData.screenWidth != 0);
    ASSERT(m_renderManager.GetRenderData().projectionData.screenHeight != 0);
    ASSERT(m_renderManager.GetRenderData().projectionData.fov != 0);

    ASSERT(m_viewPort.Width != 0);
    ASSERT(m_viewPort.Height != 0);

	XMStoreFloat4x4(&m_renderManager.GetRenderData().projection, XMMatrixPerspectiveFovLH(m_renderManager.GetRenderData().projectionData.fov, 
                    m_renderManager.GetRenderData().projectionData.screenWidth / (float)m_renderManager.GetRenderData().projectionData.screenHeight, 
                    m_renderManager.GetRenderData().projectionData.nearClippingPlane, m_renderManager.GetRenderData().projectionData.farClippingPlane));

	UpdateProjection(&m_renderManager.GetRenderData().projection);

    ASSERT(!XMMatrixIsIdentity(XMLoadFloat4x4(&m_renderManager.GetRenderData().projection)));

	return S_OK;
}

//--------------------------------------------------------------------------------------
// Clean up the objects we've created
//--------------------------------------------------------------------------------------
void Game::CleanupDevice()
{
    if (m_pScene)
    {
        m_pScene->CleanUpDeviceObjects();
        SafeDelete(&m_pScene);
    }

    m_renderManager.CleanUpDeviceObjects();

	XSF::StockRenderStates::Shutdown();

	SafeDelete(&m_renderManager.GetRenderData().pShadowMap);

	m_pImmediateContext.Release();
	m_pCBChangeOnResize.Release();
	m_rasterState.Release();
	m_pDepthStencil.Release();
	m_pDepthStencilView.Release();
	m_pRenderTargetView.Release();
	m_pSwapChain1.Release();
	m_pSwapChain.Release();
	m_pImmediateContext1.Release();
	m_pImmediateContext.Release();

    if (m_bitmapFont)
    {
        delete m_bitmapFont;
        m_bitmapFont = nullptr;
    }

#if defined(_DEBUG) && !defined(_XBOX_ONE)
	if (m_pd3dDevice)
	{
		CComPtr<ID3D11Debug> dbg;
		HR(m_pd3dDevice->QueryInterface(__uuidof(ID3D11Debug), reinterpret_cast<void**>(&dbg)));

		HR(dbg->ReportLiveDeviceObjects(D3D11_RLDO_SUMMARY | D3D11_RLDO_DETAIL));
	}
#endif

	m_pd3dDevice.Release();
    m_pd3dDevice1.Detach(); // TODO: Device leak somewhere causing crash
}

void Game::Regenerate()
{
	HRESULT hr = S_OK;

	// Clear old stuff
	m_pScene->CleanUpDeviceObjects();
	m_renderManager.CleanUpDeviceObjects();

	m_loader.Regenerate(m_pScene);

    // Init render manager
    hr = m_renderManager.InitGraphics(m_pScene->GetMaxInstances());
	assert(SUCCEEDED(hr));		

	// Init new stuff
	hr = m_pScene->InitGraphics(m_renderManager);
    assert(SUCCEEDED(hr));
}

void Game::Update(DX::StepTimer const& timer)
{
	m_renderManager.GetRenderData().frame++;

	// Rebuild tree if necessary
	if (m_resetTree)
	{
		Regenerate();
		m_resetTree = false;
	}

	// Update our time
	if (m_driverType == D3D_DRIVER_TYPE_REFERENCE)
	{
		m_renderManager.GetRenderData().time += (float)XM_PI * 0.0125f;
	}
	else
	{
		if (m_timeStart == 0)
		{
			m_timeStart = timer.GetTotalSeconds();
			m_timeCurrent = 0;
		}
		else if (!m_paused)
		{
			m_timeCurrent += timer.GetElapsedSeconds();
		}
		m_renderManager.GetRenderData().time = (float) m_timeCurrent;
	}

	HR(m_renderManager.BeginFrame());

	// Compute per-frame values
	HR(m_pScene->Update(m_renderManager, *m_threadPool));

    m_player->Update(timer, &m_renderManager.GetRenderData());

	HR(m_renderManager.EndFrame());

}


//--------------------------------------------------------------------------------------
// Once per frame processing
//--------------------------------------------------------------------------------------
void Game::ComputeCPU()
{
	FrameInputData& inputData = m_inputMgr->GetFrameInput(0);
	HandleInput(inputData.key);

    m_timer.Tick([&]()
    {
        Update(m_timer);
    });
}

void Game::ComputeGPU()
{
	// Render shadow map
	BuildShadowTransform();
	m_renderManager.GetRenderData().pShadowMap->BindDsvAndSetNullRenderTarget(m_pImmediateContext, nullptr);
	DrawSceneToShadowMap();

	// Restore state after shadow
	m_pImmediateContext->RSSetState(0);
	m_pImmediateContext->RSSetViewports(1, &m_viewPort);
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
	    m_pImmediateContext->OMSetRenderTargets(1, &m_pRenderTargetView, m_pDepthStencilView);
    }

	const XSF::StockRenderStates& stockStates = XSF::StockRenderStates::GetStates();
	if (m_wireframe)
	{
		stockStates.ApplyRasterizerState(m_pImmediateContext, XSF::StockRasterizerStates::Wireframe);
	}

    if (!oculus)
    {
	    // Clear the back buffer
	    m_pImmediateContext->ClearRenderTargetView(m_pRenderTargetView, Colors::AliceBlue);

	    // Clear the depth buffer to 1.0 (max depth)
	    m_pImmediateContext->ClearDepthStencilView(m_pDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }

	// Make shadow map avaiable to shaders
	ID3D11ShaderResourceView* depthTexture = m_renderManager.GetRenderData().pShadowMap->DepthMapSRV();
	m_pImmediateContext->PSSetShaderResources(1, 1, &depthTexture);

	// Draw everything
	HRC(m_renderManager.Render());

	// Unbind shadow texture so we can render to it next frame
	depthTexture = nullptr;
	m_pImmediateContext->PSSetShaderResources(1, 1, &depthTexture);

	if (m_showHelp && m_bitmapFont)
	{
	    m_bitmapFont->Begin(m_pImmediateContext, &m_viewPort, false );
		wchar_t text[128];
		swprintf(text, 128, L"FPS %d", m_timer.GetFramesPerSecond());
		m_bitmapFont->DrawText(0, 10, 0x33444444, text);
		m_bitmapFont->End();
	}

	if(m_showShadowBuffer)
	{
		HRC(m_renderManager.DrawScreenQuad(m_pImmediateContext, m_renderManager.GetRenderData().pShadowMap->DepthMapSRV()));
	}

    if (!oculus)
    {
	    // Present our back buffer to our front buffer
	    HRC(m_pSwapChain->Present(0, 0));
    }

Cleanup:
	return;
}

void Game::HandleInput(bool key[256])  // WM_KEYDOWN
{
    m_player->HandleInput(key);

    const char availableKeys[] = { '0', 'Z', 'P', '#' , 'H' };
	for (char k : availableKeys)
	{
		if (key[k])
		{
			switch (k)
			{
			case ']':
				m_resetTree = true;
				m_loader._currentSeed++;
				break;
			case '[':
				if (m_loader._currentSeed > 0)
				{
					m_resetTree = true;
					m_loader._currentSeed--;
				}
				break;
			case '0':
				m_timeStart = 0;
				break;
			case 'Z':
				m_showShadowBuffer = !m_showShadowBuffer;
				break;
			case 'P':
				m_paused = !m_paused;
				break;
			case '#':
				m_wireframe = !m_wireframe;
				break;
			case 'H':
				m_showHelp = !m_showHelp;
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
	XMVECTOR lightDir = XMLoadFloat3(&m_renderManager.GetRenderData().dirLights[0].Direction);
	XMVECTOR lightPos = -2.0f * m_renderManager.GetRenderData().mSceneBounds.Radius * lightDir;
	XMVECTOR targetPos = XMLoadFloat3(&m_renderManager.GetRenderData().mSceneBounds.Center);
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX V = XMMatrixLookAtLH(lightPos, targetPos, up);

	// Transform bounding sphere to light space.
	XMFLOAT3 sphereCenterLS;
	XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, V));

	// Ortho frustum in light space encloses scene.
	float l = sphereCenterLS.x - m_renderManager.GetRenderData().mSceneBounds.Radius;
	float b = sphereCenterLS.y - m_renderManager.GetRenderData().mSceneBounds.Radius;
	float n = sphereCenterLS.z - m_renderManager.GetRenderData().mSceneBounds.Radius;
	float r = sphereCenterLS.x + m_renderManager.GetRenderData().mSceneBounds.Radius;
	float t = sphereCenterLS.y + m_renderManager.GetRenderData().mSceneBounds.Radius;
	float f = sphereCenterLS.z + m_renderManager.GetRenderData().mSceneBounds.Radius;
	XMMATRIX P = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	XMMATRIX S = V*P*T;

	XMStoreFloat4x4(&m_renderManager.GetRenderData().lightView, V);
	XMStoreFloat4x4(&m_renderManager.GetRenderData().lightProj, P);
	XMStoreFloat4x4(&m_renderManager.GetRenderData().shadowTransform, S);
}

void Game::DrawSceneToShadowMap()
{
	XMMATRIX view     = XMLoadFloat4x4(&m_renderManager.GetRenderData().lightView);
	XMMATRIX proj     = XMLoadFloat4x4(&m_renderManager.GetRenderData().lightProj);
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);

	RenderData prevRenderData(m_renderManager.GetRenderData());
	m_renderManager.GetRenderData().view = m_renderManager.GetRenderData().lightView;
	m_renderManager.GetRenderData().projection = m_renderManager.GetRenderData().lightProj;
	m_renderManager.GetRenderData().pass = ShadowMapPass;

	UpdateProjection(&m_renderManager.GetRenderData().projection);

    const XSF::StockRenderStates& stockStates = XSF::StockRenderStates::GetStates();
	stockStates.ApplyRasterizerState( m_pImmediateContext, XSF::StockRasterizerStates::BuildShadowMap );

	// Draw everything
	HR(m_renderManager.Render());

	m_renderManager.GetRenderData() = prevRenderData;

	UpdateProjection(&m_renderManager.GetRenderData().projection);

	stockStates.ApplyRasterizerState( m_pImmediateContext, XSF::StockRasterizerStates::Solid);
}

#if !defined(_TREE_CLASSIC) && !defined(_XBOX_ONE)
// Method to convert a length in device-independent pixels (DIPs) to a length in physical pixels.
float Game::ConvertDipsToPixels(float dips)
{
	static const float dipsPerInch = 96.0f;
	return floor(dips * DisplayProperties::LogicalDpi / dipsPerInch + 0.5f); // Round to nearest integer.
}
#endif


