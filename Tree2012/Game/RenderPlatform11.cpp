#include "pch.h"

#include "RenderPlatform11.h"

#include "DDSTextureLoader.h"
#include "BitmapFont.h"

#include "StockRenderStates11.h"
#include "ShadowMap.h"

#include "DirectXTex.h"

#pragma region InputLayouts
#define InputElementDesc D3D11_INPUT_ELEMENT_DESC
#define InputClassificationVertex D3D11_INPUT_PER_VERTEX_DATA
#define InputClassificationInstance D3D11_INPUT_PER_INSTANCE_DATA
#define AppendAlignedElement D3D11_APPEND_ALIGNED_ELEMENT
#define ID3DInputLayout ID3D11InputLayout        

// TODO: undupe with 12
class InputLayoutDesc
{
public:
    static const InputElementDesc InstancedBasic16[14];
    static const InputElementDesc Basic32[3];
};

const InputElementDesc InputLayoutDesc::InstancedBasic16[14] =
{
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, InputClassificationVertex, 0 },
    { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, InputClassificationVertex, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, InputClassificationVertex, 0 },
    { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, InputClassificationVertex, 0 },
    { "BLENDWEIGHT", 0, DXGI_FORMAT_R32_FLOAT, 0, AppendAlignedElement, InputClassificationVertex, 0 },
    { "BLENDWEIGHT", 1, DXGI_FORMAT_R32_FLOAT, 0, AppendAlignedElement, InputClassificationVertex, 0 },
    { "BLENDWEIGHT", 2, DXGI_FORMAT_R32_FLOAT, 0, AppendAlignedElement, InputClassificationVertex, 0 },
    { "WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "BLENDINDICES", 0, DXGI_FORMAT_R32_UINT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "BLENDINDICES", 1, DXGI_FORMAT_R32_UINT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "BLENDINDICES", 2, DXGI_FORMAT_R32_UINT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
};

const InputElementDesc InputLayoutDesc::Basic32[3] =
{
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, InputClassificationVertex, 0 },
    { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, InputClassificationVertex, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, InputClassificationVertex, 0 }
};


class InputLayouts
{
public:
    static void InitAll(ID3D11Device* device, const void* pShaderBytecodeWithInputSignature, SIZE_T byteCodeLen);
    static void DestroyAll();

    static ID3DInputLayout* InstancedBasic16;
    static ID3DInputLayout* Basic32;
};

ID3DInputLayout* InputLayouts::InstancedBasic16 = 0;
ID3DInputLayout* InputLayouts::Basic32 = 0;

void InputLayouts::InitAll(ID3D11Device* device, const void* pShaderBytecodeWithInputSignature, SIZE_T byteCodeLen)
{
    HR(device->CreateInputLayout(InputLayoutDesc::InstancedBasic16,
        ARRAYSIZE(InputLayoutDesc::InstancedBasic16),
        pShaderBytecodeWithInputSignature /*passDesc.pIAInputSignature*/,
        byteCodeLen /*passDesc.IAInputSignatureSize*/, &InstancedBasic16));
    SetDebugName(InstancedBasic16, "RenderManager InstancedBasic16");
}

void InputLayouts::DestroyAll()
{
    SafeRelease(&InstancedBasic16);
    SafeRelease(&Basic32);
}


//--------------------------------------------------------------------------------------
// Name: LoadPixelShader()
// Desc: Load a pixel shader
//--------------------------------------------------------------------------------------
HRESULT LoadPixelShader(D3DDevice* pDev, const wchar_t* path, ID3D11PixelShader** ppPS, std::vector< BYTE >* pData = nullptr)
{
    std::vector< BYTE > data;
    if (!pData)
        pData = &data;

    HRESULT hr = XSF::LoadBlob(path, *pData);
    if (FAILED(hr))
        return hr;

    return pDev->CreatePixelShader(&(*pData)[0], pData->size(), nullptr, ppPS);
}

HRESULT RenderPlatform11::GetViewport(Viewport& viewport)
{
    viewport.TopLeftX = m_viewPort.TopLeftX;
    viewport.TopLeftY = m_viewPort.TopLeftY;
    viewport.Width = m_viewPort.Width;
    viewport.Height = m_viewPort.Height;
    viewport.MinDepth = m_viewPort.MinDepth;
    viewport.MaxDepth = m_viewPort.MaxDepth;

    return S_OK;
}

HRESULT RenderPlatform11::UpdateView(CBNeverChanges& cbNeverChanges, bool shadowPass)
{
    //ID3D11Buffer* buffer = m_constBufferNeverChanges->Resource();

    //m_immediateContext->UpdateSubresource(buffer, 0, nullptr, &cbNeverChanges, 0, 0);
    //m_immediateContext->VSSetConstantBuffers(0, 1, &buffer);

    return S_OK;
}

