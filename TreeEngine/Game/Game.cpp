#include "pch.h"
#include "Game.h"
#include "StockRenderStates.h"
#include "SceneRoot.h"
#include "Tree.h"
#include "TreeModelGenerator.h"
#include "Primitive.h"
#include "ShadowMap.h"
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
    m_needsResize = false;
    m_nextScreenWidth = 0;
    m_nextScreenHeight = 0;
    m_renderToSharedTexture = false;
    m_paused = false;
    m_wireframe = false;
    m_showHelp = false;
    m_is12Driver = true;
    m_rotateLights = false;
    m_timeStart = 0;
    m_resetTree = true;
    m_showShadowBuffer = false;
    m_advanceScene = 0;
    m_advanceSceneAmount = 0;
    m_currentScene = 0;
    m_reloadDevice = false;
    m_renderPlatformDLL = nullptr;

    m_player = nullptr;
    m_threadPool = nullptr;
    assert(m_inputMgr);
}

void Game::UpdateViewMatrix()
{
}

Game::~Game()
{
    Cleanup();
    SafeDelete(&m_pScene);
    SafeDelete(&m_threadPool);
    SafeDelete(&m_player);
}


HRESULT Game::Initialize(bool renderToSharedTexture)
{
    m_renderToSharedTexture = renderToSharedTexture;


    // Rendering defaults
    m_renderManager.GetRenderData().frame = 0;
    m_renderManager.GetRenderData().projectionData.fov = XM_PIDIV4;
    m_renderManager.GetRenderData().projectionData.nearClippingPlane = .2f;
    m_renderManager.GetRenderData().projectionData.farClippingPlane = 30.0f;
    m_renderManager.GetRenderData().clearColor = XMFLOAT4(0, 0, 0, 0);

    GameCommon::SetContentFileRoot();

    // Init vertex/index buffer
    m_pScene = new SceneRoot();

    // Init scene bounds.
    // Estimatation.    
    // Ideally would loop through all world space vertices
    m_renderManager.GetRenderData().mSceneBounds.Center = XMFLOAT3(0.0f, 3.0f, 0.0f);
    m_renderManager.GetRenderData().mSceneBounds.Radius = 5; //sqrtf(5.0f*5.0f + 5.0f*5.0f);

    // Create player
    WorldObjectParams* playerParams = new WorldObjectParams(NullGeneratorType);
    playerParams->position = XMFLOAT3(-4.0f, 1.5f, -4.0f);
    XMStoreFloat4(&playerParams->rotation, XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 1), XM_PIDIV4));
    m_player = new Player(playerParams);

    HRR(ReloadDevice());

    m_currentScene = 0;
    m_loader.Load(m_currentScene, m_pScene, &m_renderManager.GetRenderData(), m_player, &m_gameData);

    m_needsResize = true;

    // Create thread pool
    //may return 0 when not able to detect
    unsigned concurentThreadsSupported = std::thread::hardware_concurrency();
    m_threadPool = new ThreadPool(concurentThreadsSupported ? concurentThreadsSupported : 1);

    return S_OK;
}


HRESULT Game::Cleanup()
{
    if (m_pScene)
    {
        m_pScene->CleanUpDeviceObjects();
        SafeDelete(&m_pScene);
    }

    m_renderManager.UninitDevice();

    return S_OK;
}

HRESULT Game::ReloadDevice()
{
    HRESULT hr = S_OK;
    bool firstTimeLoad = m_renderPlatformDLL == nullptr;

    // Clear old stuff
    if (m_renderPlatformDLL)
    {
        if (m_pScene)
        {
            m_pScene->CleanUpDeviceObjects();
        }

        m_renderManager.UninitGameLevelGraphics();

        m_renderManager.UninitDevice();

#if defined(TREENGINE_WIN32)
        m_renderManager.GetPlatform()->SetWindow(nullptr);
#else
        m_renderManager.GetPlatform()->SetWindow(nullptr, m_logicalDpi);
#endif

        FreeLibrary(m_renderPlatformDLL);
        m_renderPlatformDLL = nullptr;

    }

    //if (!firstTimeLoad)
    //{
    //    m_is12Driver = !m_is12Driver;
    //}

    const wchar_t* dllFilename =
#if defined(TREE3D_CLASSIC)
        L"RenderPlatform12.dll";
        m_renderPlatformDLL = ::LoadLibrary(dllFilename);
#elif defined(TREE_XBOX)
        L"RenderPlatform12Xbox.dll";
        m_renderPlatformDLL = ::LoadLibrary(dllFilename);
#else
        m_is12Driver ? L"RenderPlatform12UWP.dll" : L"RenderPlatform11UWP.dll";
    m_renderPlatformDLL = ::LoadPackagedLibrary(dllFilename, 0);
#endif

    assert(m_renderPlatformDLL != nullptr);

    m_renderManager.SetPlatform(m_renderPlatformDLL);

#if defined(TREENGINE_WIN32)
    m_renderManager.GetPlatform()->SetWindow(m_hwnd);
#else
    m_renderManager.GetPlatform()->SetWindow(m_window.Get(), m_logicalDpi);
#endif


    m_renderManager.InitDevice();

    m_needsResize = true;

    if (m_pScene)
    {
        if (m_needsResize)
        {
            m_renderManager.OnResize(m_nextScreenWidth, m_nextScreenHeight, m_renderToSharedTexture/*, this*/);
            m_needsResize = false;
        }

        // Init render manager
        hr = m_renderManager.InitGameLevelGraphics(m_pScene->GetMaxInstances(), m_gameData.useShadowMaps);
        assert(SUCCEEDED(hr));

        // Init new stuff
        hr = m_pScene->InitGraphics(m_renderManager);
        assert(SUCCEEDED(hr));
    }

    return hr;
}

