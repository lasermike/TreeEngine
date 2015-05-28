#pragma once
#include <vector>

#include "Primitive.h"
#include "sceneroot.h"
#include "Tree.h"
#include "bitmapfont.h"
#include "RenderData.h"
#include "LSystemModelGenerator.h"
#include "GameLoader.h"
#include "StepTimer.h"
#include "Player.h"

#ifndef _TREE_CLASSIC
#include "agile.h"
using namespace Microsoft::WRL;
#endif

class BitmapFont;

typedef enum DisplayMode
{
	Monitor = 0,
	Oculus
};



class Game 
{
public:
	Game();
	~Game();

	// Initialization and management
#ifdef _TREE_CLASSIC
	HRESULT Initialize(HWND hwnd);
#else
	HRESULT Initialize(Windows::UI::Core::CoreWindow^ window);
#endif

	HRESULT Cleanup() { CleanupDevice(); return S_OK; }
	HRESULT OnResize(UINT windowWidth, UINT windowHeight);

	// Basic game loop
	void Tick(bool key[256]);
	void Render(bool present);

	// Rendering helpers
	void Clear();
	void Present();

	void Suspend();
	void Resume();

	const RenderData& GetRenderData() { return _renderData; }
	ID3D11Device* GetDevice() { return _pd3dDevice1; }
    XSF::D3DDeviceContext* GetContext() { return _pImmediateContext; }
    ID3D11Texture2D* GetBackBuffer() { return _pDepthStencil; }
    IDXGISwapChain* GetSwapChain() { return _pSwapChain; }
	HRESULT UpdateProjection(XMFLOAT4X4* pProjMat);

    Player* GetPlayer() { return _player; }

    //typedef std::function<HRESULT(ProjectionData& projectionData)> ResizeFunc;
    //void SetResizeHandler(ResizeFunc func)
    //{
    //    _resizeHandler = func;
    //}
private:

	void Update(DX::StepTimer const& timer);
	void Regenerate();
	void HandleInput(bool key[256]);

	HRESULT InitDevice();
	void CleanupDevice();

    //HRESULT StandardResizeHandler();
	void UpdateView();

	void BuildShadowTransform();
	void DrawSceneToShadowMap();

	// Direct3D Objects
	D3D_FEATURE_LEVEL                   m_featureLevel;

    //ResizeFunc _resizeHandler;

	// Game state
	RenderData							_renderData;
	DX::StepTimer						_timer;
	double								_timeStart;
	double								_timeCurrent;

#ifdef _TREE_CLASSIC
	HWND								_hwnd;
#else
	Platform::Agile<Windows::UI::Core::CoreWindow>		_window;
#if !defined(_XBOX_ONE)
	float ConvertDipsToPixels(float dips);
#endif // XBOX
#endif //Classic

	D3D_DRIVER_TYPE                     _driverType;
	D3D_FEATURE_LEVEL                   _featureLevel;
	XSF::D3DDevice*                     _pd3dDevice;
	ID3D11Device1*                      _pd3dDevice1;
	XSF::D3DDeviceContext*              _pImmediateContext;
	ID3D11DeviceContext1*               _pImmediateContext1;
	IDXGISwapChain*                     _pSwapChain;
	IDXGISwapChain1*                    _pSwapChain1;
	ID3D11RenderTargetView*             _pRenderTargetView;
	DisplayMode							_displayMode;
	CComPtr<ID3D11Texture2D>            _pDepthStencil;
	CComPtr<ID3D11DepthStencilView>		_pDepthStencilView;

	ID3D11RasterizerState*				_rasterState;
	D3D11_VIEWPORT						_viewPort;
	bool								_enableMsaa;

	GameLoader							_loader;
	SceneRoot*							_pScene;

	bool								_resetTree;
	bool								_showShadowBuffer;
	bool								_paused;
	bool								_wireframe;
	bool								_showHelp;
	ID3D11Buffer*                       _pCBChangeOnResize;

	XSF::BitmapFont*					_bitmapFont;

	Player*								_player;
};