HRESULT RenderPlatform11::UpdateViewProjection(XMFLOAT4X4* pViewMat, XMFLOAT4X4* pProjMat, bool shadowPass)
{
    CBChangesPerPass cbChangesPerPass;
    XMStoreFloat4x4(&cbChangesPerPass.mProjection, XMMatrixTranspose(XMLoadFloat4x4(pProjMat)));

    XMStoreFloat4x4(&cbChangesPerPass.mView, XMMatrixTranspose(XMLoadFloat4x4(pViewMat)));

    m_immediateContext->UpdateSubresource(m_constBufferChangesPerPass->Resource(), 0, nullptr, &cbChangesPerPass, 0, 0);


    //ID3D11Buffer* buffer = m_constBufferNeverChanges->Resource();
    //CBNeverChanges cbNeverChanges;
    //cbNeverChanges.mView = *pViewMat;
    //m_immediateContext->UpdateSubresource(buffer, 0, nullptr, &cbNeverChanges, 0, 0);
    //m_immediateContext->VSSetConstantBuffers(0, 1, &buffer);

    return S_OK;
}

HRESULT RenderPlatform11::InitDevice()
{
    HRESULT result = S_OK;
    m_driverType = D3D_DRIVER_TYPE_NULL;

    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

#if defined(_XBOX_ONE) && defined(PROFILE) 
    createDeviceFlags |= D3D11_CREATE_DEVICE_INSTRUMENTED;
#endif

    D3D_DRIVER_TYPE driverTypes[] =
    {
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
        D3D_DRIVER_TYPE_REFERENCE,
    };
    UINT numDriverTypes = ARRAYSIZE(driverTypes);

    D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    UINT numFeatureLevels = ARRAYSIZE(featureLevels);

    for (UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++)
    {
        CComPtr<ID3D11Device> device;
        CComPtr<ID3D11DeviceContext> d3dContext;

        m_driverType = driverTypes[driverTypeIndex];
        result = D3D11CreateDevice(nullptr, m_driverType, nullptr, createDeviceFlags, featureLevels, numFeatureLevels,
            D3D11_SDK_VERSION, &device, &m_featureLevel, &d3dContext);

        if (SUCCEEDED(result))
        {
            HRR(device->QueryInterface(__uuidof(m_d3dDevice), reinterpret_cast<void**>(&m_d3dDevice)));
            HRR(d3dContext->QueryInterface(__uuidof(m_immediateContext), reinterpret_cast<void**>(&m_immediateContext)));
            break;
        }
    }

    if (FAILED(result))
        return result;

#if defined(_DEBUG) && !defined(_XBOX_ONE)
    if ((createDeviceFlags & D3D11_CREATE_DEVICE_DEBUG) == D3D11_CREATE_DEVICE_DEBUG)
    {
        // Debug layers
        CComPtr<ID3D11Debug> d3dDebug;
        HR(m_d3dDevice->QueryInterface(__uuidof(ID3D11Debug), (void**)&d3dDebug));

        CComPtr<ID3D11InfoQueue> d3dInfoQueue;
        HR(d3dDebug->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&d3dInfoQueue))
            d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
        d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);

        D3D11_MESSAGE_ID hide[] =
        {
            D3D11_MESSAGE_ID_SETPRIVATEDATA_CHANGINGPARAMS,
            // Add more message IDs here as needed
        };

        D3D11_INFO_QUEUE_FILTER filter;
        ZeroMemory(&filter, sizeof(filter));
        filter.DenyList.NumIDs = _countof(hide);
        filter.DenyList.pIDList = hide;
        d3dInfoQueue->AddStorageFilterEntries(&filter);
    }
#endif

    // Create constant buffer
    // TODO: Allocate second CB for shadow pass instead of uploading CB during render
    m_constBufferChangesPerPass = new UploadBuffer<CBChangesPerPass>(GetDevice(), 1, true);

    ID3D11Buffer* buffer = m_constBufferChangesPerPass->Resource();
    m_immediateContext->VSSetConstantBuffers(1, 1, &buffer);

    // REnder states
    XSF::StockRenderStates::Initialize(GetD3DDevice());

    return S_OK;
}

HRESULT RenderPlatform11::UninitDevice()
{
    XSF::StockRenderStates::Shutdown();

    ReleaseSwapChainResources();

    SafeDelete(&m_constBufferChangesPerPass);

    m_rasterState.Release();

    m_immediateContext.Release();

#if defined(_DEBUG) && !defined(_XBOX_ONE)
    if (GetDevice())
    {
        CComPtr<ID3D11Debug> dbg;
        HR(GetDevice()->QueryInterface(__uuidof(ID3D11Debug), reinterpret_cast<void**>(&dbg)));

        HR(dbg->ReportLiveDeviceObjects(D3D11_RLDO_SUMMARY | D3D11_RLDO_DETAIL));
    }
#endif

    m_d3dDevice.Release();

    return S_OK;
}

