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
//------------------------------------------------------------
// ovrSwapTextureSet wrapper class that also maintains the render target views
// needed for D3D11 rendering.
struct OculusTexture
{
    ovrSwapTextureSet      * TextureSet;
    ID3D11RenderTargetView * TexRtv[3];

    OculusTexture(ovrHmd hmd, ID3D11Device* pDevice, Sizei size)
    {
        D3D11_TEXTURE2D_DESC dsDesc;
        dsDesc.Width            = size.w;
        dsDesc.Height           = size.h;
        dsDesc.MipLevels        = 1;
        dsDesc.ArraySize        = 1;
        dsDesc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
        dsDesc.SampleDesc.Count = 1;   // No multi-sampling allowed
        dsDesc.SampleDesc.Quality = 0;
        dsDesc.Usage            = D3D11_USAGE_DEFAULT;
        dsDesc.CPUAccessFlags   = 0;
        dsDesc.MiscFlags        = 0;
        dsDesc.BindFlags        = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

        ovrHmd_CreateSwapTextureSetD3D11(hmd, pDevice, &dsDesc, &TextureSet);
        for (int i = 0; i < TextureSet->TextureCount; ++i)
        {
            ovrD3D11Texture* tex = (ovrD3D11Texture*)&TextureSet->Textures[i];
            pDevice->CreateRenderTargetView(tex->D3D11.pTexture, NULL, &TexRtv[i]);
        }
    }

    void AdvanceToNextTexture()
    {
        TextureSet->CurrentIndex = (TextureSet->CurrentIndex + 1) % TextureSet->TextureCount;
    }
    void Release(ovrHmd hmd)
    {
        ovrHmd_DestroySwapTextureSet(hmd, TextureSet);
    }
};

// Tree classic
HINSTANCE hInst;								// current instance
TCHAR szTitle[MAX_LOADSTRING];					// The title bar text
TCHAR szWindowClass[MAX_LOADSTRING];			// the main window class name
HWND hWnd = nullptr;
bool oculusMode = false;

// Tree engine
Game* g_game = nullptr;
bool key[256] = {};

// Oculus specific
ovrHmd HMD = nullptr;
bool debugOvr = false;
bool windowedOvr = false;
OVR::Sizei WinSize;
ovrEyeRenderDesc eyeRenderDesc[2];
OculusTexture  *g_pEyeRenderTexture[2];
DepthBuffer    *g_pEyeDepthBuffer[2];
ovrRecti         g_eyeRenderViewport[2];
ovrTexture*     g_mirrorTexture = nullptr;
ovrQuatf neutralRotation;
Vector3f neutralPosition;

//------------------------------------------------------------

// Forward declarations of functions included in this code module:
HRESULT				CreateOculusDevice(bool& detected);
HRESULT				ConfigOculusDevice();
HRESULT             Render();

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
            Render();
        }
    }

    g_game->Cleanup();

    return (int) msg.wParam;
}


