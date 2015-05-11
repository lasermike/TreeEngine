#pragma once
#include <vector>

#include "Primitive.h"
#include "sceneroot.h"
#include "Tree.h"
#include "bitmapfont.h"
#include "RenderData.h"
#include "LSystemModelGenerator.h"
#include "OrbitCamera.h"
#include "GameLoader.h"
#include "StepTimer.h"

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
#ifndef _TREE_CLASSIC
	HRESULT Initialize(Windows::UI::Core::CoreWindow^ window) { _window = window; return InitDevice(); }
#else
	HRESULT Initialize() { ASSERT(false); return E_NOTIMPL; }
	HRESULT DetectOculus(bool& detected);
#endif
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
	void Regenerate();

	HRESULT InitDevice();
	void CleanupDevice();
	static float ConvertDipsToPixels(float dips);

	HRESULT UpdateProjection(XMFLOAT4X4* pProjMat);
	void UpdateCamera(DX::StepTimer const& timer);
	void UpdateView();
	void Select(WorldObject* pSelected);
	void Select(int index);

	void BuildShadowTransform();
	void DrawSceneToShadowMap();

	// Direct3D Objects
	D3D_FEATURE_LEVEL                   m_featureLevel;

	// Game state
	RenderData							_renderData;
	DX::StepTimer						_timer;
	double								_timeStart;
	double								_timeCurrent;
    XSF::OrbitCamera*					_camera;

#ifndef _TREE_CLASSIC
	Platform::Agile<Windows::UI::Core::CoreWindow>		_window;
#else
	HWND								_hwnd;

#endif
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
	float								_rotateSpeed;
	float								_dollySpeed;
	bool								_paused;
	bool								_wireframe;
	bool								_showHelp;
	ID3D11Buffer*                       _pCBChangeOnResize;

	XSF::BitmapFont*					_bitmapFont;

	WorldObject*						_selection;
};