HRESULT RenderPlatform11::InitGameLevelGraphics(UINT maxInstances, bool useShadowMaps, GeometryBufferData& geometryData)
{
    // Init text font
    m_bitmapFont = new XSF::BitmapFont();
    XSF_ERROR_IF_FAILED(m_bitmapFont->Create(GetD3DDevice(), L"Arial_16"));

    // Create the constant buffers
    m_constBufferNeverChanges = new UploadBuffer<CBNeverChanges>(GetDevice(), 1, true);
    SetDebugName(m_constBufferNeverChanges->Resource(), "RenderManager::m_constBufferNeverChanges");

    // Create constants for per frame 
    m_constBufferChangesEveryFrame = new UploadBuffer<CBChangesEveryFrame>(GetDevice(), 1, true);
    SetDebugName(m_constBufferChangesEveryFrame->Resource(), "RenderManager::m_constBufferChangesEveryFrame");

    // Create input layout 1
    std::vector< BYTE > dataVS;
    HRR(XSF::LoadBlob(L"VS.cso", dataVS));

    InputLayouts::InitAll(GetDevice(), &(dataVS)[0], dataVS.size());
    m_immediateContext->IASetInputLayout(InputLayouts::InstancedBasic16);

    // Create input layout 2
    dataVS.clear();
    HRR(XSF::LoadBlob(L"DrawScreenQuadVS.cso", dataVS));

    HRR(GetDevice()->CreateInputLayout(InputLayoutDesc::Basic32,
        ARRAYSIZE(InputLayoutDesc::Basic32),
        &(dataVS)[0] /*passDesc.pIAInputSignature*/,
        dataVS.size() /*passDesc.IAInputSignatureSize*/,
        &InputLayouts::Basic32));
    SetDebugName(InputLayouts::Basic32, "InputLayouts::Basic32");

    //
    // Shaders
    //
    // Create Instanced draw data layout
    m_vertexShader = nullptr;
    m_pixelShader = nullptr;
    HRR(LoadVertexShader(L"VS.cso", &m_vertexShader));
    HRR(LoadPixelShader(L"PS.cso", &m_pixelShader));

    ////////  Shadow map shader /////
    // Load shadow shaders
    m_shadowVertexShader = nullptr;
    m_shadowPixelShader = nullptr;

    HRR(LoadVertexShader(L"BuildShadowMapVS.cso", &m_shadowVertexShader));
    // TODO: load a shadow pixel shader to support transparent textures not casting shadows

    ////////  Debug window
    m_drawScreenVertexShader = nullptr;
    m_drawScreenPixelShader = nullptr;
    HRR(LoadVertexShader(L"DrawScreenQuadVS.cso", &m_drawScreenVertexShader));
    HRR(LoadPixelShader(L"DrawScreenQuadPS.cso", &m_drawScreenPixelShader));

    // Debug overlay to show depth map
    HRR(BuildScreenQuadGeometryBuffers());

    //////
    // Create primitive vertex buffer
    D3D11_BUFFER_DESC vbd;
    ZeroMemory(&vbd, sizeof(vbd));
    vbd.Usage = D3D11_USAGE_IMMUTABLE;
    vbd.ByteWidth = (UINT)(sizeof(SimpleVertex) * geometryData.vertices.size());
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbd.CPUAccessFlags = 0;
    vbd.MiscFlags = 0;
    D3D11_SUBRESOURCE_DATA vinitData;
    ZeroMemory(&vinitData, sizeof(vinitData));
    vinitData.pSysMem = &geometryData.vertices[0];
    HRR(GetDevice()->CreateBuffer(&vbd, &vinitData, &m_vertexBuffer.buffer));
    SetDebugName(m_vertexBuffer, "RenderManager::m_vertexBuffer");

    D3D11_BUFFER_DESC ibd;
    ZeroMemory(&ibd, sizeof(ibd));
    ibd.Usage = D3D11_USAGE_IMMUTABLE;
    ibd.ByteWidth = (UINT)(sizeof(UINT) * geometryData.indices.size());
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    ibd.CPUAccessFlags = 0;
    ibd.MiscFlags = 0;
    D3D11_SUBRESOURCE_DATA iinitData;
    ZeroMemory(&iinitData, sizeof(iinitData));
    iinitData.pSysMem = &geometryData.indices[0];
    HRR(GetDevice()->CreateBuffer(&ibd, &iinitData, &m_indexBuffer.buffer));
    SetDebugName(m_indexBuffer, "RenderManager::m_indexBuffer");

    //////
    // Create skinned primitive vertex buffer
    ZeroMemory(&vbd, sizeof(vbd));
    vbd.Usage = D3D11_USAGE_IMMUTABLE;
    vbd.ByteWidth = (UINT)(sizeof(SkinnedVertex) * geometryData.skinnedVertices.size());
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbd.CPUAccessFlags = 0;
    vbd.MiscFlags = 0;
    //D3D11_SUBRESOURCE_DATA vinitData;
    ZeroMemory(&vinitData, sizeof(vinitData));
    vinitData.pSysMem = &geometryData.skinnedVertices[0];
    HRR(GetDevice()->CreateBuffer(&vbd, &vinitData, &m_skinnedVertexBuffer.buffer));
    SetDebugName(m_skinnedVertexBuffer, "RenderManager::m_skinnedVertexBuffer");

    //D3D11_BUFFER_DESC ibd;
    ZeroMemory(&ibd, sizeof(ibd));
    ibd.Usage = D3D11_USAGE_IMMUTABLE;
    ibd.ByteWidth = (UINT)(sizeof(UINT) * geometryData.skinnedIndices.size());
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    ibd.CPUAccessFlags = 0;
    ibd.MiscFlags = 0;
    //D3D11_SUBRESOURCE_DATA iinitData;
    ZeroMemory(&iinitData, sizeof(iinitData));
    iinitData.pSysMem = &geometryData.skinnedIndices[0];
    HRR(GetDevice()->CreateBuffer(&ibd, &iinitData, &m_skinnedIndexBuffer.buffer));
    SetDebugName(m_skinnedIndexBuffer, "RenderManager::m_skinnedIndexBuffer");


    // Set index buffer
    m_immediateContext->IASetIndexBuffer(m_indexBuffer, DXGI_FORMAT_R32_UINT, 0);

    // Set primitive topology
    m_immediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    if (useShadowMaps)
    {
        m_renderData->pShadowMap = new ShadowMap(GetD3DDevice(), m_renderData->SMapWidth, m_renderData->SMapHeight);
    }

    return S_OK;
}

