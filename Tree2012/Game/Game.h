#pragma once
#include <vector>

#include "Primitive.h"
#include "sceneroot.h"
#include "Tree.h"
#include "bitmapfont.h"
#include "LSystemModelGenerator.h"
#include "GameLoader.h"
#include "StepTimer.h"
#include "Player.h"
#include "RenderManager.h"

#ifndef WIN32
#include "agile.h"
using namespace Microsoft::WRL;
#endif

class ThreadPool;
class BitmapFont;
interface IInputManager;

enum DisplayMode
{
	Monitor = 0,
	Oculus
};

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

class Game 
{
public:

	Game(IInputManager* inputMgr);
	~Game();

	// Initialization and management
#ifdef WIN32
	HRESULT Initialize(HWND hwnd, bool renderToSharedTexture);
#else
	HRESULT Initialize(Windows::UI::Core::CoreWindow^ window, float logicalDpi);
#endif

	HRESULT Cleanup() { CleanupDevice(); return S_OK; }
	HRESULT OnResize(UINT windowWidth, UINT windowHeight);

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

	// Accessor methods for Oculus
	ID3D11Device* GetDevice() { return m_pd3dDevice1; }
    XSF::D3DDeviceContext* GetContext() { return m_pImmediateContext; }
    ID3D11Texture2D* GetBackBuffer() { return m_pSharedRenderToTexture; }
    IDXGISwapChain* GetSwapChain() { return m_pSwapChain; }
	HRESULT UpdateProjection(XMFLOAT4X4* pProjMat);

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

	HRESULT InitDevice();
	void CleanupDevice();

	void UpdateView();

	void BuildShadowTransform();
	void DrawSceneToShadowMap();

	// Managers
	GameLoader							m_loader;
	RenderManager						m_renderManager;

	// Owned objectes
	ThreadPool*							m_threadPool;
	SceneRoot*							m_pScene;
	XSF::BitmapFont*					m_bitmapFont;
	Player*								m_player;

	// Unowned objects
	IInputManager*						m_inputMgr;   

	// Game state
	DX::StepTimer						m_timer;
	double								m_timeStart;
	double								m_timeCurrent;
	int								    m_currentScene;
	int								    m_advanceScene;

#ifdef WIN32
	HWND								m_hwnd;
#else
	Platform::Agile<Windows::UI::Core::CoreWindow>		m_window;
#if !defined(_XBOX_ONE)
	float ConvertDipsToPixels(float dips, float logicalDpi);
#endif // XBOX
#endif //Classic

	// Direct3D Objects
	D3D_DRIVER_TYPE                     m_driverType;
	D3D_FEATURE_LEVEL                   m_featureLevel;
	CComPtr<XSF::D3DDevice>             m_pd3dDevice;
	CComPtr<ID3D11Device1>              m_pd3dDevice1;
	CComPtr<XSF::D3DDeviceContext>      m_pImmediateContext;
	CComPtr<ID3D11DeviceContext1>       m_pImmediateContext1;
	CComPtr<IDXGISwapChain>             m_pSwapChain;
	CComPtr<IDXGISwapChain1>            m_pSwapChain1;
	CComPtr<ID3D11RenderTargetView>     m_pRenderTargetView;
	CComPtr<ID3D11Texture2D>            m_pSharedRenderToTexture;
	DisplayMode							m_displayMode;
	CComPtr<ID3D11Texture2D>            m_pDepthStencil;
	CComPtr<ID3D11DepthStencilView>		m_pDepthStencilView;

	CComPtr<ID3D11RasterizerState>		m_rasterState;
	CComPtr<ID3D11Buffer>               m_pCBChangeOnResize;

	D3D11_VIEWPORT						m_viewPort;
	bool								m_enableMsaa;

	bool								m_renderToSharedTexture;

	bool								m_resetTree;
	bool								m_showShadowBuffer;
	bool								m_paused;
	bool								m_wireframe;
	bool								m_showHelp;

};


