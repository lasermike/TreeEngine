#pragma once
#include <vector>

#include "Primitive.h"
#include "sceneroot.h"
#include "Tree.h"
#include "LSystemModelGenerator.h"
#include "GameLoader.h"
#include "StepTimer.h"
#include "Player.h"
#include "RenderManager.h"

#if !defined(WIN32) || defined(TREENGINE_XBOX)
#include "agile.h"
using namespace Microsoft::WRL;
#endif

class ThreadPool;
class BitmapFont;
interface IInputManager;

struct FrameInputData
{
	UINT frame;
	bool key[256];

	FrameInputData()
	{
		frame = 0;
		memset(key, 0, sizeof(bool) * _countof(key));
	}
};

class Game : public SwapChainCreator
{
public:

	Game(IInputManager* inputMgr);
	~Game();

	// Initialization and management
#if defined(WIN32) && !defined(TREENGINE_XBOX)
	HRESULT Initialize(HWND hwnd, bool renderToSharedTexture);
#else
	HRESULT Initialize(Windows::UI::Core::CoreWindow^ window, float logicalDpi);
#endif
	HRESULT CreateSwapChain(DXGI_SWAP_CHAIN_DESC1* sd, IDXGIFactory2* dxgiFactory2, IDXGISwapChain1** swapChain); // TODO: move to platform specific code

	HRESULT UpdateProjection(XMFLOAT4X4* pProjMat) { return m_renderManager.UpdateProjection(pProjMat); }
	HRESULT OnResize(UINT width, UINT height) { return m_renderManager.OnResize(width, height, m_renderToSharedTexture, this); }

	HRESULT Cleanup();

	// Basic game loop
	void ComputeCPU();
	void ComputeGPU();
	void Render(bool present);

	// Rendering helpers
	void Clear();
	void Present();

	void Suspend();
	void Resume();

	RenderManager& GetRenderManager() { return m_renderManager; }

    Player* GetPlayer() { return m_player; }

	// Allow SSE members
	void* operator new(size_t size) 
	{ 
		return _aligned_malloc(size, 16); 
	}
	void operator delete(void* mem) { return _aligned_free(mem); }

private:

	HRESULT Initialize();

	void Update(DX::StepTimer const& timer);
	void Regenerate();
	void HandleInput(bool key[256]);


	void UpdateView();

	// Managers
	GameLoader							m_loader;
	RenderManager						m_renderManager;

	// Owned objectes
	ThreadPool*							m_threadPool;
	SceneRoot*							m_pScene;
	Player*								m_player;

	// Unowned objects
	IInputManager*						m_inputMgr;   

	// Game state
	DX::StepTimer						m_timer;
	double								m_timeStart;
	double								m_timeCurrent;
	int								    m_currentScene;
	bool							    m_advanceScene;
	int								    m_advanceSceneAmount;

#if defined(WIN32) && !defined(TREENGINE_XBOX)
	HWND								m_hwnd;
#else
	Platform::Agile<Windows::UI::Core::CoreWindow>		m_window;
#if !defined(_XBOX_ONE)
	float ConvertDipsToPixels(float dips, float logicalDpi);
#endif // XBOX
#endif //Classic

	bool								m_renderToSharedTexture;

	bool								m_resetTree;
	bool								m_showShadowBuffer;
	bool								m_paused;
	bool								m_wireframe;
	bool								m_showHelp;

	GameData							m_gameData;
};