HRESULT Render()
{
    if (oculusMode)
    {
        XMFLOAT4 eye = g_game->GetRenderData().eyePos;

        //Camera mainCam(Vector3f(eye.x, eye.y, eye..z), Matrix4f::RotationY(3.141f));
        float y = ovrHmd_GetFloat(HMD, OVR_KEY_EYE_HEIGHT, 0);
        //Util.Output("LOG: position Y: %f \n", y);  

        // Get both eye poses simultaneously, with IPD offset already included. 
        ovrPosef         EyeRenderPose[2];
        ovrVector3f      HmdToEyeViewOffset[2] = { eyeRenderDesc[0].HmdToEyeViewOffset,
                                                   eyeRenderDesc[1].HmdToEyeViewOffset };
        ovrFrameTiming   ftiming  = ovrHmd_GetFrameTiming(HMD, 0);
        ovrTrackingState hmdState = ovrHmd_GetTrackingState(HMD, ftiming.DisplayMidpointSeconds);
        ovr_CalcEyePoses(hmdState.HeadPose.ThePose, HmdToEyeViewOffset, EyeRenderPose);

        //Util.Output("LOG: position Y: %f, %f, %f \n", hmdState.HeadPose.ThePose.Position.x, 
        //                                      hmdState.HeadPose.ThePose.Position.y,
        //                                      hmdState.HeadPose.ThePose.Position.z);  


        // Run game 
        g_game->Tick(key);

        // Render Scene to Eye Buffers
        for (int eye = 0; eye < 2; eye++)  //2
        {
            if (key[VK_SPACE])
            {
                // Reset to default camera position
				neutralPosition = EyeRenderPose[eye].Position;
                neutralRotation = EyeRenderPose[eye].Orientation;
            }

			// COmpute position
            Vector3f adjustedPos = Vector3f(EyeRenderPose[eye].Position) - neutralPosition;
			XMFLOAT3 hmdPos = XMFLOAT3(adjustedPos.x, adjustedPos.y, adjustedPos.z);			 

            // Compute rotation.  Divide sensor data by neutral data 
			XMVECTOR eyeQuat = XMVectorSet(-EyeRenderPose[eye].Orientation.x, -EyeRenderPose[eye].Orientation.y,
											EyeRenderPose[eye].Orientation.z, EyeRenderPose[eye].Orientation.w);
			XMVECTOR neutralQuat = XMVectorSet(-neutralRotation.x, -neutralRotation.y,
												neutralRotation.z, neutralRotation.w);
			XMVECTOR finalQuat = XMQuaternionMultiply(eyeQuat, XMQuaternionInverse(neutralQuat));
			XMFLOAT4 hmdRot;
			XMStoreFloat4(&hmdRot, finalQuat);

            g_game->GetPlayer()->GetCamera()->SetHmdState(hmdPos, hmdRot);

            // Render
            {
                // Increment to use next texture, just before writing
                g_pEyeRenderTexture[eye]->AdvanceToNextTexture();

                // Clear and set up rendertarget
                int texIndex = g_pEyeRenderTexture[eye]->TextureSet->CurrentIndex;

                DIRECTX.SetAndClearRenderTarget(g_pEyeRenderTexture[eye]->TexRtv[texIndex], g_pEyeDepthBuffer[eye]);

                DIRECTX.SetViewport(Recti(g_eyeRenderViewport[eye]));

                g_game->Render(true);

                // Initialize our single full screen Fov layer.
                ovrLayerEyeFov ld;
                ld.Header.Type  = ovrLayerType_EyeFov;
                ld.Header.Flags = 0;

                for (int eye = 0; eye < 2; eye++)
                {
                    ld.ColorTexture[eye] = g_pEyeRenderTexture[eye]->TextureSet;
                    ld.Viewport[eye]     = g_eyeRenderViewport[eye];
                    ld.Fov[eye]          = HMD->DefaultEyeFov[eye];
                    ld.RenderPose[eye]   = EyeRenderPose[eye];
                }

                ovrLayerHeader* layers = &ld.Header;
                ovrResult result = ovrHmd_SubmitFrame(HMD, 0, nullptr, &layers, 1);
                //isVisible = result == ovrSuccess;

                // Render mirror
	            ID3D11Texture2D* pBackBuffer = nullptr;
                HRR(g_game->GetSwapChain()->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer)));

                ovrD3D11Texture* tex = (ovrD3D11Texture*)g_mirrorTexture;
                DIRECTX.Context->CopyResource(pBackBuffer, tex->D3D11.pTexture);
                pBackBuffer->Release();

                DIRECTX.SwapChain->Present(0, 0);

            }
        }
    }
    else
    {
        // Run game 
        g_game->Tick(key);

        g_game->Render(false);
    }

    return S_OK;
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

	oculusMode = HMD != nullptr && !debugOvr;

	// Found a regular or debug Oculus device
	if (oculusMode)
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

        neutralRotation = Quatf::Identity();
	}
	else
	{
		RECT rc = { 0, 0, 1600, 1080};
		AdjustWindowRect( &rc, WS_OVERLAPPEDWINDOW, FALSE );
		hWnd = CreateWindow(L"OVRAppWindow", szTitle, WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, hInstance, NULL);
	}

    if (!hWnd)
    {
        return FALSE;
    }

	g_game = new Game();

	if (FAILED(g_game->Initialize(hWnd)))
    {
		g_game->Cleanup();
        return 0;
    }

    // Set up the oculus helper library
    DIRECTX.Context = g_game->GetContext();
    DIRECTX.BackBuffer = g_game->GetBackBuffer();
    DIRECTX.SwapChain = g_game->GetSwapChain();

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

	// Oculus mode
	if (oculusMode)
	{
		ConfigOculusDevice();
	}

    return TRUE;
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


