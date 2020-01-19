//
// Main.cpp
//
 
#include "pch.h"
#include "Game.h"
#include "InputManager.h"

#include <appnotify.h>

using namespace DirectX;

namespace
{
    std::unique_ptr<Game> g_game;
    HANDLE g_plmSuspendComplete = nullptr;
    HANDLE g_plmSignalResume = nullptr;
};

// Tree engine
//Game* g_game = nullptr;
InputManager g_inputManager;

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

void UpdateFrameAndRender()
{
    // Run game 
    g_game->ComputeCPU();
    g_game->ComputeGPU();

    g_game->Render(false);
}

// Entry point
int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(lpCmdLine);

    if (!XMVerifyCPUSupport())
        return 1;

    if (FAILED(XGameRuntimeInitialize()))
        return 1;

    // Microsoft Game Core on Xbox supports UTF-8 everywhere
    assert(GetACP() == CP_UTF8);

    // Register class and create window
    PAPPSTATE_REGISTRATION hPLM = {};
    {
        // Register class
        WNDCLASSEXA wcex = {};
        wcex.cbSize = sizeof(WNDCLASSEXA);
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = WndProc;
        wcex.hInstance = hInstance;
        wcex.lpszClassName = u8"TreeXboxWindowClass";
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        if (!RegisterClassExA(&wcex))
            return 1;

        const int windowWidth = 1920;
        const int windowHeight = 1080;

        // Create window
        HWND hwnd = CreateWindowExA(0, u8"TreeXboxWindowClass", u8"TreeXbox", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight, nullptr, nullptr, hInstance,
            nullptr);
        if (!hwnd)
            return 1;

        g_game = std::make_unique<Game>(&g_inputManager);
        //g_game = new Game(&g_inputManager);
        g_game->SetWindow(hwnd);

        g_game->OnResize(windowWidth, windowHeight);

        if (FAILED(g_game->Initialize(false /* render to shared texture */)))
        {
            g_game->Cleanup();
            return 0;
        }

        ShowWindow(hwnd, nCmdShow);

        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(g_game.get()));

        g_plmSuspendComplete = CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
        g_plmSignalResume = CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
        if (!g_plmSuspendComplete || !g_plmSignalResume)
            return 1;

        if (RegisterAppStateChangeNotification([](BOOLEAN quiesced, PVOID context)
        {
            if (quiesced)
            {
                ResetEvent(g_plmSuspendComplete);
                ResetEvent(g_plmSignalResume);

                // To ensure we use the main UI thread to process the notification, we self-post a message
                PostMessage(reinterpret_cast<HWND>(context), WM_USER, 0, 0);

                // To defer suspend, you must wait to exit this callback
                (void)WaitForSingleObject(g_plmSuspendComplete, INFINITE);
            }
            else
            {
                SetEvent(g_plmSignalResume);
            }
        }, hwnd, &hPLM))
            return 1;
    }

    // Main message loop
    MSG msg = {};
    while (WM_QUIT != msg.message)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            UpdateFrameAndRender();
        }
    }

    g_game->Cleanup();

    g_game.reset();
    //delete g_game;

    UnregisterAppStateChangeNotification(hPLM);

    CloseHandle(g_plmSuspendComplete);
    CloseHandle(g_plmSignalResume);

    XGameRuntimeUninitialize();

    return (int) msg.wParam;
}

// Windows procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto game = reinterpret_cast<Game*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    switch (message)
    {
    case WM_CREATE:
        break;

    case WM_ACTIVATEAPP:
        break;

    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_KEYDOWN:
    case WM_KEYUP:
    {
        FrameInputData& input = g_inputManager.GetFrameInput(0);

        bool WasDown = ((lParam & (1 << 30)) != 0);
        bool IsDown = ((lParam & (1 << 31)) == 0);

        if (WasDown != IsDown)
        {
            if (IsDown)
                input.key[wParam] = true;
            else if (WasDown)
                input.key[wParam] = false;
        }

        break;
    }

    case WM_USER:
        if (game)
        {
            //game->OnSuspending();

            // Complete deferral
            SetEvent(g_plmSuspendComplete);

            (void)WaitForSingleObject(g_plmSignalResume, INFINITE);

            //game->OnResuming();
        }
        break;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

// Exit helper
void ExitGame()
{
    PostQuitMessage(0);
}
