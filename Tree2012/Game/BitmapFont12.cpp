//--------------------------------------------------------------------------------------
// BitmapFont12.cpp
//
// BitmapFont class for samples. For details, see header.
//
// Xbox Advanced Technology Group.
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------
#include <pch.h>
#include "RenderManager.h"

#if defined(TREE3D12)
#include "StockRenderStates12.h"
#include "BitmapFont12.h"

using namespace XboxSampleFramework;

// maximum number of quads each instance can render in one go
#define     MAX_QUADS_PER_INSTANCE      1024

//#define     VS_FILE_NAME                L"Media\\shaders\\ATGFontVS.bin"
//#define     PS_FILE_NAME                L"Media\\shaders\\ATGFontPS.bin"
#define     VS_FILE_NAME                L"FontVertexShader.cso"
#define     PS_FILE_NAME                L"FontPixelShader.cso"

// BitmapFont description

#define ATGCALCFONTFILEHEADERSIZE(x) ( sizeof(DWORD) + (sizeof(FLOAT)*4) + sizeof(WORD) + (sizeof(WCHAR)*(x)) )
#define ATGFONTFILEVERSION 5

#define RELEASEFONTSHADERSANDRETURN(hr) ReleaseFontShaders(); \
        DebugPrint("BitmapFont::CreateFontShaders: error %x\n", hr); \
        return hr;

ID3DBlob*                   BitmapFont::s_pVS;    // Created vertex shader
ID3DBlob*                   BitmapFont::s_pPS;    // Created pixel shader
ID3D12Resource*             BitmapFont::s_pIB;
D3D12_INPUT_ELEMENT_DESC    BitmapFont::s_pIL[] = 
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_UINT,  0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
D3D12_INDEX_BUFFER_VIEW     BitmapFont::s_IBView;


struct Vertex
{
    FLOAT   xy[2];
    USHORT  uv[2];
    DWORD   channel;
};

// once the data has been covnerted to little endian these can be removed

inline
DWORD SwapEndian(DWORD v)
{
    const BYTE a = (v >> 0) & 0xff;
    const BYTE b = (v >> 8) & 0xff;
    const BYTE c = (v >> 16) & 0xff;
    const BYTE d = (v >> 24) & 0xff;

    return (a << 24) | (b << 16) | (c << 8) | d;
}

inline
WORD SwapEndian(WORD v)
{
    const BYTE a = (v >> 0) & 0xff;
    const BYTE b = (v >> 8) & 0xff;

    return (a << 8) | (b << 0);
}

inline
SHORT SwapEndian(SHORT v)
{
    WORD vv = SwapEndian(*(WORD*)&v);

    return *(SHORT*)&vv;
}

inline
FLOAT SwapEndian(FLOAT v)
{
    DWORD vv = SwapEndian(*(DWORD*)&v);

    return *((FLOAT*)&vv);
}

inline
WCHAR SwapEndian(WCHAR v)
{
    WORD vv = SwapEndian(*(WORD*)&v);

    return *(WCHAR*)&vv;
}


//--------------------------------------------------------------------------------------
// Name: GlyphAttr
// Desc: Structure to hold information about one glyph (font character image)
//--------------------------------------------------------------------------------------
struct BitmapFont::GlyphAttr
{
    WORD tu1, tv1, tu2, tv2;    // Texture coordinates for the image
    SHORT wOffset;              // Pixel offset for glyph start
    SHORT wWidth;               // Pixel width of the glyph
    SHORT wAdvance;             // Pixels to advance after the glyph
    WORD wMask;                 // Channel mask
};

struct BitmapFont::FontFileHeaderImage
{
    DWORD m_dwFileVersion;          // Version of the font file (Must match FONTFILEVERSION)
    FLOAT m_fFontHeight;            // Height of the font strike in pixels
    FLOAT m_fFontTopPadding;        // Padding above the strike zone
    FLOAT m_fFontBottomPadding;     // Padding below the strike zone
    FLOAT m_fFontYAdvance;          // Number of pixels to move the cursor for a line feed
    WORD m_cMaxGlyph;               // Number of font characters (Should be an odd number to maintain DWORD Alignment)
    WCHAR m_TranslatorTable[1];     // ASCII to Glyph lookup table, NOTE: It's m_cMaxGlyph+1 in size.
                                    // Entry 0 maps to the "Unknown" glyph.    
};

// BitmapFont strike array. Immediately follows the FontFileHeaderImage
// structure image

struct BitmapFont::FontFileStrikesImage
{
    DWORD m_dwNumGlyphs;            // Size of font strike array (First entry is the unknown glyph)
    GlyphAttr m_Glyphs[1];         // Array of font strike uv's etc... NOTE: It's m_dwNumGlyphs in size
};


