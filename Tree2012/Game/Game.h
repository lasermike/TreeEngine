#pragma once
#include <vector>

#include "Primitive.h"
#include "sceneroot.h"
#include "Tree.h"
#include "agile.h"
#include "bitmapfont.h"
#include "StepTimer.h"
#include "RenderData.h"

using namespace Microsoft::WRL;

class BitmapFont;


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

	void Update(DX::StepTimer const& timer);

	HRESULT InitDevice();
	void CleanupDevice();
	static float ConvertDipsToPixels(float dips);

	void BuildShadowTransform();

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
	RenderData							_renderData;
	DX::StepTimer						m_timer;
	float								m_fps;
	//float								m_time;

	Platform::Agile<Windows::UI::Core::CoreWindow>		_window;
	D3D_DRIVER_TYPE                     _driverType;
	D3D_FEATURE_LEVEL                   _featureLevel;
	XSF::D3DDevice*                     _pd3dDevice;
	ID3D11Device1*                      _pd3dDevice1;
	XSF::D3DDeviceContext*              _pImmediateContext;
	ID3D11DeviceContext1*               _pImmediateContext1;
	IDXGISwapChain*                     _pSwapChain;
	IDXGISwapChain1*                    _pSwapChain1;
	ID3D11RenderTargetView*             _pRenderTargetView;
	ID3D11Texture2D*                    _pDepthStencil;
	ID3D11DepthStencilView*             _pDepthStencilView;
	ID3D11RasterizerState*				_rasterState;
	D3D11_VIEWPORT						_viewPort;
	bool								_enableMsaa;

	Primitive*							_pPlane;
	SceneRoot*							_pScene;
	std::vector<Tree*>					_trees;
	ULONGLONG							_timeStart;
	std::vector<unsigned int>			_seeds;
	int									_currentSeed;
	bool								_resetTree;

	ID3D11Buffer*                       _pCBChangeOnResize;

	XSF::BitmapFont*					_bitmapFont;

	//DirectionalLight					_dirLights[1];

};


