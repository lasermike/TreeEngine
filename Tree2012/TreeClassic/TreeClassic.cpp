// TreeClassic.cpp : Defines the entry point for the application.
//

#include "pch.h"
#include "TreeClassic.h"
#include "Game.h"
#include <OVR_CAPI_D3D.h>
#include <Kernel/OVR_System.h>
#include <Extras/OVR_Math.h>

using namespace OVR;

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;								// current instance
TCHAR szTitle[MAX_LOADSTRING];					// The title bar text
TCHAR szWindowClass[MAX_LOADSTRING];			// the main window class name

// Tree engine
Game* g_game = nullptr;

HWND hWnd = nullptr;

// Oculus specific
ovrHmd HMD = nullptr;
bool debugOvr = false;
bool windowedOvr = false;
OVR::Sizei WinSize;

// Forward declarations of functions included in this code module:
HRESULT				CreateOculusDevice(bool& detected);

ATOM				MyRegisterClass(HINSTANCE hInstance);
BOOL				InitInstance(HINSTANCE, int);
LRESULT CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK	About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY _tWinMain(_In_ HINSTANCE hInstance,
                       _In_opt_ HINSTANCE hPrevInstance,
                       _In_ LPTSTR    lpCmdLine,
                       _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    // Initialize global strings
    LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadString(hInstance, IDC_TREECLASSIC, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    // Main message loop:
    MSG msg = {0};
    while (WM_QUIT != msg.message)
    {
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
    		g_game->Render();
        }
    }

    g_game->Cleanup();

    return (int) msg.wParam;
}


HRESULT OvrResizeHandler(ProjectionData& projectionData)
{
    return S_OK;
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance; // Store instance handle in our global variable

    bool oculusDetected = false;
    HR(CreateOculusDevice(oculusDetected));

	// Found a regular or debug Oculus device
	if (HMD && !debugOvr)
	{
        ovrSizei ovrWinSize = { HMD->Resolution.w / 2, HMD->Resolution.h / 2 };
		Recti vp(Recti(Vector2i(0), ovrWinSize));

        WNDCLASSW wc; memset(&wc, 0, sizeof(wc));
        wc.lpszClassName = L"OVRAppWindow";
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = WndProc;
        wc.cbWndExtra = sizeof(struct DirectX11 *);
        RegisterClassW(&wc);

        const DWORD wsStyle = WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME;
        RECT winSize = { 0, 0, vp.w, vp.h };
        AdjustWindowRect(&winSize, wsStyle, FALSE);
        hWnd = CreateWindowW(L"OVRAppWindow", L"Tree Engine VR", wsStyle | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT, winSize.right - winSize.left, winSize.bottom - winSize.top,
            NULL, NULL, hInstance, NULL);
        if (!hWnd) 
            return(false);
        //SetWindowLongPtr(hWnd, 0, LONG_PTR(this));

        WinSize = vp.GetSize();
	}
	else
	{
        RECT rc = { 0, 0, 1600, 1200};
    AdjustWindowRect( &rc, WS_OVERLAPPEDWINDOW, FALSE );
		hWnd = CreateWindow(L"OVRAppWindow", szTitle, WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, hInstance, NULL);
	}

    if (!hWnd)
    {
        return FALSE;
    }

	g_game = new Game();

    //if (HMD && !debugOvr)
    //{
    //    g_game->SetResizeHandler(&OvrResizeHandler);
    //}


	if (FAILED(g_game->Initialize(hWnd)))
    {
		g_game->Cleanup();
        return 0;
    }



    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return TRUE;
}

void OnWindowSizeChanged()
{
    ASSERT(hWnd);

	UINT windowWidth = 0; 
	UINT windowHeight = 0;
	RECT rect = {0};
	GetClientRect(hWnd, &rect);
	windowWidth = rect.right - rect.left;
	windowHeight = rect.bottom - rect.top;

	g_game->OnResize(windowWidth, windowHeight);
}

//--------------------------------------------------------------------------------------
// Create Oculus interface if possible
//--------------------------------------------------------------------------------------
HRESULT CreateOculusDevice(bool& detected)
{
    detected = false;

    //OVR::System::Init(OVR::Log::ConfigureDefaultLog(OVR::LogMask_All));

    // Initializes LibOVR, and the Rift
    ovrResult result = ovr_Initialize(nullptr);
    if (result != ovrSuccess)
    {
        LOG("Unable to initialize libOVR."); 
        return 0; 
    }

    result = ovrHmd_Create(0, &HMD);
	if (HMD)
	{
		LOG("Oculus Rift device created."); 
	}
	else 
    {
        result = ovrHmd_CreateDebug(ovrHmd_DK2, &HMD);
		debugOvr = true;
        LOG("Debug Oculus Rift device created."); 
    }

    if (HMD->ProductName[0] == '\0')
    {
        LOG("Rift detected, display not enabled.");
    }

    windowedOvr = false; //(HMD->HmdCaps & ovrHmdCap_ExtendDesktop) ? false : true;    

    detected = true;
    return S_OK;
}


//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND	- process the application menu
//  WM_PAINT	- Paint the main window
//  WM_DESTROY	- post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int wmId, wmEvent;
    PAINTSTRUCT ps;
    HDC hdc;

    switch (message)
    {
    case WM_COMMAND:
        wmId    = LOWORD(wParam);
        wmEvent = HIWORD(wParam);
        // Parse the menu selections:
        switch (wmId)
        {
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;
    case WM_PAINT:
        hdc = BeginPaint(hWnd, &ps);
        // TODO: Add any drawing code here...
        EndPaint(hWnd, &ps);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}


// Registers the window class.
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSW wc; memset(&wc, 0, sizeof(wc));
    wc.lpszClassName = L"OVRAppWindow";
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.cbWndExtra = NULL;
    return RegisterClassW(&wc);

}