//--------------------------------------------------------------------------------------
// Name: CreateFontShaders()
// Desc: Creates the global font shaders
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT BitmapFont::CreateFontShaders(D3DDevice* const pDevice, D3DCommandList* const pCmdList)
{
    ATGPROFILETHIS;

    XSF_ASSERT(pDevice);
    XSF_ASSERT(pCmdList);
    
    //
    // There are only two states the globals could be in,
    // Initialized, in which the ref count is increased,
    // Uninialized, in which the vertex/pixel shaders need to be
    // started up and a vertex array created.
    ///
    
    if (s_pVS)
    {
        // Already initialized, so just add to the ref counts
        s_pVS->AddRef();
        s_pPS->AddRef();
        s_pIB->AddRef();
    }
    else
    {
        XSF_ASSERT(nullptr == s_pVS);
        XSF_ASSERT(nullptr == s_pPS);
        XSF_ASSERT(nullptr == s_pIB);

        // load shaders first
        std::vector<BYTE> dataVS, dataPS;

        HRR(XSF::LoadShader(VS_FILE_NAME, &BitmapFont::s_pVS));
        HRR(XSF::LoadShader(PS_FILE_NAME, &BitmapFont::s_pPS));

        // IB
        {
            std::vector<USHORT> ib(MAX_QUADS_PER_INSTANCE * 5);

            for (USHORT i = 0; i < MAX_QUADS_PER_INSTANCE; ++i)
            {
                ib[i * 5 + 0] = i * 4 + 0;
                ib[i * 5 + 1] = i * 4 + 1;
                ib[i * 5 + 2] = i * 4 + 3;
                ib[i * 5 + 3] = i * 4 + 2;
                ib[i * 5 + 4] = D3D12_16BIT_INDEX_STRIP_CUT_VALUE;
            }

            const D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(ib.size() * sizeof(ib[0]));
            const D3D12_HEAP_PROPERTIES defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            XSF_ERROR_IF_FAILED(pDevice->CreateCommittedResource(
                &defaultHeapProperties,
                D3D12_HEAP_FLAG_NONE,
                &bufferDesc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_GRAPHICS_PPV_ARGS(&s_pIB)));

            ResourceBarrier(pCmdList, s_pIB, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
            XSF_ERROR_IF_FAILED(m_uploadHeap.CopyBufferDataToDefaultBuffer(const_cast<const BYTE*>(reinterpret_cast<BYTE*>(&ib[0])), static_cast<SIZE_T>(bufferDesc.Width), pCmdList, s_pIB));
            ResourceBarrier(pCmdList, s_pIB, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);

            s_IBView.BufferLocation = s_pIB->GetGPUVirtualAddress();
            s_IBView.Format = DXGI_FORMAT_R16_UINT;
            s_IBView.SizeInBytes = static_cast<UINT>(ib.size() * sizeof(ib[0]));
        }
    }

    // partially build PSO descriptor and initialize the PSO cache
    ZeroMemory(&m_descPSO, sizeof(m_descPSO));
    m_descPSO.pRootSignature = m_spRootSignature;
    m_descPSO.InputLayout.pInputElementDescs = s_pIL;
    m_descPSO.InputLayout.NumElements = sizeof(s_pIL) / sizeof(s_pIL[0]);
    m_descPSO.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    m_descPSO.VS.pShaderBytecode = s_pVS->GetBufferPointer();
    m_descPSO.VS.BytecodeLength = s_pVS->GetBufferSize();
    m_descPSO.PS.pShaderBytecode = s_pPS->GetBufferPointer();
    m_descPSO.PS.BytecodeLength = s_pPS->GetBufferSize();
    m_descPSO.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF;
    m_descPSO.SampleDesc.Count = 1;
    m_descPSO.SampleMask = UINT_MAX;
    m_descPSO.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    m_descPSO.NumRenderTargets = 1;
    m_descPSO.DSVFormat = DXGI_FORMAT_R24G8_TYPELESS;

    m_PSOCache.Initialize(pDevice, m_descPSO);

    return S_OK;
}


//--------------------------------------------------------------------------------------
// Name: ReleaseFontShaders()
// Desc: Releases the font shaders by reference
//--------------------------------------------------------------------------------------
VOID BitmapFont::ReleaseFontShaders()
{
    // Safely release shaders
    // NOTE: They are released in reverse order of creation
    // to make sure any interdependencies are dealt with
    m_PSOCache.Terminate();
    ZeroMemory(&m_descPSO, sizeof(m_descPSO));
    XSF_SAFE_RELEASE_C(s_pVS);
    XSF_SAFE_RELEASE_C(s_pPS);
    XSF_SAFE_RELEASE_C(s_pIB);
}


//--------------------------------------------------------------------------------------
// Name: BitmapFont()
// Desc: Constructor
//--------------------------------------------------------------------------------------
BitmapFont::BitmapFont():
    m_iCBQuad(c_iCBQuad),
    m_pFontTexture(nullptr),
    m_dwNumGlyphs(0L),
    m_Glyphs(nullptr),
    m_fCursorX(0.0f),
    m_fCursorY(0.0f),
    m_ScaleFactor(1.0f, 1.0f),
    m_fSlantFactor(0.0f),
    m_bRotate(FALSE),
    m_dRotCos(cos(0.0)),
    m_dRotSin(sin(0.0)),
    m_cMaxGlyph(0),
    m_TranslatorTable(nullptr),
    m_dwNestedBeginCount(0L),
    m_pCmdList(nullptr),
    m_windowSet(FALSE),
    m_curRTSx(0),
    m_curRTSy(0),
	m_renderManager(nullptr)
{
}


//--------------------------------------------------------------------------------------
// Name: ~BitmapFont()
// Desc: Destructor
//--------------------------------------------------------------------------------------
BitmapFont::~BitmapFont()
{
    Destroy();
}


//--------------------------------------------------------------------------------------
// Name: Create()
// Desc: Create the font's internal objects (texture and array of glyph info)
//       using the XPR packed resource file
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT BitmapFont::Create(RenderManager* const renderManager, const WCHAR* strFontFileName, const D3D12_RECT* pRc)
{
    ATGPROFILETHIS;

    XSF_ASSERT(renderManager);
    XSF_ASSERT(strFontFileName);

    if (!renderManager || !strFontFileName)
    {
        DebugPrint("BitmapFont::Create: error in parameters\n");
        return E_INVALIDARG;
    }

	m_renderManager = renderManager;
    D3DDevice* const pDevice = GetPlatform(m_renderManager)->GetDevice();
    ID3D12Fence* const pFence = m_renderManager->GetPlatform()->GetFence();
    D3DCommandList* const pCmdList = m_renderManager->GetPlatform()->GetCommandList();

    XSF_ERROR_IF_FAILED(m_frameHeap.Initialize(pDevice, pFence, 256 * 1024, false, 1 /*frame latency*/, L"BitmapFont::FrameHeap"));
    XSF_ERROR_IF_FAILED(m_uploadHeap.Initialize(pDevice, pFence, 4 * 1024 * 1024, false, 1, L"BitmapFont::UploadHeap"));
    
    //m_renderManager->GetPlatform()->ManageUploadHeap(&m_uploadHeap);

    // read the data
    WCHAR tmp[1024];

    _snwprintf_s(tmp, _countof(tmp), _TRUNCATE, L"%s.abc", strFontFileName);
    XSF_ERROR_IF_FAILED(LoadBlob(tmp, m_data));
    
    // read the texture
    std::vector<BYTE> textureData;
    _snwprintf_s(tmp, _countof(tmp), _TRUNCATE, L"%s.tga", strFontFileName);
    XSF_ERROR_IF_FAILED(LoadBlob(tmp, textureData));
    
    // create a texture of tga
#pragma pack(push, 1)
    struct TGA_HEADER
    {
        BYTE  identsize;          // size of ID field that follows 18 BYTE header (0 usually)
        BYTE  colourmaptype;      // type of colour map 0=none, 1=has palette
        BYTE  imagetype;          // type of image 0=none,1=indexed,2=rgb,3=grey,+8=rle packed

        SHORT colourmapstart;     // first colour map entry in palette
        SHORT colourmaplength;    // number of colours in palette
        BYTE  colourmapbits;      // number of bits per palette entry 15,16,24,32

        SHORT xstart;             // image x origin
        SHORT ystart;             // image y origin
        SHORT width;              // image width in pixels
        SHORT height;             // image height in pixels
        BYTE  bits;               // image bits per pixel 8,16,24,32
        BYTE  descriptor;         // image descriptor bits (vh flip bits)
    };
#pragma pack(pop)

    const TGA_HEADER* h = (TGA_HEADER*)&textureData[0];

    ID3D12Resource* pTex = nullptr;
    const D3D12_RESOURCE_DESC tex2DDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_TYPELESS, h->width, h->height, 1, 1);
    const D3D12_HEAP_PROPERTIES defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    XSF_ERROR_IF_FAILED(pDevice->CreateCommittedResource(
        &defaultHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &tex2DDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_GRAPHICS_PPV_ARGS(&pTex)));

    ResourceBarrier(pCmdList, pTex, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_SUBRESOURCE_FOOTPRINT tex2DUploadDesc = CD3DX12_SUBRESOURCE_FOOTPRINT(tex2DDesc.Format, static_cast<UINT>(tex2DDesc.Width), tex2DDesc.Height, 1, 4 * static_cast<UINT>(tex2DDesc.Width));
    XSF_ERROR_IF_FAILED(m_uploadHeap.CopyTextureSubresourceToDefaultTexture(&textureData[sizeof(TGA_HEADER)], tex2DUploadDesc, pCmdList, pTex, 0));
    ResourceBarrier(pCmdList, pTex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // correct endianness
    // TODO: remove when the data is fixed
    {
        FontFileHeaderImage* pHeader = reinterpret_cast<FontFileHeaderImage*>(&m_data[0]);

        pHeader->m_dwFileVersion = SwapEndian(pHeader->m_dwFileVersion);
        pHeader->m_fFontBottomPadding = SwapEndian(pHeader->m_fFontBottomPadding);
        pHeader->m_fFontHeight = SwapEndian(pHeader->m_fFontHeight);
        pHeader->m_fFontTopPadding = SwapEndian(pHeader->m_fFontTopPadding);
        pHeader->m_fFontYAdvance = SwapEndian(pHeader->m_fFontYAdvance);
        pHeader->m_cMaxGlyph = SwapEndian(pHeader->m_cMaxGlyph);

        for (UINT i=0; i < pHeader->m_cMaxGlyph; ++i)
        {
            pHeader->m_TranslatorTable[i] = SwapEndian(pHeader->m_TranslatorTable[i]);
        }

        FontFileStrikesImage* pGlyphs = reinterpret_cast<FontFileStrikesImage*>(&m_data[ATGCALCFONTFILEHEADERSIZE(pHeader->m_cMaxGlyph + 1)]);
        pGlyphs->m_dwNumGlyphs = SwapEndian(pGlyphs->m_dwNumGlyphs);
        for (UINT i=0; i < pGlyphs->m_dwNumGlyphs; ++i)
        {
            pGlyphs->m_Glyphs[i].tu1 = SwapEndian(pGlyphs->m_Glyphs[i].tu1);
            pGlyphs->m_Glyphs[i].tv1 = SwapEndian(pGlyphs->m_Glyphs[i].tv1);
            pGlyphs->m_Glyphs[i].tu2 = SwapEndian(pGlyphs->m_Glyphs[i].tu2);
            pGlyphs->m_Glyphs[i].tv2 = SwapEndian(pGlyphs->m_Glyphs[i].tv2);
            pGlyphs->m_Glyphs[i].wAdvance = SwapEndian(pGlyphs->m_Glyphs[i].wAdvance);
            pGlyphs->m_Glyphs[i].wMask = SwapEndian(pGlyphs->m_Glyphs[i].wMask);
            pGlyphs->m_Glyphs[i].wOffset = SwapEndian(pGlyphs->m_Glyphs[i].wOffset);
            pGlyphs->m_Glyphs[i].wWidth = SwapEndian(pGlyphs->m_Glyphs[i].wWidth);
        }
    }

    return Create(m_renderManager, pTex, &m_data[ 0 ], pRc);
}


//--------------------------------------------------------------------------------------
// Name: Create()
// Desc: Create the font's internal objects (texture and array of glyph info)
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT BitmapFont::Create(RenderManager* renderManager, ID3D12Resource* const pFontTexture, const VOID* pFontData, const D3D12_RECT* pRc)
{
    ATGPROFILETHIS;

    XSF_ASSERT(renderManager);
    
    if (!renderManager || !pFontTexture || !pFontData)
    {
        DebugPrint("BitmapFont::Create: invalid parameters\n");
        return E_INVALIDARG;
    }

	m_renderManager = renderManager;
    D3DDevice* const pDevice = GetPlatform(m_renderManager)->GetDevice();
    D3DCommandList* const pCmdList = m_renderManager->GetPlatform()->GetCommandList();

    // Define root table layout
    CD3DX12_DESCRIPTOR_RANGE descRange[c_numRootParameters];
    descRange[c_rootSRV].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0
    descRange[c_rootCBV].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0); // b0
    descRange[c_rootSampler].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0); // s0

    CD3DX12_ROOT_PARAMETER rootParameters[c_numRootParameters];
    rootParameters[c_rootSRV].InitAsDescriptorTable(1, &descRange[c_rootSRV], D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[c_rootCBV].InitAsDescriptorTable(1, &descRange[c_rootCBV], D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[c_rootSampler].InitAsDescriptorTable(1, &descRange[c_rootSampler], D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_ROOT_SIGNATURE_DESC rootSignature(ARRAYSIZE(rootParameters), rootParameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    D3DBlobPtr spSerializedSignature;
    XSF_RETURN_IF_FAILED(D3D12SerializeRootSignature(&rootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &spSerializedSignature, nullptr));

    XSF_RETURN_IF_FAILED(pDevice->CreateRootSignature(
        c_nodeMask,
        spSerializedSignature->GetBufferPointer(),
        spSerializedSignature->GetBufferSize(),
        IID_GRAPHICS_PPV_ARGS(&m_spRootSignature)));

    XSF_RETURN_IF_FAILED(m_CBSRVHeap.Initialize(pDevice, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, c_CBHeapEnd, true));

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = m_renderManager->GetPlatform()->GetSwapChainFormat();
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = static_cast<UINT>(-1);
    pDevice->CreateShaderResourceView(pFontTexture, &srvDesc, m_CBSRVHeap.hCPU(c_iFontTexture));
   
    // Save a copy of the texture
    m_pFontTexture = pFontTexture;

    // Check version of file (to make sure it matches up with the FontMaker tool)
    const BYTE* pData = static_cast<const BYTE*>(pFontData);
    DWORD dwFileVersion = reinterpret_cast<const FontFileHeaderImage*>(pData)->m_dwFileVersion;

    if (dwFileVersion == ATGFONTFILEVERSION)
    {
        m_fFontHeight = reinterpret_cast<const FontFileHeaderImage*>(pData)->m_fFontHeight;
        m_fFontTopPadding = reinterpret_cast<const FontFileHeaderImage*>(pData)->m_fFontTopPadding;
        m_fFontBottomPadding = reinterpret_cast<const FontFileHeaderImage*>(pData)->m_fFontBottomPadding;
        m_fFontYAdvance = reinterpret_cast<const FontFileHeaderImage*>(pData)->m_fFontYAdvance;

        // Point to the translator string which immediately follows the 4 floats
        m_cMaxGlyph = reinterpret_cast<const FontFileHeaderImage*>(pData)->m_cMaxGlyph;
  
        m_TranslatorTable = const_cast<FontFileHeaderImage*>(reinterpret_cast<const FontFileHeaderImage*>(pData))->m_TranslatorTable;

        pData += ATGCALCFONTFILEHEADERSIZE(m_cMaxGlyph + 1);

        // Read the glyph attributes from the file
        m_dwNumGlyphs = reinterpret_cast<const FontFileStrikesImage*>(pData)->m_dwNumGlyphs;
        m_Glyphs = reinterpret_cast<const FontFileStrikesImage*>(pData)->m_Glyphs;        // Pointer
    }
    else
    {
        DebugPrint("Incorrect version number on font file!\n");
        return E_FAIL;
    }

    // Create the vertex and pixel shaders for rendering the font
    if (FAILED(CreateFontShaders(pDevice, pCmdList)))
    {
        DebugPrint("Could not create font shaders!\n");
        return E_FAIL;
    }

    // Initialize the window
    m_safeAreaInPixels = 48;
    if (pRc)
        SetWindow(*pRc);

    // initialize render state

    // create VS cbuffer with texture scales
    m_cbView.SizeInBytes = static_cast<UINT>(NextMultiple(8 * sizeof(float), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));

    // create a streaming VB for characters
    m_vbView.StrideInBytes = sizeof(Vertex);

    D3D12_RESOURCE_DESC texDesc = m_pFontTexture->GetDesc();

    m_invTexSize[ 0 ] = 1.f / static_cast<FLOAT>(texDesc.Width);
    m_invTexSize[ 1 ] = 1.f / static_cast<FLOAT>(texDesc.Height);

    return S_OK;
}


//--------------------------------------------------------------------------------------
// Name: Destroy()
// Desc: Destroy the font object
//--------------------------------------------------------------------------------------
VOID BitmapFont::Destroy()
{
    m_spRootSignature.Release();

    XSF_SAFE_RELEASE(m_pFontTexture);

    m_CBSRVHeap.Terminate();
    m_frameHeap.Terminate();

    m_dwNumGlyphs = 0L;
    m_Glyphs = nullptr;
    m_cMaxGlyph = 0;
    m_TranslatorTable = nullptr;
    m_dwNestedBeginCount = 0L;

    // Safely release shaders
    ReleaseFontShaders();
}


//--------------------------------------------------------------------------------------
// Name: SetWindow()
// Desc: Sets the  and the bounds rect for drawing text and resets the cursor position
//--------------------------------------------------------------------------------------
VOID BitmapFont::SetWindow(const D3D12_RECT& rcWindow)
{
    m_rcWindow = rcWindow;
    m_windowSet = TRUE;

    m_fCursorX = 0.0f;
    m_fCursorY = 0.0f;
}


//--------------------------------------------------------------------------------------
// Name: ResetWindow()
// Desc: Makes the window track the render target size
//--------------------------------------------------------------------------------------
VOID BitmapFont::ResetWindow(UINT safeAreaInPixels)
{
    m_windowSet = FALSE;
    m_safeAreaInPixels = safeAreaInPixels;
}

//--------------------------------------------------------------------------------------
// Name: GetWindow()
// Desc: Gets the current bounds rect for drawing
//--------------------------------------------------------------------------------------
const D3D12_RECT& BitmapFont::GetWindow() const
{
    return m_rcWindow;
}

//--------------------------------------------------------------------------------------
// Name: SetCursorPosition()
// Desc: Sets the cursor position for drawing text
//--------------------------------------------------------------------------------------
VOID BitmapFont::SetCursorPosition(FLOAT fCursorX, FLOAT fCursorY)
{
    m_fCursorX = floorf(fCursorX);
    m_fCursorY = floorf(fCursorY);
}


//--------------------------------------------------------------------------------------
// Name: SetScaleFactors()
// Desc: Sets X and Y scale factor to make rendered text bigger or smaller.
//       Note that since text is pre-anti-aliased and therefore point-filtered,
//       any scale factors besides 1.0f will degrade the quality.
//--------------------------------------------------------------------------------------

// inlined in the header

//--------------------------------------------------------------------------------------
// Name: SetSlantFactor()
// Desc: Sets the slant factor for rendering slanted text.
//--------------------------------------------------------------------------------------

// inlined in the header

//--------------------------------------------------------------------------------------
// Name: SetRotation()
// Desc: Sets the rotation factor in radians for rendering tilted text.
//--------------------------------------------------------------------------------------
VOID BitmapFont::SetRotationFactor(FLOAT fRotationFactor)
{
    m_bRotate = fRotationFactor != 0;

    m_dRotCos = cosf(fRotationFactor);     // NOTE: Using double precision
    m_dRotSin = sinf(fRotationFactor);
}


//--------------------------------------------------------------------------------------
// Name: GetTextExtent()
// Desc: Get the dimensions of a text string
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
VOID BitmapFont::GetTextExtent(const WCHAR* strText, FLOAT* pWidth, FLOAT* pHeight, BOOL bFirstLineOnly) const
{
    XSF_ASSERT(pWidth != nullptr);
    XSF_ASSERT(pHeight != nullptr);

    // Set default text extent in output parameters
    int iWidth = 0;
    FLOAT fHeight = 0.0f;

    if (strText)
    {
        // Initialize counters that keep track of text extent
        int ix = 0;
        FLOAT fy = m_fFontHeight;       // One character high to start
        if (fy > fHeight)
            fHeight = fy;

        // Loop through each character and update text extent
        DWORD letter;
        while ((letter = *strText) != 0)
        {
            ++strText;

            // Handle newline character
            if (letter == L'\n')
            {
                if (bFirstLineOnly)
                    break;
                ix = 0;
                fy += m_fFontYAdvance;
                // since the height has changed, test against the height extent
                if (fy > fHeight)
                    fHeight = fy;
           }

            // Handle carriage return characters by ignoring them. This helps when
            // displaying text from a file.
            if (letter == L'\r')
                continue;

            // Translate unprintable characters
            const GlyphAttr* pGlyph;
            
            if (letter > m_cMaxGlyph)
                letter = 0;     // Out of bounds?
            else
                letter = m_TranslatorTable[letter];     // Remap ASCII to glyph

            pGlyph = &m_Glyphs[letter];                 // Get the requested glyph

            // Get text extent for this character's glyph
            ix += pGlyph->wOffset;
            ix += pGlyph->wAdvance;

            // Since the x widened, test against the x extent

            if (ix > iWidth)
                iWidth = ix;
        }
    }

    // Convert the width to a float here, load/hit/store. :(
    FLOAT fWidth = static_cast<FLOAT>(iWidth);          // Delay the use if fWidth to reduce LHS pain
    // Apply the scale factor to the result
    
    fWidth *= m_ScaleFactor.x;
    fHeight *= m_ScaleFactor.y;
    
    // Store the final results
    *pWidth = fWidth;
    *pHeight = fHeight;
}


//--------------------------------------------------------------------------------------
// Name: GetTextWidth()
// Desc: Returns the width in pixels of a text string
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
FLOAT BitmapFont::GetTextWidth(const WCHAR* strText) const
{
    FLOAT fTextWidth;
    FLOAT fTextHeight;
    GetTextExtent(strText, &fTextWidth, &fTextHeight);
    return fTextWidth;
}


//--------------------------------------------------------------------------------------
// Name: Begin()
// Desc: Prepares the font vertex buffers for rendering.
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
VOID BitmapFont::Begin(const D3D12_VIEWPORT* pViewport)
{
    // Set state on the first call
    if (0 == m_dwNestedBeginCount)
    {
        XSF_ASSERT(m_pCmdList == nullptr);
        m_pCmdList = m_renderManager->GetPlatform()->GetCommandList();

        XSFBeginNamedEvent(m_pCmdList, 0, L"Text rendering");

        const StockRenderStates& stockStates = StockRenderStates::GetInstance();

        // Finalize PSO
        stockStates.ApplyRasterizerState(&m_descPSO, StockRasterizerStates::Solid);
        FLOAT zero[4] = {0};
        stockStates.ApplyBlendState(m_pCmdList, &m_descPSO, StockBlendStates::AlphaBlend, zero);
        stockStates.ApplyDepthStencilState(m_pCmdList, &m_descPSO, StockDepthStencilStates::AlwaysSucceedNoZWriteNoStencil);

        m_pCmdList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        m_pCmdList->IASetIndexBuffer(&s_IBView);

        ID3D12PipelineState *pPipelineState = m_PSOCache.FindOrCreatePSO(m_descPSO);
        XSF_ASSERT(pPipelineState != nullptr);
        m_pCmdList->SetPipelineState(pPipelineState);

        // set root signature and descriptor heaps
        m_pCmdList->SetGraphicsRootSignature(m_spRootSignature);
        ID3D12DescriptorHeap *pSamplerHeap = stockStates.GetSamplerHeap();
        ID3D12DescriptorHeap* pHeaps[] = { m_CBSRVHeap, pSamplerHeap };
        m_pCmdList->SetDescriptorHeaps(ARRAYSIZE(pHeaps), pHeaps);
                
        // Set root descriptors
        const D3D12_GPU_DESCRIPTOR_HANDLE hFontTexture = m_CBSRVHeap.hGPU(c_iFontTexture);
        m_pCmdList->SetGraphicsRootDescriptorTable(c_rootSRV, hFontTexture);

        const D3D12_GPU_DESCRIPTOR_HANDLE hSampler = stockStates.GetSamplerGPUHandle(StockSamplerStates::MinMagLinearMipPointUVWClamp);
        m_pCmdList->SetGraphicsRootDescriptorTable(c_rootSampler, hSampler);
        
        // obtain render target size
        Viewport viewport = { 0 };
        if (pViewport != nullptr)
        {
            viewport.Width = pViewport->Width;
            viewport.Height = pViewport->Height;
        }
        else
        {
            D3D12_VIEWPORT viewport12 = { 0 };
            m_renderManager->GetViewport(viewport);
        }
        m_curRTSx = std::max(m_safeAreaInPixels * 2 + 1, static_cast<UINT>(viewport.Width));
        m_curRTSy = std::max(m_safeAreaInPixels * 2 + 1, static_cast<UINT>(viewport.Height));
        
        // set the window if it's not set
        if (!m_windowSet)
        {
            XSF_ASSERT(m_safeAreaInPixels * 2 < m_curRTSx);
            XSF_ASSERT(m_safeAreaInPixels * 2 < m_curRTSy);

            m_rcWindow.left = m_safeAreaInPixels;
            m_rcWindow.right = m_curRTSx - m_safeAreaInPixels;
            m_rcWindow.top = m_safeAreaInPixels;
            m_rcWindow.bottom = m_curRTSy - m_safeAreaInPixels;
        }
    }

    XSF_ASSERT(m_pCmdList == m_renderManager->GetPlatform()->GetCommandList());

    // Keep track of the nested begin/end calls.
    m_dwNestedBeginCount++;
}


//--------------------------------------------------------------------------------------
// Name: DrawTextF()
// Desc: Draws text as textured polygons
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
VOID BitmapFont::DrawTextF(FLOAT sx, FLOAT sy, DWORD dwColor, DWORD dwFlags, FLOAT fMaxPixelWidth, const WCHAR* strText, ...)
{
    va_list ap;

    va_start(ap, strText);

    WCHAR buf[1024];
    vswprintf_s(buf, _countof(buf) - 1, strText, ap);
    buf[_countof(buf) - 1] = 0;

    va_end(ap);

    DrawText(sx, sy, dwColor, buf, dwFlags, fMaxPixelWidth);
}


//--------------------------------------------------------------------------------------
// Name: DrawText()
// Desc: Draws text as textured polygons
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
VOID BitmapFont::DrawText(DWORD dwColor, const WCHAR* strText, DWORD dwFlags, FLOAT fMaxPixelWidth)
{
    DrawText(m_fCursorX, m_fCursorY, dwColor, strText, dwFlags, fMaxPixelWidth);
}


//--------------------------------------------------------------------------------------
// Name: DrawText()
// Desc: Draws text as textured polygons
//       TODO: This function should use the Begin/SetVertexData/End() API when it
//       becomes available.
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
VOID BitmapFont::DrawText(FLOAT fOriginX, FLOAT fOriginY, DWORD dwColor, const WCHAR* strText, DWORD dwFlags, FLOAT fMaxPixelWidth)
{
    XSF_ASSERT(m_pCmdList);

    D3DDevice* const pDevice = GetPlatform(m_renderManager)->GetDevice();

    if (nullptr == strText)
        return;

    if (L'\0' == strText[0])
        return;
 
    UINT uNumChars = static_cast<UINT>(wcslen(strText) + (dwFlags & ALIGN_TRUNCATED ? 3 : 0));

    // Create a PIX user-defined event that encapsulates all of the text draw calls.
    // This makes DrawText calls easier to recognize in PIX captures, and it makes
    // them take up fewer entries in the event list.
    XSFScopedNamedEvent(m_pCmdList, XTF_COLOR_DRAW_TEXT, L"BitmapFont::DrawText (%d chars)", uNumChars);

    // load vertex shader constants
    {
        FLOAT *pData;
        XSF_ERROR_IF_FAILED(m_frameHeap.GetHeapPointer(m_cbView.SizeInBytes, m_cbView.BufferLocation, reinterpret_cast<BYTE**>(&pData)));

        pData[0] = m_invTexSize[0];
        pData[1] = m_invTexSize[1];
        pData[2] = 0;
        pData[3] = 0;
        pData[4] = ((dwColor & 0x00ff0000) >> 16L) / 255.0F; 
        pData[5] = ((dwColor & 0x0000ff00) >> 8L) / 255.0F;
        pData[6] = ((dwColor & 0x000000ff) >> 0L) / 255.0F;
        pData[7] = ((dwColor & 0xff000000) >> 24L) / 255.0F;

        pDevice->CreateConstantBufferView(&m_cbView, m_CBSRVHeap.hCPU(m_iCBQuad));
        m_pCmdList->SetGraphicsRootDescriptorTable(c_rootCBV, m_CBSRVHeap.hGPU(m_iCBQuad));

        if (++m_iCBQuad >= c_CBHeapEnd)
        {
            m_iCBQuad = c_iCBQuad;
        }
    }

    // Set up stuff to prepare for drawing text
    Begin();

    // Set the starting screen position
    if ((fOriginX < 0.0f) || ((dwFlags & ALIGN_RIGHT) && (fOriginX <= 0.0f)))
    {
        fOriginX += (m_rcWindow.right - m_rcWindow.left);
    }
    if (fOriginY < 0.0f)
    {
        fOriginY += (m_rcWindow.bottom - m_rcWindow.top);
    }

    m_fCursorX = floorf(fOriginX);
    m_fCursorY = floorf(fOriginY);

    // Adjust for padding
    fOriginY -= m_fFontTopPadding;

    FLOAT fEllipsesPixelWidth = m_ScaleFactor.x * 3.0f * (m_Glyphs[ m_TranslatorTable[L'.'] ].wOffset + m_Glyphs[ m_TranslatorTable[L'.'] ].wAdvance);

    if (dwFlags & ALIGN_TRUNCATED)
    {
        // Check if we will really need to truncate the string
        if (fMaxPixelWidth <= 0.0f)
        {
            dwFlags &= (~ALIGN_TRUNCATED);
        }
        else
        {
            FLOAT w, h;
            GetTextExtent(strText, &w, &h, FALSE);

            // If not, then clear the flag
            if (w <= fMaxPixelWidth)
                dwFlags &= (~ALIGN_TRUNCATED);
        }
    }

    // If vertically centered, offset the starting m_fCursorY value
    if (dwFlags & ALIGN_CENTER_Y)
    {
        FLOAT w, h;
        GetTextExtent(strText, &w, &h);
        m_fCursorY = floorf(m_fCursorY - (h * 0.5f));
    }

    // Add window offsets
    FLOAT invScaleX = 1.f / FLOAT(m_curRTSx);
    FLOAT invScaleY = 1.f / FLOAT(m_curRTSy);
    FLOAT Winx = static_cast<FLOAT>(m_rcWindow.left);
    FLOAT Winy = static_cast<FLOAT>(m_rcWindow.top);
    fOriginX += Winx;
    fOriginY += Winy;
    m_fCursorX += Winx;
    m_fCursorY += Winy;

    // Set a flag so we can determine initial justification effects
    BOOL bStartingNewLine = TRUE;

    DWORD dwNumEllipsesToDraw = 0;

    // Begin drawing the vertices

    volatile Vertex* pVertex;

    const UINT uLockedNumChars = uNumChars;
    HRESULT hr = BeginQuads(uNumChars, (VOID**)(&pVertex));
    if (FAILED(hr))
    {
        DebugPrint("Bitmapfont: buffer out of memory\n");
        return;
    }

    bStartingNewLine = TRUE;

    // Draw four vertices for each glyph
    while (*strText)
    {
        WCHAR letter;

        if (dwNumEllipsesToDraw)
        {
            letter = L'.';
        }
        else
        {
            // If starting text on a new line, determine justification effects
            if (bStartingNewLine)
            {
                if (dwFlags & (ALIGN_RIGHT | ALIGN_CENTER_X))
                {
                    // Get the extent of this line
                    FLOAT w, h;
                    GetTextExtent(strText, &w, &h, TRUE);

                    // Offset this line's starting m_fCursorX value
                    if (dwFlags & ALIGN_RIGHT)
                        m_fCursorX = floorf(fOriginX - w);
                    if (dwFlags & ALIGN_CENTER_X)
                        m_fCursorX = floorf(fOriginX - w * 0.5f);
                }
                bStartingNewLine = FALSE;
            }

            // Get the current letter in the string
            letter = *strText++;

            // Handle the newline character
            if (letter == L'\n')
            {
                m_fCursorX = fOriginX;
                m_fCursorY += m_fFontYAdvance * m_ScaleFactor.y;
                bStartingNewLine = TRUE;
                continue;
            }

            // Handle carriage return characters by ignoring them. This helps when
            // displaying text from a file.
            if (letter == L'\r')
                continue;
        }

        // Translate unprintable characters
        const GlyphAttr* pGlyph = &m_Glyphs[(letter < m_cMaxGlyph) ? m_TranslatorTable[letter] : 0];

        FLOAT fOffset = m_ScaleFactor.x * (FLOAT)pGlyph->wOffset;
        FLOAT fAdvance = m_ScaleFactor.x * (FLOAT)pGlyph->wAdvance;
        FLOAT fWidth = m_ScaleFactor.x * (FLOAT)pGlyph->wWidth;
        FLOAT fHeight = m_ScaleFactor.y * m_fFontHeight;

        if (0 == dwNumEllipsesToDraw)
        {
            if (dwFlags & ALIGN_TRUNCATED)
            {
                // Check if we will be exceeded the max allowed width
                if (m_fCursorX + fOffset + fWidth + fEllipsesPixelWidth + m_fSlantFactor > fOriginX + fMaxPixelWidth)
                {
                    // Yup, draw the three ellipses dots instead
                    dwNumEllipsesToDraw = 3;

                    // Fast forward to the next line
                    while (*strText && *strText != '\n')
                    {
                        strText++;
                    }

                    continue;
                }
            }
        }

        // Setup the screen coordinates
        m_fCursorX += fOffset;
        FLOAT X4 = m_fCursorX;
        FLOAT X1 = X4 + m_fSlantFactor;
        FLOAT X3 = X4 + fWidth;
        FLOAT X2 = X1 + fWidth;
        FLOAT Y1 = m_fCursorY;
        FLOAT Y3 = Y1 + fHeight;
        FLOAT Y2 = Y1;
        FLOAT Y4 = Y3;

        // Rotate the points by the rotation factor
        if (m_bRotate)
        {
            RotatePoint(&X1, &Y1, fOriginX, fOriginY);
            RotatePoint(&X2, &Y2, fOriginX, fOriginY);
            RotatePoint(&X3, &Y3, fOriginX, fOriginY);
            RotatePoint(&X4, &Y4, fOriginX, fOriginY);
        }

        m_fCursorX += fAdvance;

        // Select the RGBA channel that the compressed glyph is stored in
        // Takes a 4 bit per pixel ARGB value and expand it to an 8 bit per pixel ARGB value

        DWORD dwChannelSelector = pGlyph->wMask;

        // Add the vertices to draw this glyph

        USHORT tu1 = pGlyph->tu1;
        USHORT tv1 = pGlyph->tv1;
        USHORT tu2 = pGlyph->tu2;
        USHORT tv2 = pGlyph->tv2;

        pVertex[0].xy[0] = 2 * X1 * invScaleX - 1;
        pVertex[0].xy[1] = 1 - 2 * Y1 * invScaleY;
        pVertex[0].uv[0] = tu1;
        pVertex[0].uv[1] = tv1;
        pVertex[0].channel = dwChannelSelector;

        pVertex[1].xy[0] = 2 * X2 * invScaleX - 1;
        pVertex[1].xy[1] = 1 - 2 * Y2 * invScaleY;
        pVertex[1].uv[0] = tu2;
        pVertex[1].uv[1] = tv1;
        pVertex[1].channel = dwChannelSelector;

        pVertex[2].xy[0] = 2 * X3 * invScaleX - 1;
        pVertex[2].xy[1] = 1 - 2 * Y3 * invScaleY;
        pVertex[2].uv[0] = tu2;
        pVertex[2].uv[1] = tv2;
        pVertex[2].channel = dwChannelSelector;

        pVertex[3].xy[0] = 2 * X4 * invScaleX - 1;
        pVertex[3].xy[1] = 1 - 2 * Y4 * invScaleY;
        pVertex[3].uv[0] = tu1;
        pVertex[3].uv[1] = tv2;
        pVertex[3].channel = dwChannelSelector;

        pVertex += 4;

        // If drawing ellipses, exit when they're all drawn
        if (dwNumEllipsesToDraw)
        {
            --dwNumEllipsesToDraw;
        }

        uNumChars--;
    }

    // Stop drawing vertices
    EndQuads(uLockedNumChars - uNumChars);

    // Undo window offsets
    m_fCursorX -= Winx;
    m_fCursorY -= Winy;

    // Call End() to complete the begin/end pair for drawing text
    End();
}


//--------------------------------------------------------------------------------------
// Name: End()
// Desc: Paired call that restores state set in the Begin() call.
//--------------------------------------------------------------------------------------
VOID BitmapFont::End()
{
    XSF_ASSERT(m_pCmdList);
    XSF_ASSERT(m_dwNestedBeginCount > 0);

    if (--m_dwNestedBeginCount == 0)
    {
        XSFEndNamedEvent(m_pCmdList);

        m_pCmdList = nullptr;
    }
}


//--------------------------------------------------------------------------------------
// Name: RotatePoint()
// Desc: Rotate a 2D point around the origin
//-------------------------------------------------------------------------------------
_Use_decl_annotations_
VOID BitmapFont::RotatePoint(FLOAT* X, FLOAT* Y, DOUBLE OriginX, DOUBLE OriginY) const
{
    DOUBLE dTempX = *X - OriginX;
    DOUBLE dTempY = *Y - OriginY;
    DOUBLE dXprime = OriginX + (m_dRotCos * dTempX - m_dRotSin * dTempY);
    DOUBLE dYprime = OriginY + (m_dRotSin * dTempX + m_dRotCos * dTempY);

    *X = static_cast<FLOAT>(dXprime);
    *Y = static_cast<FLOAT>(dYprime);
}


//--------------------------------------------------------------------------------------
// Name: BeginQuads()
// Desc: Maps a portion of the vertex buffer and returns the pointer to it
//-------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT BitmapFont::BeginQuads(UINT uNumQuads, VOID** ppData)
{
    XSF_ASSERT(m_pCmdList);

    if (uNumQuads >= MAX_QUADS_PER_INSTANCE)
        return E_FAIL;

    XSF_RETURN_IF_FAILED(m_frameHeap.GetHeapPointer(uNumQuads * m_vbView.StrideInBytes * 4, m_vbView.BufferLocation, reinterpret_cast<BYTE**>(ppData)));

    return S_OK;
}


//--------------------------------------------------------------------------------------
// Name: EndQuads()
// Desc: Unmaps the vertex buffer and draws the quads
//-------------------------------------------------------------------------------------
VOID BitmapFont::EndQuads(UINT uNumQuadsUsed)
{
    XSF_ASSERT(m_pCmdList);

    // Finalize dynamic VB
    m_vbView.SizeInBytes = uNumQuadsUsed * 4 * m_vbView.StrideInBytes;
    m_pCmdList->IASetVertexBuffers(0, 1, &m_vbView);

    m_pCmdList->DrawIndexedInstanced(5 * uNumQuadsUsed, 1, 0, 0, 0);
}

#endif