HRESULT RenderPlatform11::UninitGameLevelGraphics()
{
    SafeDelete(&m_constBufferChangesEveryFrame);
    SafeDelete(&m_constBufferNeverChanges);
    SafeDelete(&m_bitmapFont);

    m_vertexBuffer.Release();
    m_indexBuffer.Release();
    m_skinnedVertexBuffer.Release();
    m_skinnedIndexBuffer.Release();
    m_screenQuadVB.Release();
    m_screenQuadIB.Release();
    SafeRelease(&m_vertexLayout);

    SafeDelete(&m_renderData->pShadowMap);

    for (VertexShader* vs : m_gameLevelVertexShaders)
    {
        delete vs;
    }
    m_gameLevelVertexShaders.clear();

    for (PixelShader* ps : m_gameLevelPixelShaders)
    {
        delete ps;
    }
    m_gameLevelPixelShaders.clear();

    for (ID3D11Buffer* buffer : m_gameLevelBuffers)
    {
        buffer->Release();
    }
    m_gameLevelBuffers.clear();

    InputLayouts::DestroyAll();

    return S_OK;
}

HRESULT RenderPlatform11::ReleaseSwapChainResources()
{
    HRESULT hr = S_OK;

    if (!m_immediateContext)
    {
        return S_FALSE;
    }

    // Resize logic

    // Create width/height dependent objects
    m_pDepthStencilView.Release();
    m_pDepthStencil.Release();

    m_pSwapChain.Release();
    m_pSharedRenderToTexture.Release();
    m_pRenderTargetView.Release();


    return hr;
}

HRESULT RenderPlatform11::OnResize(UINT windowWidth, UINT windowHeight, bool renderToSharedTexture)
{
    HRESULT hr = S_OK;

    m_pSwapChain.Release();

#ifdef _XBOX_ONE
    m_swapChainFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
#else
    m_swapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
#endif

    // Setup the viewport
    m_viewPort.Width = (FLOAT)windowWidth;
    m_viewPort.Height = (FLOAT)windowHeight;
    m_viewPort.MinDepth = 0.0f;
    m_viewPort.MaxDepth = 1.0f;
    m_viewPort.TopLeftX = 0;
    m_viewPort.TopLeftY = 0;
    m_immediateContext->RSSetViewports(1, &m_viewPort);

    // Obtain DXGI factory from device (since we used nullptr for pAdapter above)
    CComPtr<IDXGIFactory1> dxgiFactory;
    {
        CComPtr<IDXGIDevice> dxgiDevice;
        hr = GetDevice()->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
        if (SUCCEEDED(hr))
        {
            CComPtr<IDXGIAdapter> adapter;
            hr = dxgiDevice->GetAdapter(&adapter);
            if (SUCCEEDED(hr))
            {
                hr = adapter->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&dxgiFactory));
            }
        }
    }
    if (FAILED(hr))
        return hr;

    // Check MSAA support
    HRR(GetDevice()->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, msaaCount, &m_msaaQuality));
    if (m_msaaQuality == 0)
    {
        m_enableMsaa = false;
    }

    // Enable MSAA
    if (m_enableMsaa)
    {
        D3D11_RASTERIZER_DESC rasterDesc;
        rasterDesc.AntialiasedLineEnable = true; // MSA
        rasterDesc.CullMode = D3D11_CULL_BACK;
        rasterDesc.DepthBias = 0;
        rasterDesc.DepthBiasClamp = 0.0f;
        rasterDesc.DepthClipEnable = true;
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.FrontCounterClockwise = false;
        rasterDesc.MultisampleEnable = true; // MSAA
        rasterDesc.ScissorEnable = false;
        rasterDesc.SlopeScaledDepthBias = 0.0f;
        HRR(GetDevice()->CreateRasterizerState(&rasterDesc, &m_rasterState));
        SetDebugName(m_rasterState, "Game::m_rasterState");
        m_immediateContext->RSSetState(m_rasterState);
    }

    // Create swap chain
    CComPtr<IDXGIFactory2> dxgiFactory2;
    HRR(dxgiFactory->QueryInterface(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgiFactory2)));

    //// DirectX 11.1 or later
    //hr = GetDevice()->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&m_d3dDevice1));
    //if (SUCCEEDED(hr))
    //{
    //    (void)m_immediateContext->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&m_immediateContext1));
    //}

    CComPtr<ID3D11Texture2D> pBackBuffer;

    if (renderToSharedTexture) // Create just a textured to render to.  No double buffering.
    {
        D3D11_TEXTURE2D_DESC Desc;
        Desc.Width = 1600;
        Desc.Height = 1080;
        Desc.MipLevels = 1;
        Desc.ArraySize = 1;
        Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        Desc.SampleDesc.Count = 1;
        Desc.SampleDesc.Quality = 0;
        Desc.Usage = D3D11_USAGE_DEFAULT;
        Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        Desc.CPUAccessFlags = 0;
        Desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

        HRR(GetDevice()->CreateTexture2D(&Desc, NULL, &m_pSharedRenderToTexture));

        pBackBuffer = m_pSharedRenderToTexture;
    }
    else // Create swap chain which includes render target buffer
    {
        DXGI_SWAP_CHAIN_DESC1 sd;
        ZeroMemory(&sd, sizeof(sd));

#if !defined(WIN32)
        sd.Width = windowWidth;
        sd.Height = windowHeight;
#endif

#ifdef _XBOX_ONE
        sd.Flags |= DXGIX_SWAP_CHAIN_MATCH_OTHER_CONSOLES;
#endif
        sd.Format = m_swapChainFormat;

        sd.SampleDesc.Count = m_enableMsaa ? msaaCount : 1;
        sd.SampleDesc.Quality = m_enableMsaa ? m_msaaQuality - 1 : 0;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

        CComPtr<IDXGISwapChain1> pSwapChain1;
        //HRR(dxgiFactory->CreateSwapChain(&sd, dxgiFactory2, &pSwapChain1));

        //#if defined(WIN32) && !defined(TREENGINE_XBOX)
#if defined(TREENGINE_WIN32)
        HRR(dxgiFactory2->CreateSwapChainForHwnd(GetDevice(), m_hwnd, &sd, nullptr, nullptr, &pSwapChain1));
#else
        HRR(dxgiFactory2->CreateSwapChainForCoreWindow(GetDevice(), reinterpret_cast<IUnknown*>(m_window.Get()), &sd, nullptr, &pSwapChain1));
#endif 

        HRR(pSwapChain1->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(&m_pSwapChain)));
    }

    HRR(m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer)));

    // Create a render target view
    HRR(GetDevice()->CreateRenderTargetView(pBackBuffer, nullptr, &m_pRenderTargetView));
    SetDebugName(m_pRenderTargetView, "Game::m_pRenderTargetView");
    pBackBuffer.Release();

    // 
    // Create depth stencil texture
    //
    D3D11_TEXTURE2D_DESC descDepth;
    ZeroMemory(&descDepth, sizeof(descDepth));
    descDepth.Width = windowWidth;
    descDepth.Height = windowHeight;
    descDepth.MipLevels = 1;
    descDepth.ArraySize = 1;