void Game::Regenerate()
{
    HRESULT hr = S_OK;

    // Clear old stuff
    m_pScene->CleanUpDeviceObjects();
    m_renderManager.UninitGameLevelGraphics();

    m_loader.Regenerate(m_pScene);

    // Init render manager
    hr = m_renderManager.InitGameLevelGraphics(m_pScene->GetMaxInstances(), m_gameData.useShadowMaps);
    assert(SUCCEEDED(hr));

    // Init new stuff
    hr = m_pScene->InitGraphics(m_renderManager);
    assert(SUCCEEDED(hr));
}

void Game::Update(DX::StepTimer const& timer)
{
    PIXScopedEvent(TREE_COLOR_DRAW_TEXT, L"Update");

    // Reset stats
    for (int i = 0; i < MAX_FRAME_STAT; i++)
    {
        m_renderManager.GetRenderData().frameStats[i].stat = 0;
    }

    m_renderManager.GetRenderData().frame++;

    if (m_reloadDevice)
    {
        HR(ReloadDevice());
        m_reloadDevice = false;
    }

    m_renderManager.GetRenderData().frameStats[DRIVER_12_STAT].stat = m_is12Driver;

    if (m_needsResize)
    {
        m_renderManager.OnResize(m_nextScreenWidth, m_nextScreenHeight, m_renderToSharedTexture/*, this*/);
        m_needsResize = false;
    }



    if (m_advanceScene)
    {
        // Clean out game state 
        m_pScene->DeleteAllChildren();
        m_renderManager.UninitGameLevelGraphics();
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

        // Recreate backbuffer with level specific clear color
        m_needsResize = true;
    }

    // Rebuild tree if necessary
    if (m_resetTree)
    {
        Regenerate();
        m_resetTree = false;
    }

    // Update our time
    if (m_timeStart == 0)
    {
        m_timeStart = timer.GetTotalSeconds();
        m_timeCurrent = 0;
    }
    else if (!m_paused)
    {
        m_timeCurrent += timer.GetElapsedSeconds();
    }
    m_renderManager.GetRenderData().time = (float)m_timeCurrent;


    if (!m_paused && m_rotateLights)
    {
        // Light rotation
        XMVECTOR quat = XMQuaternionRotationNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 1.f), 0.05f); //m_timeCurrent / 1000

        XMVECTOR vec = XMLoadFloat3(&m_renderManager.GetRenderData().dirLights[0].Direction);
        vec = XMVector3Rotate(vec, quat);
        XMStoreFloat3(&m_renderManager.GetRenderData().dirLights[0].Direction, vec);
    }

    HR(m_renderManager.BeginNewFrame());

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
    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"Frame begin");
    PIXScopedEvent(TREE_COLOR_DRAW_TEXT, L"ComputeCPU");

    FrameInputData& inputData = m_inputMgr->GetFrameInput(0);
    HandleInput(inputData.key);

    m_timer.Tick([&]()
    {
        Update(m_timer);
    });

    PIXEndEvent();  // ComputeCPU
}

void Game::ComputeGPU()
{
    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"ComputeGPU");

    //Render shadow map
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
    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"Render");

    m_renderManager.GetRenderData().frameStats[FPS_STAT].stat = m_timer.GetFramesPerSecond();

    XMStoreFloat4x4(&m_renderManager.GetRenderData().view, m_player->GetViewMatrix());
    m_renderManager.GetRenderData().eyePos = m_player->GetEyePosition();

    m_renderManager.Render(oculus, m_wireframe, m_gameData.useAlphaBlendedRenderTarget, m_gameData.useShadowMaps, m_showHelp,
        m_showShadowBuffer, m_renderToSharedTexture, &m_gameData.clearColor.x);

    PIXEndEvent();  // Render
    PIXEndEvent();  // Frame begin
}

void Game::HandleInput(bool key[256])  // WM_KEYDOWN
{
    m_player->HandleInput(key);

    const char availableKeys[] = { '0', 'Z', 'P', 'M' , 'H', 'N', 'B', 'R', '1', '2', '3' };
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
            case 'M':
                m_wireframe = !m_wireframe;
                m_renderManager.GetRenderData().wireframe = m_wireframe;
                key[k] = false;
                m_reloadDevice = true;
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
            case '2':
                m_reloadDevice = true;
                break;
            case '3':
                m_rotateLights = !m_rotateLights;
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



