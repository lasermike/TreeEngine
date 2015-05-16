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

// Oculus specific
ovrHmd HMD = nullptr;
bool debugOvr = false;
bool windowedOvr = false;
OVR::Sizei WinSize;

// Forward declarations of functions included in this code module:
HRESULT				DetectOculus(bool& detected);

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
    MSG msg;
    HACCEL hAccelTable;

    // Initialize global strings
    LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadString(hInstance, IDC_TREECLASSIC, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_TREECLASSIC));

    // Main message loop:
    while (GetMessage(&msg, NULL, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

		g_game->Render();
    }

    return (int) msg.wParam;
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance; // Store instance handle in our global variable
    HWND hWnd;

    bool oculusDetected = false;
    HR(DetectOculus(oculusDetected));

	// Found a regular or debug Oculus device
	if (HMD)
	{
		Recti vp(HMD->WindowsPos, HMD->Resolution);
		// Create Window
		DWORD wsStyle = WS_POPUP;
		DWORD sizeDivisor = 1;

		if (windowedOvr)
		{
			wsStyle |= WS_OVERLAPPEDWINDOW; 
			sizeDivisor = 2;
		}
		RECT winSize = { 0, 0, vp.w / sizeDivisor, vp.h / sizeDivisor };
		AdjustWindowRect(&winSize, wsStyle, false);
		hWnd = CreateWindowW(L"OVRAppWindow", L"Tree Engine", wsStyle | WS_VISIBLE,
			vp.x, vp.y, winSize.right - winSize.left, winSize.bottom - winSize.top,
			NULL, NULL, hInst, NULL);

		if (!hWnd)
			return(false);
		if (windowedOvr)
		{
			WinSize = vp.GetSize();
		}
		else
		{
			RECT rc; GetClientRect(hWnd, &rc);
			WinSize = Sizei(rc.right - rc.left, rc.bottom - rc.top);
		}
	}
	else
	{
		hWnd = CreateWindow(L"OVRAppWindow", szTitle, WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);
	}

    if (!hWnd)
    {
        return FALSE;
    }

	g_game = new Game();
	g_game->Initialize(hWnd);


    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return TRUE;
}

void OnWindowSizeChanged()
{
	g_game->OnResize();
}


//--------------------------------------------------------------------------------------
// Create Oculus interface if possible
//--------------------------------------------------------------------------------------
HRESULT DetectOculus(bool& detected)
{
    detected = false;

    OVR::System::Init(OVR::Log::ConfigureDefaultLog(OVR::LogMask_All));

    //Initialise rift
    if (!ovr_Initialize())
    { 
        LOG("Unable to initialize libOVR."); 
        return 0; 
    }
    HMD = ovrHmd_Create(0);
	if (HMD)
	{
		LOG("Oculus Rift device created."); 
	}
	else 
    {
        HMD = ovrHmd_CreateDebug(ovrHmd_DK2);
		debugOvr = true;
        LOG("Debug Oculus Rift device created."); 
    }

    if (!HMD) 
    {	
        LOG("Oculus Rift not detected."); 
        ovr_Shutdown(); 
        return S_FALSE; 
    }

    if (HMD->ProductName[0] == '\0')
    {
        LOG("Rift detected, display not enabled.");
    }

    windowedOvr = (HMD->HmdCaps & ovrHmdCap_ExtendDesktop) ? false : true;    

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
    wc.lpfnWndProc = DefWindowProc;
    wc.cbWndExtra = NULL;
    return RegisterClassW(&wc);

}