#ifdef _XBOX_ONE
    descDepth.Format = DXGI_FORMAT_D32_FLOAT;
#else
    descDepth.Format = DXGI_FORMAT_R24G8_TYPELESS;
#endif
    descDepth.SampleDesc.Count = IsMSAAEnabled() ? RenderPlatform11::msaaCount : 1;
    descDepth.SampleDesc.Quality = IsMSAAEnabled() ? GetMSAAQuality() - 1 : 0;
    descDepth.Usage = D3D11_USAGE_DEFAULT;
    descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    descDepth.CPUAccessFlags = 0;
    descDepth.MiscFlags = 0;
    HRR(GetDevice()->CreateTexture2D(&descDepth, nullptr, &m_pDepthStencil));
    SetDebugName(m_pDepthStencil, "Game::m_pDepthStencil");

    // Create the depth stencil view
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
    dsvDesc.Flags = 0;
#ifdef _XBOX_ONE
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
#else
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
#endif
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;
    HRR(GetDevice()->CreateDepthStencilView(m_pDepthStencil, &dsvDesc, &m_pDepthStencilView));
    SetDebugName(m_pDepthStencilView, "Game::m_pDepthStencilView");

    // Validation
    ASSERT(m_pRenderTargetView);
    ASSERT(GetSwapChain() || m_pSharedRenderToTexture);
    ASSERT(m_viewPort.Width != 0);
    ASSERT(m_viewPort.Height != 0);


    return S_OK;
}

HRESULT RenderPlatform11::BeginNewFrame(bool /*resetCommandList*/, D3DBuffer* buffer, InstancedData** dataView)
{
    // Compute instance data
    D3D11_MAPPED_SUBRESOURCE mappedData;
    HRR(m_immediateContext->Map(buffer->buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedData));
    *dataView = reinterpret_cast<InstancedData*>(mappedData.pData);

    return S_OK;
}

HRESULT RenderPlatform11::EndFrame(D3DBuffer* buffer)
{
    HRESULT hr = S_OK;
    m_immediateContext->Unmap(buffer->buffer, 0);
    return hr;
}


void RenderPlatform11::SetFrameSceneData(CBChangesEveryFrame* cb)
{
    ID3D11Buffer* buffer = m_constBufferChangesEveryFrame->Resource();
    m_immediateContext->VSSetConstantBuffers(2, 1, &buffer);
    m_immediateContext->PSSetConstantBuffers(2, 1, &buffer);
    m_immediateContext->UpdateSubresource(buffer, 0, nullptr, cb, 0, 0);
}

