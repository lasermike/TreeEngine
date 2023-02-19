//
// Main.cpp
//
 
#include "pch.h"
#include "Game.h"
#include "InputManager.h"

#ifdef TREE_XBOX
#include <GameInput.h>
#endif

#include <appnotify.h>

using namespace DirectX;

namespace
{
    std::unique_ptr<Game> g_game;
    HANDLE g_plmSuspendComplete = nullptr;
    HANDLE g_plmSignalResume = nullptr;
};

// Tree engine
InputManager g_inputManager;
CComPtr<IGameInput>                 g_gameInput;
std::vector<APP_LOCAL_DEVICE_ID>    g_deviceIds;
wchar_t                             g_deviceString[20];
std::wstring                        g_buttonString;

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void GatherGamepadInput();

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
        g_game->GetRenderManager().GetRenderData().inputManager = &g_inputManager;
        g_game->SetWindow(hwnd);

        g_game->OnResize(windowWidth, windowHeight);

        if (FAILED(g_game->Initialize(false /* render to shared texture */)))
        {
            g_game->Cleanup();
            return 0;
        }

        ShowWindow(hwnd, nCmdShow);

        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(g_game.get()));

        if (FAILED(GameInputCreate(&g_gameInput)))
        {
            return 1;
        }


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
            GatherGamepadInput();

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

bool IsSameDevice(APP_LOCAL_DEVICE_ID first, APP_LOCAL_DEVICE_ID second)
{
    if (memcmp(&first, &second, APP_LOCAL_DEVICE_ID_SIZE) == 0)
    {
        return true;
    }

    return false;
}

void GatherGamepadInput()
{
    CComPtr<IGameInputReading>          g_reading;

    FrameInputData& inputData = g_inputManager.GetFrameInput(0);

    if (FAILED(g_gameInput->GetCurrentReading(GameInputKindGamepad, nullptr, &g_reading)))
    {
        // Failure indicates no gamepad is connected
        g_buttonString.clear();
    }
    else
    {
        CComPtr<IGameInputDevice> device;
        g_reading->GetDevice(&device);

        if (device != nullptr)
        {
            int currentDevice = -1;
            auto deviceInfo = device->GetDeviceInfo();

            for (size_t i = 0; i < g_deviceIds.size(); i++)
            {
                if (IsSameDevice(g_deviceIds[size_t(i)], deviceInfo->deviceId))
                {
                    currentDevice = int(i);
                    break;
                }
            }

            if (currentDevice == -1)
            {
                currentDevice = (int)g_deviceIds.size() - 1;
                g_deviceIds.emplace_back(deviceInfo->deviceId);
            }

            swprintf(g_deviceString, 19, L"Gamepad index: %d", currentDevice);
        }
        else
        {
            swprintf(g_deviceString, 19, L"No Device");
        }

        GameInputGamepadState state;

        if (g_reading->GetGamepadState(&state))
        {
            g_buttonString = L"Buttons pressed:  ";

            int exitComboPressed = 0;

            inputData.key['W'] = (state.buttons & GameInputGamepadDPadUp) || (state.leftThumbstickY > .3f);

            inputData.key['S'] = (state.buttons & GameInputGamepadDPadDown) || (state.leftThumbstickY < -.3f);

            inputData.key['D'] = state.buttons & GameInputGamepadDPadRight;

            inputData.key['A'] = state.buttons & GameInputGamepadDPadLeft;

            inputData.key[VK_LEFT] = state.leftThumbstickX < -.3f;

            inputData.key[VK_RIGHT] = state.leftThumbstickX > .3f;

            inputData.key['Y'] = state.buttons & GameInputGamepadY;

        }
    }
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
