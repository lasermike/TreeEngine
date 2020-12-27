// TreeClassic.cpp : Defines the entry point for the application.
//

#include "pch.h"
//#include "TreeXbox.h"
#include "Game.h"
#include "InputManager.h"

#define MAX_LOADSTRING 100

// Tree classic
HINSTANCE hInst;                                // current instance
TCHAR szTitle[MAX_LOADSTRING];                    // The title bar text
//TCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
HWND m_hWnd = nullptr;

// Tree engine
Game* g_game = nullptr;
InputManager g_inputManager;

HRESULT             Render();
void                OnWindowSizeChanged();

ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);


int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    // Initialize global strings
    wcscpy(szTitle, L"TREEXBOX");
    //lstrcpy(szWindowClass, L"TreeXbox");
    //LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    //LoadString(hInstance, IDC_TREECLASSIC, szWindowClass, MAX_LOADSTRING);
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
            Render();
        }
    }

    g_game->Cleanup();
    
    delete g_game;

    return (int) msg.wParam;
}

HRESULT Render()
{
        // Run game 
        g_game->ComputeCPU();
        g_game->ComputeGPU();

        g_game->Render(false);

    return S_OK;
}


//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance; // Store instance handle in our global variable

        RECT rc = { 0, 0, 1600, 1080};
        AdjustWindowRect( &rc, WS_OVERLAPPEDWINDOW, FALSE );
        m_hWnd = CreateWindow(L"OVRAppWindow", szTitle, WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, hInstance, NULL);

    if (!m_hWnd)
    {
        return FALSE;
    }

    g_game = new Game(&g_inputManager);
    g_game->GetRenderManager().GetRenderData().inputManager = &g_inputManager;
    g_game->SetWindow(m_hWnd);
    OnWindowSizeChanged();
    //g_game->GetRenderManager().GetPlatform()->SetWindow(m_hWnd);

    if (FAILED(g_game->Initialize(false /* render to shared texture */)))
    {
        g_game->Cleanup();
        return 0;
    }

    //OLD - OnWindowSizeChanged();

    ShowWindow(m_hWnd, nCmdShow);
    
    // Needed? UpdateWindow(m_hWnd);

    return TRUE;
}

void OnWindowSizeChanged()
{
    ASSERT(m_hWnd);

    RECT rect = {0};
    UINT windowWidth = 0; 
    UINT windowHeight = 0;
    GetClientRect(m_hWnd, &rect);
    windowWidth = rect.right - rect.left;
    windowHeight = rect.bottom - rect.top;

    g_game->OnResize(windowWidth, windowHeight);
}


//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int wmId, wmEvent;
    //PAINTSTRUCT ps;
    HDC hdc;

    FrameInputData& input = g_inputManager.GetFrameInput(0);

    switch (message)
    {
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_KEYDOWN:
    case WM_KEYUP:
    {
        //UINT VKCode = wParam;
        bool WasDown = ((lParam & (1 << 30)) != 0);
        bool IsDown = ((lParam & (1 << 31)) == 0);
        
        if (WasDown != IsDown)
        {
            if (IsDown)
                input.key[wParam] = true;
            else if (WasDown)
                input.key[wParam] = false;
        }
    }
    //case WM_KEYDOWN:
 //       input.key[wParam] = !(lParam & 1 << 30);
 //       break;
 //   case WM_KEYUP:
 //       input.key[wParam] = false;
 //       break;
    case WM_COMMAND:
        wmId    = LOWORD(wParam);
        wmEvent = HIWORD(wParam);
        // Parse the menu selections:
        switch (wmId)
        {
        //case IDM_ABOUT:
        //    DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;
    //case WM_PAINT:
    //    hdc = BeginPaint(hWnd, &ps);
    //    // TODO: Add any drawing code here...
    //    EndPaint(hWnd, &ps);
    //    break;
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

    //case WM_COMMAND:
    //    if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
    //    {
    //        EndDialog(hDlg, LOWORD(wParam));
    //        return (INT_PTR)TRUE;
    //    }
    //    break;
    }
    return (INT_PTR)FALSE;
}


// Registers the window class.
ATOM MyRegisterClass(HINSTANCE /*hInstance*/)
{
    WNDCLASSW wc; memset(&wc, 0, sizeof(wc));
    wc.lpszClassName = L"OVRAppWindow";
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.cbWndExtra = NULL;
    return RegisterClassW(&wc);

}