HRESULT RenderPlatform11::RenderSceneSetup(RenderPass pass, DoubleBuffer* instancedBuffer)
{
    // Set samplers
    const XSF::StockRenderStates& stockStates = XSF::StockRenderStates::GetStates();
    ID3D11SamplerState* samplers[3] = { stockStates.GetSamplerState(StockSamplerStates::MinMagMipLinearUVWWrap),
        stockStates.GetSamplerState(StockSamplerStates::UseShadowMap),
        stockStates.GetSamplerState(StockSamplerStates::MinMagLinearMipPointUVWClamp)
    };
    m_immediateContext->PSSetSamplers(0, 3, samplers);

    // Set shaders
    if (pass == ShadowMapPass)
    {
        m_immediateContext->VSSetShader(m_shadowVertexShader ? m_shadowVertexShader->shader : nullptr, nullptr, 0);
        m_immediateContext->PSSetShader(m_shadowPixelShader ? m_shadowPixelShader->shader : nullptr, nullptr, 0);
    }
    else if (pass == RegularPass)
    {
        m_immediateContext->VSSetShader(*m_vertexShader, nullptr, 0);
        m_immediateContext->PSSetShader(*m_pixelShader, nullptr, 0);
    }

    // Set up input assembler
    m_immediateContext->IASetInputLayout(InputLayouts::InstancedBasic16);
    m_immediateContext->IASetIndexBuffer(m_indexBuffer, DXGI_FORMAT_R32_UINT, 0);
    m_immediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Set vertex buffer
    // TODO: Move to per-object?
    UINT stride[2] = { sizeof(SimpleVertex), sizeof(InstancedData) };
    UINT offset[2] = { 0, 0 };
    ID3D11Buffer* vbs[2] = { m_vertexBuffer.buffer, *instancedBuffer->Get(m_renderData->frame) };
    m_immediateContext->IASetVertexBuffers(0, 2, vbs, stride, offset);

    return S_OK;
}

