#pragma once
#include <vector>

#include "Primitive.h"
#include "sceneroot.h"
#include "Tree.h"

using namespace Microsoft::WRL;

class Game 
{
public:
	Game();
	~Game();

	// Initialization and management
	HRESULT Initialize(Windows::UI::Core::CoreWindow^ window) { _window = window; return InitDevice(); }
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
	static float ConvertDipsToPixels(float dips);

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

	Platform::Agile<Windows::UI::Core::CoreWindow>		_window;
	D3D_DRIVER_TYPE                     _driverType;
	D3D_FEATURE_LEVEL                   _featureLevel;
	ID3D11Device*                       _pd3dDevice;
	ID3D11Device1*                      _pd3dDevice1;
	ID3D11DeviceContext*                _pImmediateContext;
	ID3D11DeviceContext1*               _pImmediateContext1;
	IDXGISwapChain*                     _pSwapChain;
	IDXGISwapChain1*                    _pSwapChain1;
	ID3D11RenderTargetView*             _pRenderTargetView;
	ID3D11Texture2D*                    _pDepthStencil;
	ID3D11DepthStencilView*             _pDepthStencilView;
	ID3D11RasterizerState*				_rasterState;
	bool								_enableMsaa;

	XMFLOAT4X4							_World;
	XMFLOAT4X4                          _Projection;

	Primitive*							_pPlane;
	SceneRoot*							_pScene;
	Tree*								_pTree;
	ULONGLONG							_timeStart;
	std::vector<unsigned int>			_seeds;
	int									_currentSeed;
	bool								_resetTree;

	ID3D11Buffer*                       _pCBChangeOnResize;
};

