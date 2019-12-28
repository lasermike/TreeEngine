// TreeWin32Dll.cpp : Defines the exported functions for the DLL application.
//

#include "pch.h"
#include "Game.h"
#include "InputManager.h"
#include "d3d9.h"

Game* g_game = nullptr;
InputManager g_inputManager;

LRESULT WINAPI MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

WNDCLASSEX g_wc = { sizeof(WNDCLASSEX), CS_CLASSDC, MsgProc, 0L, 0L,
GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"Foo", NULL };

D3DFORMAT ConvertDXGIToD3D9Format(DXGI_FORMAT format)
{
	switch (format)
	{
	case DXGI_FORMAT_B8G8R8A8_UNORM:
		return D3DFMT_A8R8G8B8;
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		return D3DFMT_A8R8G8B8;
	case DXGI_FORMAT_B8G8R8X8_UNORM:
		return D3DFMT_X8R8G8B8;
	case DXGI_FORMAT_R8G8B8A8_UNORM:
		return D3DFMT_A8B8G8R8;
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		return D3DFMT_A8B8G8R8;
	default:
		return D3DFMT_UNKNOWN;
	};
}


const UINT backBufferWidth = 1600;
const UINT backBufferHeight = 1080;
RECT g_rc = { 0, 0, backBufferWidth, backBufferHeight };

extern "C" __declspec(dllexport) LPVOID WINAPI InitializeScene()
{
	AdjustWindowRect(&g_rc, WS_OVERLAPPEDWINDOW, FALSE);
	RegisterClassEx(&g_wc);
	HWND hWnd = CreateWindow(L"Foo", L"Trees", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, g_rc.right - g_rc.left, g_rc.bottom - g_rc.top, NULL, NULL, g_wc.hInstance, NULL);

	if (!hWnd)
	{
		return FALSE;
	}

	HRESULT hr = S_OK;
	D3D11_TEXTURE2D_DESC textureDesc;
	D3DPRESENT_PARAMETERS d3dpp;

	IDXGIResource* pResource = nullptr;
	IDXGISwapChain* pSwapChain = nullptr;
	IDirect3D9Ex* pD3D9 = nullptr;
	IDirect3DTexture9* pTexture = nullptr;
	IDirect3DSurface9* pD3D9Surface = nullptr;
	HANDLE hSharedHandle = nullptr;
	D3DFORMAT d3d9Format = D3DFMT_A8R8G8B8;
	IDirect3DDevice9Ex* pD3D9Device = nullptr;
	//CComPtr<ID3D11Texture2D> pBackBuffer;

	// Game
	g_game = new Game(&g_inputManager);

	if (FAILED(g_game->Initialize(hWnd, true)))
	{
		g_game->Cleanup();
		return 0;
	}

	// Get a D3D9 handle to the back buffer
	HRC(g_game->GetRenderManager().GetBackBuffer()->QueryInterface(__uuidof(IDXGIResource), (void**)&pResource));
	HRC(pResource->GetSharedHandle(&hSharedHandle));
	g_game->GetRenderManager().GetBackBuffer()->GetDesc(&textureDesc);

	// D3D9
	HRC(Direct3DCreate9Ex(D3D_SDK_VERSION, &pD3D9));
	ZeroMemory(&d3dpp, sizeof(d3dpp));
	d3dpp.Windowed = TRUE;
	d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3dpp.hDeviceWindow = GetDesktopWindow();
	d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
	HRC(pD3D9->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp.hDeviceWindow,
		D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
		&d3dpp, nullptr, &pD3D9Device));

	// Create D3D9 view of back buffer using shared handle
	d3d9Format = ConvertDXGIToD3D9Format(textureDesc.Format);
	HRC(pD3D9Device->CreateTexture(textureDesc.Width, textureDesc.Height, 1, D3DUSAGE_RENDERTARGET, d3d9Format, D3DPOOL_DEFAULT, &pTexture, &hSharedHandle));
	HRC(pTexture->GetSurfaceLevel(0, &pD3D9Surface));

Cleanup:

	SafeRelease(&pResource);
	//SafeRelease(&pBackBuffer);
	SafeRelease(&pTexture);
	SafeRelease(&pD3D9Device);
	SafeRelease(&pD3D9);

	return pD3D9Surface;
}

extern "C" __declspec(dllexport) void WINAPI RenderScene(LPSIZE pSize)
{
	// Run game 
	// TODO: sdfjlk


	g_game->ComputeCPU();
	g_game->ComputeGPU();

	g_game->Render(false);

	pSize->cx = backBufferWidth;
	pSize->cy = backBufferHeight;
}


extern "C" __declspec(dllexport) VOID WINAPI ReleaseScene()
{
	UnregisterClass(NULL, g_wc.hInstance);

	if (g_game)
	{
		delete g_game;
		g_game = nullptr;

	}


}
