#include "pch.h"
#include "Game.h"
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
#ifndef WIN32
using namespace Windows::Graphics::Display;
#endif 
#endif 


#define D3D_DEBUG_INFO

Game::Game(IInputManager* inputMgr) : m_inputMgr(inputMgr)
{
#if defined(WIN32) && !defined(TREENGINE_XBOX)
	m_hwnd = nullptr;
#else
	m_window = nullptr;
#endif
	m_renderToSharedTexture = false;
	m_paused = false;
	m_wireframe = false;
	m_showHelp = false;
	m_timeStart = 0;
	m_resetTree = true;
	m_showShadowBuffer = false;
	m_advanceScene = 0;
	m_advanceSceneAmount = 0;
	m_currentScene = 0;

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
	SafeDelete(&m_pScene);
	SafeDelete(&m_threadPool);
	SafeDelete(&m_player);

	m_renderManager.UninitDevice();
}

HRESULT Game::Initialize()
{
	XSF::SetContentFileRoot();

	m_renderManager.Initialize();

	// Init vertex/index buffer
	m_pScene = new SceneRoot();

	// Create player
	WorldObjectParams* playerParams = new WorldObjectParams(NullGeneratorType);
	playerParams->position = XMFLOAT3(-4.0f, 1.5f, -4.0f);
	XMStoreFloat4(&playerParams->rotation, XMQuaternionRotationAxis(XMVectorSet(0,1,0,1), XM_PIDIV4));	
	m_player = new Player(playerParams);

	// Rendering defaults
	m_renderManager.GetRenderData().frame = 0;
	m_renderManager.GetRenderData().projectionData.fov = XM_PIDIV4;
	m_renderManager.GetRenderData().projectionData.nearClippingPlane = .2f;
	m_renderManager.GetRenderData().projectionData.farClippingPlane = 30.0f;

	m_currentScene = 0;
	m_loader.Load(m_currentScene, m_pScene, &m_renderManager.GetRenderData(), m_player, &m_gameData);
	//m_loader.Load("Basic", m_pScene, &m_renderManager.GetRenderData(), m_player, &m_gameData);

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

#if defined(WIN32) && !defined(TREENGINE_XBOX)
HRESULT Game::Initialize(HWND hwnd, bool renderToSharedTexture)
{ 
	m_renderToSharedTexture = renderToSharedTexture;

	Initialize();

	m_hwnd = hwnd;  
	HRESULT hr = S_OK;
	HRR(m_renderManager.InitDevice());

	UINT windowWidth = 0; 
	UINT windowHeight = 0;
	RECT rect = {0};
	GetClientRect(m_hwnd, &rect);
	windowWidth = rect.right - rect.left;
	windowHeight = rect.bottom - rect.top;

	HRR(m_renderManager.OnResize(windowWidth, windowHeight, m_renderToSharedTexture, this));

	return hr;
}
#else

HRESULT Game::Initialize(Windows::UI::Core::CoreWindow^ window, float logicalDpi) 
{ 
	Initialize();

	m_window = window; 
	HRR(m_renderManager.InitDevice());

	auto windowBounds = m_window->Bounds;
#if defined(_XBOX_ONE)
	logicalDpi = logicalDpi; // Address warning 
	UINT windowWidth = 1920;
	UINT windowHeight = 1080;
#else
	UINT windowWidth = (UINT) ConvertDipsToPixels(windowBounds.Width, logicalDpi);
	UINT windowHeight = (UINT)  ConvertDipsToPixels(windowBounds.Height, logicalDpi);
#endif

	HRR(m_renderManager.OnResize(windowWidth, windowHeight, m_renderToSharedTexture, this));
	
	return S_OK;
}

#endif


// TODO push this out to the platform layer
#if defined(TREE3D12)
HRESULT Game::CreateSwapChain(DXGI_SWAP_CHAIN_DESC1* sd, IDXGIFactory4* dxgiFactory4, ID3D12CommandQueue* commandQueue, IDXGISwapChain1** swapChain)
{
	HRESULT hr = S_OK;

	//CComPtr<IDXGISwapChain1> swapChain;

	HRR(dxgiFactory4->CreateSwapChainForCoreWindow(commandQueue,
												   reinterpret_cast<IUnknown*>(m_window.Get()), sd, nullptr, swapChain));

	// This sample does not support fullscreen transitions.
	//HRR(dxgiFactory4->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER));

	//HRR(swapChain->QueryInterface(IID_PPV_ARGS(&m_pSwapChain));
	return hr;
}
#else
HRESULT Game::CreateSwapChain(DXGI_SWAP_CHAIN_DESC1* sd, IDXGIFactory2* dxgiFactory2, IDXGISwapChain1** swapChain)
{
	HRESULT hr = S_OK;

#if defined(WIN32) && !defined(TREENGINE_XBOX)
	HRR(dxgiFactory2->CreateSwapChainForHwnd(m_renderManager.GetDevice(), m_hwnd, sd, nullptr, nullptr, swapChain));
#else
	HRR(dxgiFactory2->CreateSwapChainForCoreWindow(m_renderManager.GetDevice(), reinterpret_cast<IUnknown*>(m_window.Get()), sd, nullptr, swapChain));
#endif 

	return hr;
}
#endif

HRESULT Game::Cleanup() 
{ 
	if (m_pScene)
	{
		m_pScene->CleanUpDeviceObjects();
		SafeDelete(&m_pScene);
	}

	m_renderManager.UninitDevice(); return S_OK;
}


void Game::Regenerate()
{
	HRESULT hr = S_OK;

	// Clear old stuff
	m_pScene->CleanUpDeviceObjects();
	m_renderManager.UninitGameGraphics();

	m_loader.Regenerate(m_pScene);

    // Init render manager
    hr = m_renderManager.InitGraphics(m_pScene->GetMaxInstances(), m_gameData.useShadowMaps);
	assert(SUCCEEDED(hr));		

	// Init new stuff
	hr = m_pScene->InitGraphics(m_renderManager);
    assert(SUCCEEDED(hr));
}

void Game::Update(DX::StepTimer const& timer)
{
	m_renderManager.GetRenderData().frame++;

	if (m_advanceScene)
	{
		// Clean out game state 
		m_pScene->DeleteAllChildren();
		m_renderManager.UninitGameGraphics();
		m_gameData.ResetToDefaults();

		// Determine which scene to load
		m_currentScene += m_advanceSceneAmount;
		m_currentScene = m_currentScene % m_loader.GetNumScenes();
		if (m_currentScene < 0)
		{
			m_currentScene += m_loader.GetNumScenes();
		}

		m_advanceScene = false;
		m_advanceSceneAmount = 0;

		m_timeStart = 0;

		// Load the next/prev scene
		m_loader.Load(m_currentScene, m_pScene, &m_renderManager.GetRenderData(), m_player, &m_gameData);

		// Create models and device objects
		m_resetTree = true;
	}

	// Rebuild tree if necessary
	if (m_resetTree)
	{
		Regenerate();
		m_resetTree = false;
	}

	// Update our time
	//if (m_driverType == D3D_DRIVER_TYPE_REFERENCE)
	//{
	//	m_renderManager.GetRenderData().time += (float)XM_PI * 0.0125f;
	//}
	//else
	//{
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
	//}

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
	// Reset stats
	for (int i = 0; i < MAX_FRAME_STAT; i++)
	{
		m_renderManager.GetRenderData().frameStats[i].stat = 0;
	}

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
	if (m_gameData.useShadowMaps)
	{
		m_renderManager.RenderShadowMap();
	}
}

//--------------------------------------------------------------------------------------
// Render a frame.  May be called twice for stereo rendering
//--------------------------------------------------------------------------------------
void Game::Render(bool oculus)
{
	HRESULT hr = S_OK;

	m_renderManager.GetRenderData().frameStats[FPS_STAT].stat = m_timer.GetFramesPerSecond();

	UpdateView();

	m_renderManager.Render(oculus, m_wireframe, m_gameData.useAlphaBlendedRenderTarget, m_gameData.useShadowMaps, m_showHelp,
		m_showShadowBuffer, m_renderToSharedTexture, &m_gameData.clearColor.f[0]);

	return;
}

void Game::HandleInput(bool key[256])  // WM_KEYDOWN
{
    m_player->HandleInput(key);

    const char availableKeys[] = { '0', 'Z', 'P', '#' , 'H', 'N', 'B', 'R' };
	for (char k : availableKeys)
	{
		if (key[k])
		{
			switch (k)
			{
			case ']':
				m_resetTree = true;
				m_loader._currentSeed++;
				key[k] = false;
				break;
			case '[':
				if (m_loader._currentSeed > 0)
				{
					m_resetTree = true;
					m_loader._currentSeed--;
					key[k] = false;
				}
				break;
			case '0':
				m_timeStart = 0;
				key[k] = false;
				break;
			case 'Z':
				m_showShadowBuffer = !m_showShadowBuffer;
				key[k] = false;
				break;
			case 'P':
				m_paused = !m_paused;
				key[k] = false;
				break;
			case '#':
				m_wireframe = !m_wireframe;
				key[k] = false;
				break;
			case 'H':
				m_showHelp = !m_showHelp;
				key[k] = false;
				break;
			case 'R':
				m_advanceScene = true;
				key[k] = false;
				break;
			case 'N':
				m_advanceScene = true;
				m_advanceSceneAmount = 1;
				key[k] = false;
				break;
			case 'B':
				m_advanceScene = true;
				m_advanceSceneAmount = -1;
				key[k] = false;
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


#if !defined(WIN32) && !defined(_XBOX_ONE)
// Method to convert a length in device-independent pixels (DIPs) to a length in physical pixels.
float Game::ConvertDipsToPixels(float dips, float logicalDpi )
{
	static const float dipsPerInch = 96.0f;

	return floor(dips * logicalDpi / dipsPerInch + 0.5f); // Round to nearest integer.
																				 //DisplayInformation
	//return floor(dips * DisplayProperties::LogicalDpi / dipsPerInch + 0.5f); // Round to nearest integer.
}
#endif