HRESULT RenderPlatform11::RenderProlog(bool oculus, bool wireframe, bool useAlphaBlendedRenderTarget, bool useShadowMaps, float* clearColor)
{
    if (!oculus)
    {
        // Bind render target and depth
        ID3D11RenderTargetView* rtv = GetRTV();
        m_immediateContext->OMSetRenderTargets(1, &rtv, GetDSV());
    }

    const XSF::StockRenderStates& stockStates = XSF::StockRenderStates::GetStates();
    if (wireframe)
    {
        stockStates.ApplyRasterizerState(m_immediateContext, StockRasterizerStates::Wireframe);
    }

    if (useAlphaBlendedRenderTarget)
    {
        stockStates.ApplyBlendState(m_immediateContext, StockBlendStates::AlphaBlend);
    }
    else
    {
        stockStates.ApplyBlendState(m_immediateContext, StockBlendStates::Overwrite);
    }

    if (!oculus)
    {
        // Clear the back buffer
        m_immediateContext->ClearRenderTargetView(GetRTV(), clearColor);

        // Clear the depth buffer to 1.0 (max depth)
        m_immediateContext->ClearDepthStencilView(GetDSV(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }

    // Make shadow map available to shaders
    if (useShadowMaps)
    {
        ID3D11ShaderResourceView* depthTexture = m_renderData->pShadowMap->DepthMapSRV();
        m_immediateContext->PSSetShaderResources(1, 1, &depthTexture);
    }

    return S_OK;
}

HRESULT RenderPlatform11::RenderEpilog(bool oculus, bool useShadowMaps, bool showShadowBuffer, bool renderToSharedTexture)
{
    HRESULT hr = S_OK;

    // Unbind shadow texture so we can render to it next frame
    if (useShadowMaps)
    {
        ID3D11ShaderResourceView* depthTexture = nullptr;
        m_immediateContext->PSSetShaderResources(1, 1, &depthTexture);
    }

    if (showShadowBuffer)
    {
        HRC(DrawScreenQuad(m_immediateContext, m_renderData->pShadowMap->DepthMapSRV()));
    }

    if (!oculus && !renderToSharedTexture)
    {
        // Present our back buffer to our front buffer
        HRC(GetSwapChain()->Present(0, 0));
    }
Cleanup:
    return hr;
}

HRESULT RenderPlatform11::BeginDrawText()
{
    m_bitmapFont->Begin(m_immediateContext, &m_viewPort, false);
    return S_OK;
}

HRESULT RenderPlatform11::DrawText2(float sx, float sy, DWORD dwColor, const WCHAR* strText)
{
    m_bitmapFont->DrawText(sx, sy, dwColor, strText);
    return S_OK;
}

HRESULT RenderPlatform11::EndDrawText()
{
    m_bitmapFont->End();
    return S_OK;
}

HRESULT RenderPlatform11::DrawIndexedInstanced(
    UINT IndexCountPerInstance,
    UINT InstanceCount,
    UINT StartIndexLocation,
    INT BaseVertexLocation,
    UINT StartInstanceLocation)
{
    m_immediateContext->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
    return S_OK;
}

HRESULT RenderPlatform11::SetRenderPhase(RenderState state)
{
    HRESULT hr = S_OK;
    const XSF::StockRenderStates& stockStates = XSF::StockRenderStates::GetStates();

    switch (state)
    {
    case RP_TRANSITION_TO_RENDER_SHADOW_MAP:
    {
        m_renderData->pShadowMap->BindDsvAndSetNullRenderTarget(GetContext());

        stockStates.ApplyRasterizerState(GetContext(), StockRasterizerStates::BuildShadowMap);
        break;
    }
    case RP_TRANSITION_FROM_RENDER_SHADOW_MAP:
    {
        // Restore state after shadow
        GetContext()->RSSetState(0);
        GetContext()->RSSetViewports(1, &GetViewport());

        stockStates.ApplyRasterizerState(GetContext(), StockRasterizerStates::Solid);
        break;
    }
    }

    return hr;
}

HRESULT RenderPlatform11::LoadTexture(const wchar_t* textureFilename, int /*textureIndex*/, LoadedTexture** loadedTexture)
{
    // Load the Texture
    ID3D11ShaderResourceView* tex = nullptr;
    HRR(CreateDDSTextureFromFile(GetDevice(), textureFilename, nullptr, &tex));

    *loadedTexture = new LoadedTexture(tex);

    assert((*loadedTexture)->texture);

    return S_OK;
}

HRESULT RenderPlatform11::CreateMaterial(const wchar_t* name, LoadedTexture* texture, VertexShader* vs, PixelShader* ps,
    ShaderMaterial& shaderMaterial, StockRenderState renderState, int /*materialNum*/, Material** newMaterial)
{
    UploadBuffer<CBMaterial>* constBuffer = new UploadBuffer<CBMaterial>(GetDevice(), 1, true);
    SetDebugName(constBuffer->Resource(), "RenderManager::CreateMaterial::pConstBuffer");

    if (!vs)
    {
        vs = m_vertexShader;
    }

    if (!ps)
    {
        ps = m_pixelShader;
    }

    Material* newMat = new Material(name, texture, InputLayouts::InstancedBasic16, vs, ps,
        nullptr /*ID3D11SamplerState* samplerState*/, nullptr /*ID3D11RasterizerState* rasterizer*/, nullptr /*ID3D11DepthStencilState* depthState*/,
        shaderMaterial, constBuffer);

    m_gameLevelBuffers.push_back(constBuffer->Resource());

    *newMaterial = newMat;

    return S_OK;
}

HRESULT RenderPlatform11::SetMaterial(Material* material, RenderPass pass)
{
    CBMaterial cb;
    cb.material = material->m_shaderMaterial;

    ID3D11Buffer* constBuffer = material->m_constBuffer->Resource();

    m_immediateContext->VSSetConstantBuffers(3, 1, &constBuffer);
    m_immediateContext->PSSetConstantBuffers(3, 1, &constBuffer);

    // TODO: support arbitary vertex shaders with shadow mapping
    if (pass != ShadowMapPass)
    {
        ID3D11VertexShader* vertexShader = material->m_vertexShader->shader ? material->m_vertexShader->shader : m_vertexShader->shader;
        m_immediateContext->VSSetShader(vertexShader, nullptr, 0);

        ID3D11PixelShader* pixelShader = material->m_pixelShader->shader ? material->m_pixelShader->shader : m_pixelShader->shader;
        m_immediateContext->PSSetShader(pixelShader, nullptr, 0);
    }

    m_immediateContext->UpdateSubresource(material->m_constBuffer->Resource(), 0, nullptr, &cb, 0, 0);

    ID3D11ShaderResourceView* texture = nullptr;

    if (material->m_texture)
    {
        texture = material->m_texture->texture;
    }

    m_immediateContext->PSSetShaderResources(0, 1, &texture);
    return S_OK;
}

HRESULT RenderPlatform11::CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height, int textureIndex, LoadedTexture** loadedTexture)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.ArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Format = DXGI_FORMAT_R32_FLOAT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA subData = {};
    subData.pSysMem = points;
    subData.SysMemPitch = width * sizeof(float);
    subData.SysMemSlicePitch = width * height * sizeof(float);

    CComPtr<ID3D11Texture2D> texture;
    HRR(GetDevice()->CreateTexture2D(&desc, &subData, &texture));
    SetDebugName(texture, "RenderManager::CreateTexture2::procedural");

    CComPtr<ID3D11ShaderResourceView> view;
    HRR(GetDevice()->CreateShaderResourceView(texture, nullptr, &view));
    SetDebugName(view, "RenderManager::CreateTexture2::proc view");

#if 0
    Image img;
    img.width = width;
    img.height = height;
    img.format = DXGI_FORMAT_R32_FLOAT;
    img.rowPitch = subData.SysMemPitch;
    img.slicePitch = subData.SysMemSlicePitch;
    img.pixels = (uint8_t*)subData.pSysMem;
    HR(SaveToDDSFile(img, DDS_FLAGS_NONE, L"FSGraphTexture.DDS"));
#endif

    // Success
    *loadedTexture = new LoadedTexture(view);
    texture.Release();
    view.Detach();

    return S_OK;
}

HRESULT RenderPlatform11::CreateD3DBuffer(const UINT sizeBytes, const UINT numInstances, D3DBuffer** d3dBuffer)
{
    HRESULT hr = S_OK;
    D3DBuffer* newBuffer = new D3DBuffer();

    D3D11_BUFFER_DESC vbd = {};

    vbd.Usage = D3D11_USAGE_DYNAMIC;
    vbd.ByteWidth = sizeBytes; // *numInstances;
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    vbd.MiscFlags = 0;
    vbd.StructureByteStride = 0;
    HRR(GetDevice()->CreateBuffer(&vbd, nullptr, &newBuffer->buffer));

    m_gameLevelBuffers.push_back(newBuffer->buffer);

    *d3dBuffer = newBuffer;

    return hr;
}