HRESULT ConfigOculusDevice()
{
	ovrHmd_SetEnabledCaps(HMD, ovrHmdCap_LowPersistence | ovrHmdCap_DynamicPrediction);

    // Start the sensor which informs of the Rift's pose and motion
    ovrResult result = ovrHmd_ConfigureTracking(HMD, ovrTrackingCap_Orientation | ovrTrackingCap_MagYawCorrection |
                                                     ovrTrackingCap_Position, 0);
    ASSERTSZ(result == ovrSuccess, "Failed to configure tracking.");

    // Make the eye render buffers (caution if actual size < requested due to HW limits). 
    
    for (int eye = 0; eye < 2; eye++)
    {
        Sizei idealSize = ovrHmd_GetFovTextureSize(HMD, (ovrEyeType)eye, HMD->DefaultEyeFov[eye], 1.0f);
        g_pEyeRenderTexture[eye]      = new OculusTexture(HMD, g_game->GetDevice(), idealSize);
		g_pEyeDepthBuffer[eye]        = new DepthBuffer(g_game->GetDevice(), idealSize);
        g_eyeRenderViewport[eye].Pos  = Vector2i(0, 0);
        g_eyeRenderViewport[eye].Size = idealSize;
    }

    // Create mirror buffer same type as back buffer
	ID3D11Texture2D* pBackBuffer = nullptr;
    HRR(g_game->GetSwapChain()->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer)));
    D3D11_TEXTURE2D_DESC bbDesc = {};
    pBackBuffer->GetDesc(&bbDesc);
    pBackBuffer->Release();

    // Create a mirror to see on the monitor.
    D3D11_TEXTURE2D_DESC td = { };
    td.ArraySize        = 1;
    td.Format           = bbDesc.Format; //DXGI_FORMAT_B8G8R8A8_UNORM;
    td.Width            = WinSize.w;
    td.Height           = WinSize.h;
    td.Usage            = D3D11_USAGE_DEFAULT;
    td.SampleDesc.Count = 1;
    td.MipLevels        = 1;
	ovrHmd_CreateMirrorTextureD3D11(HMD, g_game->GetDevice(), &td, &g_mirrorTexture);

    //// Create the room model
    //Scene roomScene;

    //// Create camera
    //Camera mainCam(Vector3f(0.0f, 1.6f, -5.0f), Matrix4f::RotationY(3.141f));

    // Setup VR components, filling out description
    eyeRenderDesc[0] = ovrHmd_GetRenderDesc(HMD, ovrEye_Left, HMD->DefaultEyeFov[0]);
    eyeRenderDesc[1] = ovrHmd_GetRenderDesc(HMD, ovrEye_Right, HMD->DefaultEyeFov[1]);

	return S_OK;
}

void OnWindowSizeChanged()
{
    ASSERT(hWnd);

	RECT rect = {0};
	UINT windowWidth = 0; 
	UINT windowHeight = 0;
	GetClientRect(hWnd, &rect);
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
    PAINTSTRUCT ps;
    HDC hdc;

    switch (message)
    {
    case WM_KEYDOWN:
        key[wParam] = true;
        break;
    case WM_KEYUP:
        key[wParam] = false;
        break;
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
