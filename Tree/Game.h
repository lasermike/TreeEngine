#pragma once
#include <vector>

#include "Primitive.h"
#include "sceneroot.h"
#include "Tree.h"
//class Primitive;
//class Tree;
//class SceneRoot;

class Game 
{
public:
	Game();
	~Game();

	// Initialization and management
	HRESULT Initialize(HWND window) { g_hWnd = window; return InitDevice(); }
	HRESULT Cleanup() { CleanupDevice(); return S_OK; }
	HRESULT OnResize();
	void OnKeydown(UINT key);

	// Basic game loop
	void Tick();
	void Render();

	// Rendering helpers
	void Clear();
	void Present();

	void Suspend();
	void Resume();

private:

	//void Update(DX::StepTimer const& timer);

	HRESULT InitDevice();
	void CleanupDevice();

	//void CreateDevice();
	//void CreateResources();

	// Direct3D Objects
	D3D_FEATURE_LEVEL                                  m_featureLevel;
	//Microsoft::WRL::ComPtr<ID3D11DeviceX>              m_d3dDevice;
	//Microsoft::WRL::ComPtr<ID3D11DeviceContextX>       m_d3dContext;

	// Rendering resources
	//Microsoft::WRL::ComPtr<IDXGISwapChain1>            m_swapChain;
	//Microsoft::WRL::ComPtr<ID3D11RenderTargetView>     m_renderTargetView;
	//Microsoft::WRL::ComPtr<ID3D11DepthStencilView>     m_depthStencilView;
	//Microsoft::WRL::ComPtr<ID3D11Texture2D>            m_depthStencil;

	// Game state
	//INT64                                              m_frame;
	//DX::StepTimer                                      m_timer;

	HWND                                g_hWnd = nullptr;
	D3D_DRIVER_TYPE                     g_driverType = D3D_DRIVER_TYPE_NULL;
	D3D_FEATURE_LEVEL                   g_featureLevel = D3D_FEATURE_LEVEL_11_0;
	ID3D11Device*                       g_pd3dDevice = nullptr;
	ID3D11Device1*                      g_pd3dDevice1 = nullptr;
	ID3D11DeviceContext*                g_pImmediateContext = nullptr;
	ID3D11DeviceContext1*               g_pImmediateContext1 = nullptr;
	IDXGISwapChain*                     g_pSwapChain = nullptr;
	IDXGISwapChain1*                    g_pSwapChain1 = nullptr;
	ID3D11RenderTargetView*             g_pRenderTargetView = nullptr;
	ID3D11Texture2D*                    g_pDepthStencil = nullptr;
	ID3D11DepthStencilView*             g_pDepthStencilView = nullptr;
	ID3D11RasterizerState*				g_rasterState = nullptr;
	bool								g_enableMsaa = true;

	XMFLOAT4X4							g_World;
	XMFLOAT4X4                          g_Projection;

	Primitive*							g_pPlane;
	SceneRoot*							g_pScene;
	Tree*								g_pTree;
	ULONGLONG							g_timeStart = 0;
	std::vector<unsigned int>			g_seeds;
	int									g_currentSeed = 0;
	bool								g_resetTree = true;

	ID3D11Buffer*                       g_pCBChangeOnResize = nullptr;
};