HRESULT RenderPlatform11::LoadVertexShader(const wchar_t* shaderFilename, VertexShader** shader)
{
    *shader = nullptr;

    VertexShader* vertexShader = new VertexShader();

    char sbFilename[MAX_PATH];
    size_t converted = 0;
    size_t filenameLen = (wcslen(shaderFilename) + 1) * 2;
    wcstombs_s(&converted, sbFilename, filenameLen, shaderFilename, filenameLen);
    ASSERT(converted * 2 == filenameLen);

    std::vector< BYTE > shaderData;
    HRR(XSF::LoadBlob(shaderFilename, shaderData));

    // Create VS input layout
    // Load regular vertex Shader
    HRR(GetDevice()->CreateVertexShader(&(shaderData)[0], shaderData.size(), nullptr, &vertexShader->shader));
    SetDebugName(vertexShader->shader, sbFilename);

    m_gameLevelVertexShaders.push_back(vertexShader);

    *shader = vertexShader;

    return S_OK;
}

HRESULT RenderPlatform11::LoadPixelShader(const wchar_t* shaderFilename, PixelShader** shader)
{
    PixelShader* pixelShader = new PixelShader();

    char sbFilename[MAX_PATH];
    size_t converted = 0;
    size_t filenameLen = (wcslen(shaderFilename) + 1) * 2;
    wcstombs_s(&converted, sbFilename, filenameLen, shaderFilename, filenameLen);
    ASSERT(converted * 2 == filenameLen);

    std::vector< BYTE > data;
    HRR(XSF::LoadBlob(shaderFilename, data));

    HRR(GetDevice()->CreatePixelShader(&data[0], data.size(), nullptr, &pixelShader->shader));
    SetDebugName(pixelShader->shader, sbFilename);

    m_gameLevelPixelShaders.push_back(pixelShader);

    *shader = pixelShader;

    return S_OK;
}

HRESULT RenderPlatform11::BuildScreenQuadGeometryBuffers()
{
    GeometryGenerator::MeshData quad;

    GeometryGenerator geoGen;
    geoGen.CreateFullscreenQuad(quad);

    // Extract the vertex elements we are interested in and pack the
    // vertices of all the meshes into one vertex buffer.

    std::vector<SimpleVertex> vertices(quad.Vertices.size());

    for (UINT i = 0; i < quad.Vertices.size(); ++i)
    {
        vertices[i].Pos = quad.Vertices[i].Position;
        vertices[i].Normal = quad.Vertices[i].Normal;
        vertices[i].Tex = quad.Vertices[i].TexC;
    }

    D3D11_BUFFER_DESC vbd;
    vbd.Usage = D3D11_USAGE_IMMUTABLE;
    vbd.ByteWidth = (UINT)(sizeof(SimpleVertex) * quad.Vertices.size());
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbd.CPUAccessFlags = 0;
    vbd.MiscFlags = 0;
    D3D11_SUBRESOURCE_DATA vinitData = { 0 };
    vinitData.pSysMem = &vertices[0];
    HRR(GetDevice()->CreateBuffer(&vbd, &vinitData, &m_screenQuadVB));

    SetDebugName(m_screenQuadVB, "RenderManager::m_screenQuadVB");

    //
    // Pack the indices of all the meshes into one index buffer.
    //

    D3D11_BUFFER_DESC ibd;
    ibd.Usage = D3D11_USAGE_IMMUTABLE;
    ibd.ByteWidth = (UINT)(sizeof(UINT) * quad.Indices.size());
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    ibd.CPUAccessFlags = 0;
    ibd.MiscFlags = 0;
    D3D11_SUBRESOURCE_DATA iinitData = { 0 };
    iinitData.pSysMem = &quad.Indices[0];
    HRR(GetDevice()->CreateBuffer(&ibd, &iinitData, &m_screenQuadIB));
    SetDebugName(m_screenQuadIB, "RenderManager::m_screenQuadIB");

    return S_OK;
}

HRESULT RenderPlatform11::DrawScreenQuad(XSF::D3DDeviceContext* pContext, ID3D11ShaderResourceView* depthTexture)
{
    UINT stride = sizeof(SimpleVertex);
    UINT offset = 0;

    pContext->IASetInputLayout(InputLayouts::Basic32);
    pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    pContext->IASetVertexBuffers(0, 1, &m_screenQuadVB, &stride, &offset);
    pContext->IASetIndexBuffer(m_screenQuadIB, DXGI_FORMAT_R32_UINT, 0);

    pContext->VSSetShader(*m_drawScreenVertexShader, nullptr, 0);
    pContext->PSSetShader(*m_drawScreenPixelShader, nullptr, 0);

    pContext->PSSetShaderResources(0, 1, &depthTexture);

    pContext->DrawIndexed(6, 0, 0);

    ID3D11ShaderResourceView* nullText[] = { 0 };
    pContext->PSSetShaderResources(0, 1, nullText);

    return S_OK;
}

void VertexShader::Release()
{
    if (shader)
    {
        shader->Release();
        shader = nullptr;
    }
}

void PixelShader::Release()
{
    if (shader)
    {
        shader->Release();
        shader = nullptr;
    }
}

/*
D3DBuffer::operator bool()
{
    return buffer != nullptr;
}
*/
